// sncom/src/platform/serial.h —— M0.5 spike：串口引擎（overlapped I/O + 单 I/O 线程 + PostMessage）
// 验证目标（docs/05 §9 M0.5 ③）：线程模型 + 消息回传（对应 PyCom readyRead）。
// M2 补：热插拔通知（WM_DEVICECHANGE）、EV_ERR/BREAK、占用等待可取消、改波特率自动重开。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

namespace sncom {

    // PostMessage 到 notify 窗口（接收方负责释放内存）：
    //   kMsgSerialRx  : wp=字节数,  lp=new uint8_t[]（接收方 delete[]）
    //   kMsgSerialEvt : wp=事件码,  lp=new wchar_t[] 文本（接收方 delete[]）
    //   kMsgSerialSt  : wp=((会话代号<<8) | (1=已打开 / 0=已关闭))（I/O 线程所有退出路径末尾发出）
    //   kMsgSerialEvt : wp=((会话代号<<8) | 事件码)，lp=new wchar_t[] 文本
    // 会话代号：Open() 自增、Close() 再自增（作废旧会话）。UI 侧必须用 Generation() 过滤，
    // 否则「关闭→立刻重开」时旧会话迟到的 St(0) 会把新连接关掉（伪断线 + 待发被清）。
const UINT kMsgSerialRx  = WM_APP + 0x100;
const UINT kMsgSerialEvt = WM_APP + 0x101;
const UINT kMsgSerialSt  = WM_APP + 0x102;

enum {
    kEvtError    = 1,     // 出错（wp=kEvtError, 文本=错误描述）
    kEvtLoopback = 2,     // 软件回环模式提示
    kEvtBreak    = 3,     // §73：线路 BREAK（CE_BREAK，非致命——状态显示，不触发断线重连）
};

struct SerialParams {
    std::wstring port;              // "COM3"
    int baud = 115200;
    int databits = 8;
    int parity = 0;                 // 0无 1偶 2奇 3Mark 4Space
    int stopbits = 0;               // 0=1 1=1.5 2=2
    bool dtr = true, rts = false;
    bool loopback = false;          // true=不碰硬件，写入直接回投为接收（验证线程/消息/渲染链路）
};

class SerialEngine {
public:
    // SetupDi 枚举本机串口 → [(端口名, 设备描述)]（描述可空），按端口号排序
    // ← PyCom list_ports：[(portName, description or "")]
    static std::vector<std::pair<std::wstring, std::wstring>> EnumPorts();

    SerialEngine() = default;
    ~SerialEngine();
    SerialEngine(const SerialEngine&) = delete;
    SerialEngine& operator=(const SerialEngine&) = delete;

    bool Open(HWND notify, const SerialParams& p);     // 同步打开；失败返回 false（不启动线程）
    void Close();
    bool IsOpen() const { return thread_.joinable(); }
    int  Generation() const { return gen_.load(std::memory_order_relaxed); }   // 会话代号（过滤陈旧消息）
    int  Write(const uint8_t* d, size_t n);            // 入队（拷贝），I/O 线程发；返回接受字节数
                                                       // （0=引擎队列满拒收，交回 UI 待发队列，§99）

    // 以下三个从 UI 线程直接调用（与 I/O 线程 overlapped 并发安全）
    bool ModemStatus(DWORD& ms);                       // GetCommModemStatus（MS_DCD_ON 等）
    void SetBreak(bool on);                            // SetCommBreak / ClearCommBreak
    void SetDtrRts(bool dtr, bool rts);                // EscapeCommFunction 热改（← PyCom）

private:
    void ThreadProc();
    void PostRx(const uint8_t* d, size_t n);
    void PostEvt(int code, const wchar_t* text);
    void PostSt(bool open);                            // 带会话代号的状态消息

    std::thread thread_;
    std::mutex mtx_;
    std::deque<std::vector<uint8_t>> wq_;
    size_t wqBytes_ = 0;              // 写队列当前字节数（mtx_ 保护，§99 队列上限用）
    HANDLE stopEvt_ = nullptr;        // manual-reset
    HANDLE wakeEvt_ = nullptr;        // auto-reset（有写请求）
    HANDLE hComm_ = INVALID_HANDLE_VALUE;
    HWND notify_ = nullptr;
    SerialParams p_;
    std::atomic<int> gen_{0};         // 会话代号（Open/Close 各自增一次；§99 原子化）
    int threadGen_ = 0;               // I/O 线程所属会话（PostSt/PostEvt 用；线程启动前写定）
    std::atomic<bool> closing_{false};// true=本方主动关闭（CancelIoEx 的 ABORTED 不算断线；UI 写、
                                      // I/O 线程读，§99 原子化）
};

} // namespace sncom
