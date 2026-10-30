// sncom/src/platform/net.h —— M4 网络引擎（TCP 客户端 / TCP 服务端多客户端 / UDP）
// 线程模型与 PyCom net_link.py 同款：主线程事件驱动——WSAAsyncSelect 把套接字事件
// 投递到主窗口，不开线程。引擎收到 kMsgNetSock 后按会话代号 post 三条业务消息：
//   kMsgNetRx  : wp=((gen<<16)|字节数≤0xFFFF), lp=new uint8_t[]（接收方 delete[]）
//   kMsgNetEvt : wp=((gen<<8)|事件码),      lp=new wchar_t[] 文本（接收方 delete[]）
//   kMsgNetSt  : wp=((gen<<8)|(1=open/0=closed))
// 会话代号口径与 SerialEngine 一致：Open/Close 各自增，UI 侧用 Generation() 过滤
// 陈旧消息（closesocket 后队列里迟到的 Rx/St 不得喂进新会话）。
//
// IPv4/IPv6 纯单栈（PyCom 明确不做双栈）：绑定地址的族决定收发族——"0.0.0.0"=仅
// IPv4 全部、"::"=仅 IPv6 全部（显式 IPV6_V6ONLY=1，防默认双栈）、具体地址=仅该
// 地址（fe80::x%N 数字作用域直接可用）。TCP 客户端仅"具体网卡地址"才预绑定
// （PyCom 二十轮 F1：预绑定回环会钉死源地址，连局域网设备报 unreachable）。
#pragma once
#include <winsock2.h>                // 必须先于 windows.h（build.bat 全局 WIN32_LEAN_AND_MEAN 已隔离 winsock1）
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sncom {

const UINT kMsgNetRx   = WM_APP + 0x200;   // 引擎 → UI：收到数据
const UINT kMsgNetEvt  = WM_APP + 0x201;   // 引擎 → UI：事件（错误/信息）
const UINT kMsgNetSt   = WM_APP + 0x202;   // 引擎 → UI：链路状态
const UINT kMsgNetSock = WM_APP + 0x203;   // WSAAsyncSelect 直达主窗，UI 原样转发 OnSockMsg

enum {
    kNetTcpClient = 1,
    kNetTcpServer = 2,
    kNetUdp       = 3,
};

enum {
    kNetEvtError = 1,     // 链路错误（UI 亮 ERR 灯 + 通知）
    kNetEvtInfo  = 2,     // 信息（客户端接入/断开、发送恢复）
    kNetEvtUdpPeer = 3,   // §77：UDP 收到数据包的来源（文本 "ip|port"）——UI 更新远程地址（SSCOM V5.13）
};

struct NetParams {
    int mode = kNetTcpClient;
    std::wstring remote_ip;          // 客户端/UDP 目标；支持主机名（仅 TCP 客户端，PyCom 口径）
    int remote_port = 8080;
    std::wstring local_ip;           // "0.0.0.0" / "::" / 具体地址（含 %scope）
    int local_port = 777;
    bool bind_client = true;         // tcp_client 是否尝试预绑定（引擎内部排除 全部档/回环/解析失败）
};

class NetEngine {
public:
    NetEngine() = default;
    ~NetEngine();
    NetEngine(const NetEngine&) = delete;
    NetEngine& operator=(const NetEngine&) = delete;

    // 建立会话。同步步骤（解析/建套接字/绑定/首连发起）失败返回 false（会话未建立，
    // 不 post 任何消息，UI 自行提示）；异步结果经 kMsgNetSt/Evt 通知。
    bool Open(HWND notify, const NetParams& p);
    void Close();                    // 幂等。调用方随后自行走 closed 状态（与 CloseSerial 同款）
    bool IsOpen() const;             // client=已连接 server=侦听中 udp=已绑定
    int  Generation() const { return gen_; }
    int  ClientCount() const { return (int)clients_.size(); }
    bool SendErrActive() const { return udpSendErr_; }   // UDP 发送错误去重期内（UI 免叠加通用提示）

    // 把数据交给引擎：直接尝试发出，余量进内部待发（FD_WRITE 续发）。
    // 返回"接受的字节数"（0=链路不可写/无客户端/发送失败，UI 据此入待发队列）。
    int  Write(const uint8_t* d, size_t n);

    void OnSockMsg(WPARAM wp, LPARAM lp);   // kMsgNetSock 转发入口（主线程）

private:
    struct Client {
        SOCKET s = INVALID_SOCKET;
        std::wstring peer;                    // "ip:port"（WSAAddressToStringW）
        std::vector<uint8_t> pend;            // 该客户端待发余量（慢客户端不拖累别人）
    };

    void Reset();                             // 关全部套接字（不动 gen_）
    void PostRx(const uint8_t* d, size_t n);
    void PostEvt(int code, const wchar_t* text);
    void PostSt(bool open);

    bool StartClient(const NetParams& p);     // 解析 + 首连（返回 false=同步失败）
    bool NewClientSocket();                   // 为 targets_[targetIdx_] 建套接字+绑+挂事件
    bool TryConnect();                        // 发起非阻塞连接，失败自动换下一个目标
    bool NextTarget();                        // FD_CONNECT 失败 → 下一个解析结果
    bool ListenOn(const NetParams& p);
    bool BindUdp(const NetParams& p);
    bool SetupSocket(SOCKET s, int events);   // WSAAsyncSelect 包装
    void HandleAccept();
    void HandleRead(SOCKET s);                // TCP（客户端或服务端子连接）
    void HandleReadUdp();
    void HandleClientClose(SOCKET s);
    void FlushClientPend(Client& c);
    void FlushOutPend();                      // 客户端/UDP 共用余量（UDP 走 sendto）
    int  SendUdpTo(const uint8_t* d, size_t n, const sockaddr* sa, int len, int family);
    int  WriteServer(const uint8_t* d, size_t n);

    HWND   notify_ = nullptr;
    NetParams p_{};
    int    gen_ = 0;                          // 会话代号（Open/Close 各自增）
    SOCKET sock_ = INVALID_SOCKET;            // 客户端连接 / 侦听 / UDP 套接字
    bool   listening_ = false;                // 服务端侦听中
    bool   bound_ = false;                    // UDP 已绑定
    bool   connected_ = false;                // TCP 客户端已连上（FD_CONNECT 成功后）
    std::vector<Client> clients_;             // 服务端子连接
    std::vector<uint8_t> outPend_;            // 客户端/UDP 发送余量（FD_WRITE 续发）
    // TCP 客户端：解析结果队列（主机名多地址回退，← PyCom "主机名保留多地址回退"）
    std::vector<sockaddr_storage> targets_;
    std::vector<int> targetFams_;
    size_t targetIdx_ = 0;
    bool   clientBind_ = false;               // 本会话是否预绑定过（换目标族时跳过重绑）
    sockaddr_storage clientBindAddr_{};
    bool   udpSendErr_ = false;               // UDP 发送失败去重（定时发送不刷屏，← PyCom F3）
    std::wstring lastPeer_;                   // §77：上次上报的 UDP 来源 "ip|port"（去重，Open 时清）
};

} // namespace sncom
