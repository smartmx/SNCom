// sncom/src/platform/serial.cpp —— M0.5 spike
#include "serial.h"
#include <setupapi.h>
#include <devguid.h>
#include <cwchar>
#include <cstdlib>
#include <cstdio>

namespace sncom {

// §99（审查 U6）：引擎写队列字节上限——端口被流控阻塞时写队列原为无界增长。
// 队列空时必收（保证单段大包可推进；UI 侧 16MB kTxPendCap 是外层界限），
// 非空且超限拒收返回 0，由 UI 待发队列接管（与 net 的 kOutPendCap 同口径）。
static const size_t kSerialWqCapBytes = 1u << 20;

// ---------------- 枚举（SetupDi，GUID_DEVCLASS_PORTS） ----------------
static int PortNumber(const std::wstring& s) {         // "COM10" → 10
    if (s.size() > 3 && _wcsnicmp(s.c_str(), L"COM", 3) == 0)
        return _wtoi(s.c_str() + 3);
    return 0;
}

std::vector<std::pair<std::wstring, std::wstring>> SerialEngine::EnumPorts() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    HDEVINFO dev = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (dev == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA di{};
    di.cbSize = sizeof(di);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(dev, i, &di); ++i) {
        wchar_t name[256]{};
        DWORD type = 0;
        if (SetupDiGetDeviceRegistryPropertyW(dev, &di, SPDRP_FRIENDLYNAME, &type,
                                              (PBYTE)name, sizeof(name), nullptr) && name[0]) {
            // 友好名形如 "USB-SERIAL CH340 (COM3)" → 端口名 "(COMxx)" + 描述（← PyCom 口径）
            const wchar_t* l = wcsrchr(name, L'(');
            if (l && _wcsnicmp(l, L"(COM", 4) == 0) {
                wchar_t port[16]{};
                size_t n = wcslen(l + 1);
                if (n > 14) n = 14;
                wmemcpy(port, l + 1, n);
                wchar_t* r = wcsrchr(port, L')');
                if (r) *r = 0;
                if (port[0]) {
                    wchar_t desc[256]{};
                    SetupDiGetDeviceRegistryPropertyW(dev, &di, SPDRP_DEVICEDESC, &type,
                                                      (PBYTE)desc, sizeof(desc), nullptr);
                    out.emplace_back(port, desc);
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(dev);
    for (size_t i = 1; i < out.size(); ++i)            // 按端口号升序
        for (size_t j = i; j > 0 && PortNumber(out[j - 1].first) > PortNumber(out[j].first); --j)
            std::swap(out[j - 1], out[j]);
    return out;
}

// ---------------- 消息投递（所有权转移给接收方） ----------------
void SerialEngine::PostRx(const uint8_t* d, size_t n) {
    uint8_t* p = new uint8_t[n];
    memcpy(p, d, n);
    if (!PostMessageW(notify_, kMsgSerialRx, (WPARAM)n, (LPARAM)p)) delete[] p;
}

void SerialEngine::PostEvt(int code, const wchar_t* text) {
    size_t n = wcslen(text);
    wchar_t* p = new wchar_t[n + 1];
    wmemcpy(p, text, n);
    p[n] = 0;
    // 事件也带会话代号：陈旧会话的事件（如迟到的"串口断开"）不得污染新连接
    if (!PostMessageW(notify_, kMsgSerialEvt,
                      (WPARAM)(((UINT_PTR)threadGen_ << 8) | (UINT_PTR)(code & 0xFF)), (LPARAM)p))
        delete[] p;
}

void SerialEngine::PostSt(bool open) {
    PostMessageW(notify_, kMsgSerialSt,
                 (WPARAM)(((UINT_PTR)threadGen_ << 8) | (open ? 1u : 0u)), 0);
}

SerialEngine::~SerialEngine() { Close(); }

bool SerialEngine::Open(HWND notify, const SerialParams& p) {
    if (thread_.joinable()) return false;
    p_ = p;
    notify_ = notify;
    closing_ = false;                              // 新会话：ABORTED 重新视为可疑

    if (!p.loopback) {
        std::wstring dev = L"\\\\.\\" + p.port;
        hComm_ = CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (hComm_ == INVALID_HANDLE_VALUE) return false;

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(hComm_, &dcb)) { CloseHandle(hComm_); hComm_ = INVALID_HANDLE_VALUE; return false; }
        dcb.BaudRate = (DWORD)p.baud;
        dcb.ByteSize = (BYTE)p.databits;
        dcb.Parity   = p.parity == 1 ? EVENPARITY : p.parity == 2 ? ODDPARITY :
                       p.parity == 3 ? MARKPARITY : p.parity == 4 ? SPACEPARITY : NOPARITY;
        dcb.StopBits = p.stopbits == 1 ? ONE5STOPBITS : p.stopbits == 2 ? TWOSTOPBITS : ONESTOPBIT;
        dcb.fDtrControl = p.dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
        dcb.fRtsControl = p.rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fBinary = TRUE;
        dcb.fParity = (p.parity != 0);
        if (!SetCommState(hComm_, &dcb)) { CloseHandle(hComm_); hComm_ = INVALID_HANDLE_VALUE; return false; }

        // 读：一有字节立即完成（Interval=MAXDWORD, Mult=MAXDWORD, Const=MAXDWORD-1 的经典组合）
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = MAXDWORD;
        to.ReadTotalTimeoutMultiplier = MAXDWORD;
        to.ReadTotalTimeoutConstant = MAXDWORD - 1;
        to.WriteTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant = 2000;
        SetCommTimeouts(hComm_, &to);
        PurgeComm(hComm_, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    }

    stopEvt_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvt_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvt_ || !wakeEvt_) {
        Close(); 
        return false;
    }
    threadGen_ = ++gen_;                    // 本次会话代号（Close() 会再自增以作废旧会话）
    thread_ = std::thread(&SerialEngine::ThreadProc, this);
    return true;
}

void SerialEngine::Close() {
    if (!thread_.joinable()) return;
    closing_ = true;                               // 本方主动关闭：ABORTED 不报断线
    SetEvent(stopEvt_);
    if (hComm_ != INVALID_HANDLE_VALUE) CancelIoEx(hComm_, nullptr);
    if (wakeEvt_) SetEvent(wakeEvt_);
    thread_.join();
    if (hComm_ != INVALID_HANDLE_VALUE) { CloseHandle(hComm_); hComm_ = INVALID_HANDLE_VALUE; }
    if (stopEvt_) { CloseHandle(stopEvt_); stopEvt_ = nullptr; }
    if (wakeEvt_) { CloseHandle(wakeEvt_); wakeEvt_ = nullptr; }
    { std::lock_guard<std::mutex> lk(mtx_); wq_.clear(); wqBytes_ = 0; }
    gen_++;                     // 作废本会话：队列里迟到的 St/Evt 一律被 UI 丢弃
    // 状态通知由 I/O 线程退出路径统一 PostSt(false)（断线自退出与主动关闭同一来源）；
    // 主动关闭时 UI 侧自行调 OnSerialState(false)，不在此重复投递
}

bool SerialEngine::ModemStatus(DWORD& ms) {
    if (hComm_ == INVALID_HANDLE_VALUE) return false;
    DWORD v = 0;
    if (!GetCommModemStatus(hComm_, &v)) return false;
    ms = v;
    return true;
}

void SerialEngine::SetBreak(bool on) {
    if (hComm_ == INVALID_HANDLE_VALUE) return;
    if (on) SetCommBreak(hComm_);
    else ClearCommBreak(hComm_);
}

void SerialEngine::SetDtrRts(bool dtr, bool rts) {
    if (hComm_ == INVALID_HANDLE_VALUE) return;
    EscapeCommFunction(hComm_, dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(hComm_, rts ? SETRTS : CLRRTS);
}

int SerialEngine::Write(const uint8_t* d, size_t n) {
    if (!thread_.joinable() || !n) return 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!wq_.empty() && wqBytes_ + n > kSerialWqCapBytes) return 0;   // §99：满则拒收
        wq_.emplace_back(d, d + n);
        wqBytes_ += n;
    }
    SetEvent(wakeEvt_);
    return (int)n;
}

// ---------------- I/O 线程 ----------------
void SerialEngine::ThreadProc() {
    PostSt(true);
    if (p_.loopback) {
        // 软件回环：写请求 → 模拟 2ms 线路延时 → 回投为接收（验证线程/消息/渲染链路）
        while (true) {
            HANDLE hs[2] = { stopEvt_, wakeEvt_ };
            DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) break;
            std::vector<uint8_t> req;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (!wq_.empty()) { req = std::move(wq_.front()); wq_.pop_front(); wqBytes_ -= req.size(); }
                if (!wq_.empty()) SetEvent(wakeEvt_);     // 还有活 → 自我唤醒继续
            }
            if (!req.empty()) { Sleep(2); PostRx(req.data(), req.size()); }
        }
    PostSt(false);
        return;
    }

    // 真实串口：常驻 pending 读 + overlapped 写
    uint8_t buf[8192];
    OVERLAPPED ovRd{}, ovWr{};
    ovRd.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovWr.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ovRd.hEvent || !ovWr.hEvent) {
        PostEvt(kEvtError, L"创建重叠事件失败");
        if (ovRd.hEvent) CloseHandle(ovRd.hEvent);
        if (ovWr.hEvent) CloseHandle(ovWr.hEvent);
    PostSt(false);
        return;
    }

    auto fatal = [&](const wchar_t* what, DWORD e) {       // 致命错误：退出线程 → 断线重连
        wchar_t msg[128];
        swprintf(msg, 128, L"%ls（错误 %lu）", what, (unsigned long)e);
        PostEvt(kEvtError, msg);
    };

    DWORD nread = 0;
    if (!ReadFile(hComm_, buf, sizeof(buf), &nread, &ovRd) &&
        GetLastError() != ERROR_IO_PENDING) {
        fatal(L"串口断开", GetLastError());                 // 拔线/驱动失效：起手读即败
        CloseHandle(ovRd.hEvent);
        CloseHandle(ovWr.hEvent);
    PostSt(false);
        return;
    }

    while (true) {
        HANDLE hs[3] = { stopEvt_, ovRd.hEvent, wakeEvt_ };
        DWORD w = WaitForMultipleObjects(3, hs, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) break;                     // stop

        if (w == WAIT_OBJECT_0 + 1) {                      // 读完成
            DWORD n = 0;
            if (!GetOverlappedResult(hComm_, &ovRd, &n, FALSE)) {
                DWORD e = GetLastError();
#ifndef CE_BREAK
#define CE_BREAK    0x0010
#define CE_FRAME    0x0008
#define CE_OVERRUN  0x0002
#define CE_RXPARITY 0x0004
#endif
                if (e == CE_BREAK) {
                    // §73：线路 BREAK（Win32 通信事件）——非致命：上报状态显示后续读
                    // （此前被当"断线"fatal 误杀连接）
                    PostEvt(kEvtBreak, L"线路 BREAK 信号");
                    ClearCommError(hComm_, nullptr, nullptr);
                    n = 0;
                } else if (e == CE_FRAME || e == CE_RXPARITY || e == CE_OVERRUN) {
                    // 线路噪声类错误：提示但不杀连接（连续错误由用户从现象判断）
                    const wchar_t* what = (e == CE_FRAME) ? L"帧错误"
                                        : (e == CE_RXPARITY) ? L"奇偶校验错误" : L"接收溢出";
                    wchar_t msg[96];
                    swprintf(msg, 96, L"线路%ls（通信错误码 %lu，已忽略）", what, (unsigned long)e);
                    PostEvt(kEvtError, msg);
                    ClearCommError(hComm_, nullptr, nullptr);
                    n = 0;
                } else if (e == ERROR_OPERATION_ABORTED || e == ERROR_HANDLE_EOF) {
                    // 主动关闭的 CancelIoEx 走这里；设备消失时驱动也可能以 ABORTED
                    // 收尾——非本方关闭则补一条断线事件（审查 P3-1）
                    if (!closing_) fatal(L"串口断开", e);
                    break;
                } else if (e != ERROR_IO_INCOMPLETE) {     // 拔线等致命错：断线 → 重连
                    fatal(L"串口断开", e);
                    break;
                } else {
                    n = 0;
                }
            }
            if (n > 0) PostRx(buf, n);
            ResetEvent(ovRd.hEvent);
            if (!ReadFile(hComm_, buf, sizeof(buf), &nread, &ovRd) &&
                GetLastError() != ERROR_IO_PENDING) {
                fatal(L"串口断开", GetLastError());
                break;
            }
            continue;
        }

        if (w == WAIT_OBJECT_0 + 2) {                      // 写请求
            // §101（用户报：发送文件期间接收停止显示）：**一次唤醒只写一块**——旧实现把队列
            // 一口气排空才回 WaitForMultipleObjects，持续写入（文件发送连续档 64KB/tick 持续
            // 补队列）时排空永不结束 → 读完成事件全程无人应答 → 接收完全停摆。改为单块 +
            // 队列未空自我唤醒，让等待循环在读写事件间公平轮转（loopback 分支素来同姿势）。
            std::vector<uint8_t> req;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (!wq_.empty()) { req = std::move(wq_.front()); wq_.pop_front(); wqBytes_ -= req.size(); }
            }
            if (!req.empty()) {
                DWORD wrote = 0;
                BOOL ok = WriteFile(hComm_, req.data(), (DWORD)req.size(), &wrote, &ovWr);
                bool okWrite = false;
                if (!ok && GetLastError() == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(ovWr.hEvent, 3000) != WAIT_OBJECT_0 ||
                        !GetOverlappedResult(hComm_, &ovWr, &wrote, FALSE)) {
                        wchar_t msg[96];
                        swprintf(msg, 96, L"写入失败（错误 %lu）", (unsigned long)GetLastError());
                        PostEvt(kEvtError, msg);
                    } else {
                        ResetEvent(ovWr.hEvent);
                        okWrite = true;
                    }
                } else if (ok) {
                    ResetEvent(ovWr.hEvent);
                    okWrite = true;
                } else {
                    wchar_t msg[96];
                    swprintf(msg, 96, L"写入失败（错误 %lu）", (unsigned long)GetLastError());
                    PostEvt(kEvtError, msg);
                }
                // §102（审查 P3-1）：**写失败不自我唤醒**——回等待循环静候下一次 UI 写入触发；
                // 旧 §101 无条件续取下一块，端口持续失败时会造成刷屏式错误通知风暴
                bool more = false;
                if (okWrite) {
                    std::lock_guard<std::mutex> lk(mtx_);
                    more = !wq_.empty();
                }
                if (more) SetEvent(wakeEvt_);              // 回等待循环再取下一块（=给读让路）
            }
        }
    }

    // 退出前若还有 pending 读，让 CancelIoEx 已标记完成的事件复位（句柄随后关闭）
    if (hComm_ != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        GetOverlappedResult(hComm_, &ovRd, &n, FALSE);
    }
    CloseHandle(ovRd.hEvent);
    CloseHandle(ovWr.hEvent);
    PostSt(false);             // 所有退出路径统一发关闭态
}

} // namespace sncom
