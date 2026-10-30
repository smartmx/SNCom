// tools/nettest.cpp —— M4 NetEngine 无 GUI 自测：TCP 服务端↔客户端对打 + UDP 自发自收
// 两个引擎实例共用一个 message-only 窗口：kMsgNetSock 按 SOCKET 归属分发（引擎内部
// 对陌生套接字静默忽略，转发给两个实例即可）；Rx/Evt/St 按会话代号区分。
// 构建：见仓库根 build_nettest 命令（cl nettest.cpp net.cpp /link ws2_32 user32）
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/platform/net.h"

using namespace sncom;

// 三个引擎各挂一个 message-only 窗口（GWLP_USERDATA=0/1/2）：
// 会话代号在各引擎独立计数，同一窗口收多引擎消息会撞代号——按窗口归属分拣才可靠。
struct Rec {
    bool st = false;
    std::wstring lastEvt;
    std::vector<std::vector<uint8_t>> rx;
};
static NetEngine* g_engines[3] = { nullptr, nullptr, nullptr };
static Rec g_recs[3];                  // 0=srv 1=cli 2=udp
static Rec& Srv() { return g_recs[0]; }
static Rec& Cli() { return g_recs[1]; }
static Rec& Udp() { return g_recs[2]; }

static void TakeRx(int slot, WPARAM wp, LPARAM lp) {
    size_t n = wp & 0xFFFF;
    uint8_t* d = (uint8_t*)lp;
    if (!d) return;
    std::vector<uint8_t> chunk(d, d + n);
    delete[] d;
    g_recs[slot].rx.push_back(chunk);
}

static void TakeEvt(int slot, WPARAM wp, LPARAM lp) {
    wchar_t* t = (wchar_t*)lp;
    if (!t) return;
    g_recs[slot].lastEvt = t;
    delete[] t;
}

static void TakeSt(int slot, WPARAM wp) {
    g_recs[slot].st = (wp & 1) != 0;
}

static LRESULT CALLBACK TestProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int slot = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == kMsgNetRx) { TakeRx(slot, wp, lp); return 0; }
    if (msg == kMsgNetEvt) { TakeEvt(slot, wp, lp); return 0; }
    if (msg == kMsgNetSt) { TakeSt(slot, wp); return 0; }
    if (msg == kMsgNetSock) {                      // 只转发本窗口的引擎
        if (g_engines[slot]) g_engines[slot]->OnSockMsg(wp, lp);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void Pump(DWORD budgetMs) {
    // 跑消息循环直到预算耗尽（PeekMessage 轮询，防无消息时永久阻塞）
    DWORD deadline = GetTickCount() + budgetMs;
    for (;;) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() >= deadline) return;
        Sleep(2);
    }
}

template <typename Cond>
static void PumpUntil(DWORD budgetMs, Cond cond) {
    // PeekMessage 轮询（GetMessageW 在无消息时会无视预算永久阻塞）
    DWORD deadline = GetTickCount() + budgetMs;
    for (;;) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (cond()) return;
        }
        if (cond()) return;
        if (GetTickCount() >= deadline) return;
        Sleep(2);
    }
}

static bool SrvConnected() { return Srv().st && !Cli().rx.empty(); }
static bool CliConnected() { return Cli().st; }

static bool EqBytes(const std::vector<uint8_t>& a, const char* s) {
    std::vector<uint8_t> b(s, s + strlen(s));
    return a == b;
}

static int g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) wprintf(L"[PASS] %s\n", name); \
    else { wprintf(L"[FAIL] %s\n", name); ++g_fail; } } while (0)

static HWND MakeTestWindow(int slot) {
    HWND h = CreateWindowExW(0, L"SNComNetTest", L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)slot);
    return h;
}

int wmain() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    WNDCLASSW wc{};
    wc.lpfnWndProc = TestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SNComNetTest";
    RegisterClassW(&wc);
    HWND hs = MakeTestWindow(0), hc = MakeTestWindow(1), hu = MakeTestWindow(2);
    if (!hs || !hc || !hu) { wprintf(L"[FAIL] message-only 窗口创建失败\n"); return 1; }

    NetEngine srv, cli, udp;
    g_engines[0] = &srv; g_engines[1] = &cli; g_engines[2] = &udp;

    // ---- TCP 服务端侦听 ----
    NetParams sp;
    sp.mode = kNetTcpServer;
    sp.local_ip = L"127.0.0.1";
    sp.local_port = 47777;
    CHECK(srv.Open(hs, sp), L"服务端 Open（127.0.0.1:47777）同步成功");
    PumpUntil(3000, [] { return Srv().st; });
    CHECK(Srv().st, L"服务端 St(1) 侦听中");

    // ---- TCP 客户端连接 ----
    NetParams cp;
    cp.mode = kNetTcpClient;
    cp.remote_ip = L"127.0.0.1";
    cp.remote_port = 47777;
    CHECK(cli.Open(hc, cp), L"客户端 Open 同步成功");
    PumpUntil(3000, CliConnected);
    CHECK(Cli().st, L"客户端 FD_CONNECT → St(1) 已连接");
    PumpUntil(3000, SrvConnected);
    CHECK(Srv().lastEvt.find(L"客户端接入") != std::wstring::npos, L"服务端报客户端接入");
    CHECK(srv.ClientCount() == 1, L"服务端客户端计数=1");

    // ---- 客户端 → 服务端 ----
    std::vector<uint8_t> ping{'P', 'I', 'N', 'G'};
    CHECK(cli.Write(ping.data(), ping.size()) == 4, L"客户端 Write 接受 4 字节");
    PumpUntil(3000, [] { return !Srv().rx.empty(); });
    CHECK(!Srv().rx.empty() && EqBytes(Srv().rx.back(), "PING"), L"服务端收到 PING");

    // ---- 服务端 → 客户端（广播） ----
    std::vector<uint8_t> pong{'P', 'O', 'N', 'G'};
    CHECK(srv.Write(pong.data(), pong.size()) == 4, L"服务端广播 Write 接受 4 字节");
    PumpUntil(3000, [] { return !Cli().rx.empty(); });
    CHECK(!Cli().rx.empty() && EqBytes(Cli().rx.back(), "PONG"), L"客户端收到 PONG");

    // ---- 客户端断开 → 服务端通知 ----
    cli.Close();
    PumpUntil(3000, [&] { return srv.ClientCount() == 0; });
    Pump(300);                                   // 计数同步减，断开通知是异步 post：先泵进事件
    CHECK(srv.ClientCount() == 0, L"服务端客户端计数归零");
    CHECK(Srv().lastEvt.find(L"已断开") != std::wstring::npos, L"服务端报客户端断开");

    // ---- UDP 自发自收（绑 0.0.0.0:47778，发 127.0.0.1:47778） ----
    NetParams up;
    up.mode = kNetUdp;
    up.local_ip = L"0.0.0.0";
    up.local_port = 47778;
    up.remote_ip = L"127.0.0.1";
    up.remote_port = 47778;
    CHECK(udp.Open(hu, up), L"UDP Open 同步成功");
    PumpUntil(3000, [] { return Udp().st; });
    CHECK(Udp().st, L"UDP St(1) 已绑定");
    std::vector<uint8_t> dg{'U', 'D', 'P', '!', '0', '1'};
    CHECK(udp.Write(dg.data(), dg.size()) == 6, L"UDP Write 接受 6 字节");
    PumpUntil(3000, [] { return !Udp().rx.empty(); });
    CHECK(!Udp().rx.empty() && Udp().rx.back() == dg, L"UDP 自收数据报一致");

    // ---- UDP 远程不可解析 → 拒收 0（去重报错一次） ----
    NetParams up2 = up;
    up2.remote_ip = L"not a host";
    CHECK(udp.Open(hu, up2), L"UDP 重开（错误远程地址）");
    Pump(200);
    Udp().rx.clear();
    CHECK(udp.Write(dg.data(), dg.size()) == 0, L"UDP 远程不可解析 → Write 返回 0");
    Pump(300);                                   // 错误事件异步 post：泵进后再断言
    CHECK(Udp().lastEvt.find(L"无法解析") != std::wstring::npos, L"UDP 报解析失败事件");

    // ---- G2-2：重开立即收发（Close→Open 同端口复用，Reset drain 迟到事件后新会话纯净） ----
    NetParams sp2;
    sp2.mode = kNetTcpServer;
    sp2.local_ip = L"127.0.0.1";
    sp2.local_port = 47777;
    Srv().st = false; Srv().rx.clear(); Srv().lastEvt.clear();
    CHECK(srv.Open(hs, sp2), L"服务端重开（同端口，验证迟到事件排空）");
    PumpUntil(3000, [] { return Srv().st; });
    CHECK(Srv().st, L"服务端重开 St(1) 侦听中");
    NetParams cp2;
    cp2.mode = kNetTcpClient;
    cp2.remote_ip = L"127.0.0.1";
    cp2.remote_port = 47777;
    Cli().st = false; Cli().rx.clear(); Cli().lastEvt.clear();
    CHECK(cli.Open(hc, cp2), L"客户端重开");
    PumpUntil(3000, CliConnected);
    CHECK(Cli().st, L"客户端重开已连接");
    {
        std::vector<uint8_t> ping2{'A', 'G', 'A', 'I', 'N'};
        CHECK(cli.Write(ping2.data(), ping2.size()) == 5, L"重开会话 Write 5 字节");
        PumpUntil(3000, [] { return !Srv().rx.empty(); });
        CHECK(!Srv().rx.empty() && Srv().rx.back() == ping2, L"重开会话服务端收到 AGAIN");
    }

    // ---- 陈旧会话代号过滤：Close 后迟到消息不得入新会话（此处以 gen 单调验证） ----
    int genBefore = srv.Generation();
    srv.Close();
    CHECK(srv.Generation() == genBefore + 1, L"Close 会话代号自增");

    srv.Close(); cli.Close(); udp.Close();
    g_engines[0] = g_engines[1] = g_engines[2] = nullptr;

    wprintf(L"\n%s（失败 %d 项）\n", g_fail == 0 ? L"全部通过" : L"存在失败", g_fail);
    WSACleanup();
    return g_fail == 0 ? 0 : 1;
}
