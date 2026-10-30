// sncom/src/platform/net.cpp —— M4 网络引擎实现（主线程事件驱动，无线程）
#include "net.h"

#include <ws2ipdef.h>
#include <string>
#include <cstring>
#include <strsafe.h>                 // §100：StringCchCopyW 取代弃用的 wcscpy（消 C4996）

namespace sncom {

static const size_t kClientPendCap = 256 * 1024;   // 单客户端待发上限（← PyCom _pending_max）
static const size_t kOutPendCap    = 1u << 20;     // 客户端/UDP 发送余量上限（超限改让 UI 侧队列接管）
static const size_t kMaxClients    = 64;           // §99（审查 U7）：服务端并发客户端上限——
                                                   // 无上限时可被本机/局域网连接耗尽句柄与内存

// ---------------- 地址工具 ----------------

// 本地绑定地址解析：单栈语义（PyCom _bindable_local / QHostAddress 口径）
struct IpSpec {
    int family = AF_INET;
    sockaddr_storage sa{};
    bool any = false;        // 0.0.0.0 / ::
    bool loopback = false;   // 127.x.x.x / ::1
};

static bool ParseIpLiteral(const std::wstring& in, IpSpec& out) {
    std::wstring s = in;
    // 宽容：去空格、去 IPv6 方括号（PyCom _strip_brackets）
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t')) --e;
    s = s.substr(b, e - b);
    if (s.empty()) return false;
    if (s.size() >= 2 && s.front() == L'[' && s.back() == L']') s = s.substr(1, s.size() - 2);
    if (s == L"0.0.0.0") {
        out = IpSpec{}; out.family = AF_INET; out.any = true;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY;
        out.sa = *(sockaddr_storage*)&a;
        return true;
    }
    if (s == L"::") {
        out = IpSpec{}; out.family = AF_INET6; out.any = true;
        sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any;
        out.sa = *(sockaddr_storage*)&a;
        return true;
    }
    if (s.rfind(L"127.", 0) == 0) out.loopback = true;
    if (s == L"::1") out.loopback = true;
    // IPv4 字面量
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        if (InetPtonW(AF_INET, s.c_str(), &a.sin_addr) == 1) {
            out = IpSpec{};
            out.family = AF_INET;
            out.sa = *(sockaddr_storage*)&a;
            return true;
        }
    }
    // IPv6 字面量（%数字作用域：本机枚举给的都是数字，命名作用域不支持）
    {
        std::wstring addr = s, scope;
        size_t pct = s.find(L'%');
        if (pct != std::wstring::npos) {
            addr = s.substr(0, pct);
            scope = s.substr(pct + 1);
        }
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        if (InetPtonW(AF_INET6, addr.c_str(), &a.sin6_addr) == 1) {
            if (!scope.empty()) a.sin6_scope_id = (ULONG)_wtoi(scope.c_str());   // 非数字=0
            out = IpSpec{};
            out.family = AF_INET6;
            out.sa = *(sockaddr_storage*)&a;
            return true;
        }
    }
    return false;
}

static void SetV6Only(SOCKET s) {
    // "::" 单栈（PyCom 明确不做双栈）：Windows 默认即 V6ONLY=1，这里显式钉死防环境差异
    DWORD v = 1;
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v, sizeof(v));
}

static std::wstring PeerToString(const sockaddr* sa, int len) {
    wchar_t buf[128] = L"";
    DWORD n = 128;
    if (WSAAddressToStringW((LPSOCKADDR)sa, len, nullptr, buf, &n) == 0) return buf;
    return L"?";
}

// ---------------- 生命周期 ----------------

NetEngine::~NetEngine() { Reset(); }

void NetEngine::Reset() {
    // G2-2：先排空队列里迟到的 FD_* 事件消息再关套接字——closesocket 后 Winsock 已
    // 投递的 kMsgNetSock 仍会到达，若其 wp（套接字句柄是小整数，会被系统复用）恰好
    // 撞上**新会话**的套接字，就会产生伪连接/伪断开。按窗口过滤 + 消息号区间移除
    // （本引擎独占 notify_ 窗口的 kMsgNetSock；多实例各自持有独立 message-only 窗口）。
    if (notify_) {
        MSG msg{};
        while (PeekMessageW(&msg, notify_, kMsgNetSock, kMsgNetSock, PM_REMOVE)) {}
    }
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
    for (Client& c : clients_) if (c.s != INVALID_SOCKET) closesocket(c.s);
    clients_.clear();
    outPend_.clear();
    listening_ = bound_ = connected_ = false;
    udpSendErr_ = false;
    lastPeer_.clear();                           // §77：换会话后来源去重重新计
    targets_.clear();
    targetFams_.clear();
    targetIdx_ = 0;
}

bool NetEngine::Open(HWND notify, const NetParams& p) {
    Reset();
    ++gen_;
    notify_ = notify;
    p_ = p;
    bool ok = false;
    switch (p.mode) {
        case kNetTcpClient: ok = StartClient(p); break;
        case kNetTcpServer: ok = ListenOn(p);    break;
        case kNetUdp:       ok = BindUdp(p);     break;
    }
    if (!ok) { Reset(); return false; }
    return true;
}

void NetEngine::Close() {
    Reset();
    ++gen_;                                  // 作废旧会话迟到的 Rx/Evt/St
}

bool NetEngine::IsOpen() const {
    switch (p_.mode) {
        case kNetTcpClient: return connected_;
        case kNetTcpServer: return listening_;
        case kNetUdp:       return bound_;
    }
    return false;
}

// ---------------- 打开路径 ----------------

bool NetEngine::SetupSocket(SOCKET s, int events) {
    return WSAAsyncSelect(s, notify_, kMsgNetSock, events) == 0;
}

static void TrimAndStripBrackets(std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t')) --e;
    s = s.substr(b, e - b);
    if (s.size() >= 2 && s.front() == L'[' && s.back() == L']') s = s.substr(1, s.size() - 2);
}

bool NetEngine::StartClient(const NetParams& p) {
    // 主机名/字面量统一走 GetAddrInfoW（PyCom connectToHost 口径：多地址回退）。
    // 同步解析——DNS 不可达时会短暂卡 UI（SSCOM 原版同款，docs/00 §19 登记）。
    std::wstring host = p.remote_ip;
    TrimAndStripBrackets(host);
    if (host.empty()) return false;
    wchar_t svc[16];
    swprintf(svc, 16, L"%d", p.remote_port);
    addrinfoW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfoW* res = nullptr;
    if (GetAddrInfoW(host.c_str(), svc, &hints, &res) != 0 || !res) return false;

    for (addrinfoW* ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_addrlen > sizeof(sockaddr_storage)) continue;
        sockaddr_storage ss{};
        memcpy(&ss, ai->ai_addr, ai->ai_addrlen);
        targets_.push_back(ss);
        targetFams_.push_back(ai->ai_family);
    }
    FreeAddrInfoW(res);
    if (targets_.empty()) return false;

    // 预绑定决策（PyCom _bindable_local）：仅"具体网卡地址"——全部档/回环/解析失败不绑
    IpSpec local{};
    clientBind_ = p.bind_client && ParseIpLiteral(p.local_ip, local) && !local.any && !local.loopback;
    if (clientBind_) clientBindAddr_ = local.sa;

    targetIdx_ = 0;
    if (!NewClientSocket()) return false;
    return TryConnect();                     // 首连发起（异步结果走 FD_CONNECT）
}

bool NetEngine::NewClientSocket() {
    // 为 targets_[targetIdx_] 建套接字 + 预绑定（族匹配才绑）+ 挂事件
    sock_ = socket(targetFams_[targetIdx_], SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    if (clientBind_) {
        int fam = targetFams_[targetIdx_];
        if ((clientBindAddr_.ss_family == AF_INET6) == (fam == AF_INET6)) {
            bind(sock_, (sockaddr*)&clientBindAddr_,
                 fam == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in));
        }
    }
    if (!SetupSocket(sock_, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE)) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool NetEngine::TryConnect() {
    // 对当前 sock_/targetIdx_ 发起非阻塞连接：0=立即连上（本机回环常见）；
    // WSAEWOULDBLOCK=等 FD_CONNECT；其他错误 → 换下一个解析结果，全部失败 post Evt/St
    for (;;) {
        int len = targetFams_[targetIdx_] == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
        int r = connect(sock_, (sockaddr*)&targets_[targetIdx_], len);
        if (r == 0) { connected_ = true; PostSt(true); return true; }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) return true;
        if (!NextTarget()) return false;     // 已 post Evt/St（会话保持，UI 走重连）
    }
}

bool NetEngine::NextTarget() {
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
    connected_ = false;
    size_t next = targetIdx_ + 1;
    if (next >= targets_.size()) {
        wchar_t msg[128];
        swprintf(msg, 128, L"TCP 连接失败（已尝试 %d 个地址）", (int)targets_.size());
        PostEvt(kNetEvtError, msg);
        PostSt(false);
        return false;
    }
    targetIdx_ = next;
    if (!NewClientSocket()) {
        PostEvt(kNetEvtError, L"创建套接字失败");
        PostSt(false);
        return false;
    }
    return true;                             // 调用方接着 TryConnect()
}

bool NetEngine::ListenOn(const NetParams& p) {
    IpSpec local{};
    if (!ParseIpLiteral(p.local_ip.empty() ? L"0.0.0.0" : p.local_ip, local)) return false;
    sock_ = socket(local.family, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    if (local.family == AF_INET6) SetV6Only(sock_);
    sockaddr* sa = (sockaddr*)&local.sa;
    int len = local.family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (local.family == AF_INET) ((sockaddr_in*)sa)->sin_port = htons((u_short)p.local_port);
    else ((sockaddr_in6*)sa)->sin6_port = htons((u_short)p.local_port);
    if (bind(sock_, sa, len) != 0 || listen(sock_, 8) != 0) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }
    if (!SetupSocket(sock_, FD_ACCEPT)) {
        closesocket(sock_); sock_ = INVALID_SOCKET;
        return false;
    }
    listening_ = true;
    PostSt(true);
    return true;
}

bool NetEngine::BindUdp(const NetParams& p) {
    IpSpec local{};
    if (!ParseIpLiteral(p.local_ip.empty() ? L"0.0.0.0" : p.local_ip, local)) return false;
    sock_ = socket(local.family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;
    if (local.family == AF_INET6) SetV6Only(sock_);
    sockaddr* sa = (sockaddr*)&local.sa;
    int len = local.family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (local.family == AF_INET) ((sockaddr_in*)sa)->sin_port = htons((u_short)p.local_port);
    else ((sockaddr_in6*)sa)->sin6_port = htons((u_short)p.local_port);
    if (bind(sock_, sa, len) != 0) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }
    if (!SetupSocket(sock_, FD_READ | FD_WRITE)) {
        closesocket(sock_); sock_ = INVALID_SOCKET;
        return false;
    }
    bound_ = true;
    PostSt(true);
    return true;
}

// ---------------- 写 ----------------

int NetEngine::Write(const uint8_t* d, size_t n) {
    if (!d || n == 0) return 0;
    switch (p_.mode) {
        case kNetTcpClient: {
            if (!connected_) return 0;
            // 已有未送出的余量：新数据必须追在队尾（否则先发新数据
            // 会与队列里的旧余量倒序），并防止 assign 把旧余量整块覆盖丢掉
            // （审查 P2-5：背压场景，例如向慢对端发文件，会静默丢数据）
            if (!outPend_.empty()) {
                if (outPend_.size() + n > kOutPendCap) return 0;   // 交回 UI：让上层队列接管
                outPend_.insert(outPend_.end(), d, d + n);
                return (int)n;                       // 已接受（TX 口径：接受即计数）
            }
            size_t sent = 0;
            while (sent < n) {
                int r = send(sock_, (const char*)d + sent, (int)(n - sent), 0);
                if (r > 0) { sent += (size_t)r; continue; }
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    outPend_.assign(d + sent, d + n);    // 首次阻塞（队列必空）：余量入待发，FD_WRITE 续发
                    return (int)n;                       // 已接受
                }
                return (int)sent;                        // 真错误：返回已发出量（UI 侧提示）
            }
            return (int)n;
        }
        case kNetTcpServer:
            return WriteServer(d, n);
        case kNetUdp: {
            if (!bound_) return 0;
            if (!outPend_.empty()) {                       // same order guarantee as TCP client
                if (outPend_.size() + n > kOutPendCap) return 0;
                outPend_.insert(outPend_.end(), d, d + n);
                return (int)n;
            }
            IpSpec dst{};
            if (!ParseIpLiteral(p_.remote_ip, dst) || dst.any) {
                if (!udpSendErr_) {
                    udpSendErr_ = true;
                    PostEvt(kNetEvtError, L"远程IP无法解析（UDP 只认地址字面量）");
                }
                return 0;
            }
            int fam = dst.family;
            int len = fam == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
            int r = SendUdpTo(d, n, (sockaddr*)&dst.sa, len, fam);
            if (r == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) { outPend_.assign(d, d + n); return (int)n; }
                // 常见于族不匹配（本地绑 :: 只收发 IPv6，远程却是 IPv4 地址）
                if (!udpSendErr_) {
                    udpSendErr_ = true;
                    wchar_t msg[128];
                    swprintf(msg, 128, L"UDP 发送失败（Winsock 错误 %d，多为本地/远程族不匹配）", err);
                    PostEvt(kNetEvtError, msg);
                }
                return 0;
            }
            if (udpSendErr_) {                           // 发送恢复 → 回报 open（UI 清 ERR，← PyCom）
                udpSendErr_ = false;
                PostSt(true);
                PostEvt(kNetEvtInfo, L"UDP 发送已恢复");
            }
            return (int)n;
        }
    }
    return 0;
}

int NetEngine::SendUdpTo(const uint8_t* d, size_t n, const sockaddr* sa, int len, int family) {
    if (family == AF_INET) ((sockaddr_in*)sa)->sin_port = htons((u_short)p_.remote_port);
    else ((sockaddr_in6*)sa)->sin6_port = htons((u_short)p_.remote_port);
    return sendto(sock_, (const char*)d, (int)n, 0, sa, len);
}

int NetEngine::WriteServer(const uint8_t* d, size_t n) {
    if (!listening_ || clients_.empty()) return 0;
    // 事务式容量判定（PyCom 同款）：任一客户端"余量+数据"超限 → 整体拒绝不产生部分广播
    for (const Client& c : clients_)
        if (c.pend.size() + n > kClientPendCap) return 0;
    for (Client& c : clients_) {
        if (!c.pend.empty()) {                       // 先排旧余量
            int r = send(c.s, (const char*)c.pend.data(), (int)c.pend.size(), 0);
            if (r > 0) c.pend.erase(c.pend.begin(), c.pend.begin() + r);
        }
        if (!c.pend.empty()) { c.pend.insert(c.pend.end(), d, d + n); continue; }   // 旧余没排完：新数据直接排队
        size_t sent = 0;
        while (sent < n) {
            int r = send(c.s, (const char*)d + sent, (int)(n - sent), 0);
            if (r > 0) { sent += (size_t)r; continue; }
            break;                                   // WSAEWOULDBLOCK 或错误：余量入该客户端队列
        }
        if (sent < n) c.pend.insert(c.pend.end(), d + sent, d + n);
    }
    return (int)n;
}

// ---------------- 套接字事件（kMsgNetSock，主线程） ----------------

void NetEngine::OnSockMsg(WPARAM wp, LPARAM lp) {
    SOCKET s = (SOCKET)wp;
    int ev = WSAGETSELECTEVENT(lp);
    int err = WSAGETSELECTERROR(lp);
    (void)err;
    if (s == sock_) {
        switch (p_.mode) {
            case kNetTcpClient:
                if (ev == FD_CONNECT) {
                    if (err == 0) { connected_ = true; PostSt(true); }
                    else if (NextTarget()) TryConnect();   // 失败已 post Evt/St；成功则连下一个
                }
                else if (ev == FD_READ) HandleRead(s);
                else if (ev == FD_WRITE) FlushOutPend();
                else if (ev == FD_CLOSE) {
                    if (connected_) {                      // recv 路径可能已报过断开
                        connected_ = false;
                        PostEvt(kNetEvtError, L"TCP 连接已断开");
                        PostSt(false);
                    }
                }
                break;
            case kNetTcpServer:
                if (ev == FD_ACCEPT) HandleAccept();
                break;
            case kNetUdp:
                if (ev == FD_READ) HandleReadUdp();
                else if (ev == FD_WRITE) FlushOutPend();
                break;
        }
        return;
    }
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (clients_[i].s != s) continue;
        if (ev == FD_READ) HandleRead(s);
        else if (ev == FD_WRITE) FlushClientPend(clients_[i]);
        else if (ev == FD_CLOSE) HandleClientClose(s);
        return;
    }
    // 未知套接字（已关闭的迟到事件）：忽略
}

void NetEngine::HandleAccept() {
    for (;;) {
        sockaddr_storage peer{};
        int plen = sizeof(peer);
        SOCKET ns = accept(sock_, (sockaddr*)&peer, &plen);
        if (ns == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return;
            if (err == WSAECONNRESET) continue;      // 对端在 accept 前重置：继续收下一个
            return;
        }
        Client c;
        c.s = ns;
        c.peer = PeerToString((sockaddr*)&peer, plen);
        if (clients_.size() >= kMaxClients) {        // §99：到上限拒收并提示
            closesocket(ns);
            wchar_t msg[128];
            swprintf(msg, 128, L"客户端数已达上限（%d），拒绝新接入", (int)kMaxClients);
            PostEvt(kNetEvtInfo, msg);
            continue;
        }
        if (!SetupSocket(ns, FD_READ | FD_WRITE | FD_CLOSE)) { closesocket(ns); continue; }
        clients_.push_back(c);
        wchar_t msg[192];
        swprintf(msg, 192, L"客户端接入 %ls（共 %d 个）", c.peer.c_str(), (int)clients_.size());
        PostEvt(kNetEvtInfo, msg);
    }
}

void NetEngine::HandleRead(SOCKET s) {
    // 非阻塞读循环：读空/对端关闭即止；每次 recv 直接 post（与串口引擎同款）
    uint8_t tmp[32768];
    for (;;) {
        int r = recv(s, (char*)tmp, sizeof(tmp), 0);
        if (r > 0) { PostRx(tmp, (size_t)r); continue; }
        if (r == 0) {                                // 对端优雅关闭
            if (s == sock_ && p_.mode == kNetTcpClient) {
                if (connected_) {
                    connected_ = false;
                    PostEvt(kNetEvtError, L"TCP 连接已断开");
                    PostSt(false);
                }
            } else {
                HandleClientClose(s);
            }
            return;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) break;
        // 真错误：子连接按断开清理；主客户端报错
        if (s == sock_ && p_.mode == kNetTcpClient) {
            if (connected_) {
                connected_ = false;
                wchar_t msg[96];
                swprintf(msg, 96, L"TCP 接收错误（Winsock %d）", err);
                PostEvt(kNetEvtError, msg);
                PostSt(false);
            }
        } else {
            HandleClientClose(s);
        }
        return;
    }
}

void NetEngine::HandleReadUdp() {
    // 逐数据报读直到读空；每个数据报单独 post（保数据报边界，PyCom 逐包 emit 同款）
    uint8_t tmp[32768];
    for (;;) {
        sockaddr_storage from{};
        int flen = sizeof(from);
        int r = recvfrom(sock_, (char*)tmp, sizeof(tmp), 0, (sockaddr*)&from, &flen);
        if (r > 0) {
            // §77（SSCOM V5.13）：UDP 收到数据包 → 上报来源，UI 把「远程地址」改为该来源
            wchar_t ip[INET6_ADDRSTRLEN + 4]{};
            unsigned short port = 0;
            if (from.ss_family == AF_INET) {
                auto* sa = (sockaddr_in*)&from;
                InetNtopW(AF_INET, &sa->sin_addr, ip, INET6_ADDRSTRLEN);
                port = ntohs(sa->sin_port);
            } else if (from.ss_family == AF_INET6) {
                auto* sa6 = (sockaddr_in6*)&from;
                InetNtopW(AF_INET6, &sa6->sin6_addr, ip, INET6_ADDRSTRLEN);
                port = ntohs(sa6->sin6_port);
            }
            if (ip[0]) {
                wchar_t peer[INET6_ADDRSTRLEN + 16];
                swprintf(peer, INET6_ADDRSTRLEN + 16, L"%ls|%u", ip, (unsigned)port);
                if (lastPeer_ != peer) {
                    lastPeer_ = peer;
                    PostEvt(kNetEvtUdpPeer, peer);
                }
            }
            PostRx(tmp, (size_t)r);
            continue;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) break;
        return;                                      // 真错误：下个事件再说
    }
}

void NetEngine::FlushClientPend(Client& c) {
    while (!c.pend.empty()) {
        int r = send(c.s, (const char*)c.pend.data(), (int)c.pend.size(), 0);
        if (r > 0) { c.pend.erase(c.pend.begin(), c.pend.begin() + r); continue; }
        break;                                       // 暂满/错误：等下个 FD_WRITE
    }
}

void NetEngine::FlushOutPend() {
    // 客户端与 UDP 共用 outPend_：UDP 走 sendto（重解析目标），客户端走 send
    while (!outPend_.empty()) {
        int r;
        if (p_.mode == kNetUdp) {
            IpSpec dst{};
            if (!ParseIpLiteral(p_.remote_ip, dst) || dst.any) { outPend_.clear(); return; }
            int len = dst.family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
            r = SendUdpTo(outPend_.data(), outPend_.size(), (sockaddr*)&dst.sa, len, dst.family);
        } else {
            r = send(sock_, (const char*)outPend_.data(), (int)outPend_.size(), 0);
        }
        if (r > 0) { outPend_.erase(outPend_.begin(), outPend_.begin() + r); continue; }
        break;
    }
}

void NetEngine::HandleClientClose(SOCKET s) {
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (clients_[i].s != s) continue;
        std::wstring peer = clients_[i].peer;
        closesocket(s);
        clients_.erase(clients_.begin() + i);
        wchar_t msg[192];
        swprintf(msg, 192, L"客户端 %ls 已断开（余 %d 个）", peer.c_str(), (int)clients_.size());
        PostEvt(kNetEvtInfo, msg);
        return;
    }
}

// ---------------- 消息回传（与 SerialEngine 同款：new[] 所有权转移） ----------------

void NetEngine::PostRx(const uint8_t* d, size_t n) {
    if (!notify_ || n == 0 || n > 0xFFFF) return;    // 大小编号进 wp 低 16 位，单包上限 64K-1
    auto* copy = new uint8_t[n];
    memcpy(copy, d, n);
    PostMessageW(notify_, kMsgNetRx, (WPARAM)((gen_ << 16) | n), (LPARAM)copy);
}

void NetEngine::PostEvt(int code, const wchar_t* text) {
    if (!notify_) return;
    size_t len = wcslen(text) + 1;
    auto* copy = new wchar_t[len];
    StringCchCopyW(copy, len, text);
    PostMessageW(notify_, kMsgNetEvt, (WPARAM)((gen_ << 8) | code), (LPARAM)copy);
}

void NetEngine::PostSt(bool open) {
    if (!notify_) return;
    PostMessageW(notify_, kMsgNetSt, (WPARAM)((gen_ << 8) | (open ? 1 : 0)), 0);
}

} // namespace sncom
