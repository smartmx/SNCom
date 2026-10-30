// _probe/pend_probe.cpp -- M4 NetEngine TCP-client pending queue (outPend_) integrity probe.
// Scenario: connect to a raw peer that does NOT recv, so the send window fills.
//   1st Write: 1 MiB  (kernel accepts part, remainder parked in engine outPend_)
//   2nd Write: 64 B   (no FD_WRITE processed yet -> engine still holds the remainder)
// Then drain the peer: total must be 1 MiB + 64 B.
// A short total proves the remainder was silently dropped by outPend_.assign().
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <vector>
#include "../src/platform/net.h"

using namespace sncom;

static NetEngine* g_eng = nullptr;
static bool g_stOpen = false;

static LRESULT CALLBACK WP(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kMsgNetRx) { uint8_t* d = (uint8_t*)lp; if (d) delete[] d; return 0; }
    if (m == kMsgNetEvt) { wchar_t* t = (wchar_t*)lp; if (t) delete[] t; return 0; }
    if (m == kMsgNetSt) { g_stOpen = (wp & 1) != 0; return 0; }
    if (m == kMsgNetSock) { if (g_eng) g_eng->OnSockMsg(wp, lp); return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

static void Pump(DWORD ms) {
    DWORD deadline = GetTickCount() + ms;
    for (;;) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (GetTickCount() >= deadline) return;
        Sleep(2);
    }
}

static SOCKET MakePeer(int port, SOCKET* outListen) {
    SOCKET lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    InetPtonW(AF_INET, L"127.0.0.1", &a.sin_addr);
    int rcvbuf = 8192;                       // small receive window -> early EWOULDBLOCK
    setsockopt(lst, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));
    if (bind(lst, (sockaddr*)&a, sizeof(a)) != 0) return INVALID_SOCKET;
    listen(lst, 4);
    *outListen = lst;
    return INVALID_SOCKET;
}

// how many bytes does a raw non-blocking send accept against a non-reading peer?
static void RawControl() {
    SOCKET lst = INVALID_SOCKET;
    if (MakePeer(47902, &lst) == INVALID_SOCKET && lst == INVALID_SOCKET) { printf("[control] setup failed\n"); return; }
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nb = 1;
    ioctlsocket(c, FIONBIO, &nb);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(47902);
    InetPtonW(AF_INET, L"127.0.0.1", &a.sin_addr);
    connect(c, (sockaddr*)&a, sizeof(a));
    Pump(200);
    SOCKET s = accept(lst, nullptr, nullptr);
    if (s == INVALID_SOCKET) { closesocket(c); closesocket(lst); return; }
    int rcv2 = 0, l2 = sizeof(rcv2);
    getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcv2, &l2);
    int snd2 = 0, l3 = sizeof(snd2);
    getsockopt(c, SOL_SOCKET, SO_SNDBUF, (char*)&snd2, &l3);
    std::vector<uint8_t> A(1u << 20, 0xAA);
    size_t sent = 0;
    while (sent < A.size()) {
        int r = send(c, (char*)A.data() + sent, (int)(A.size() - sent), 0);
        if (r > 0) { sent += (size_t)r; continue; }
        break;
    }
    printf("[control] peer SO_RCVBUF=%d sender SO_SNDBUF=%d ; raw non-blocking send accepted %llu of %llu\n",
           rcv2, snd2, (unsigned long long)sent, (unsigned long long)A.size());
    closesocket(s); closesocket(c); closesocket(lst);
}

int wmain() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WP;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PendProbe";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"PendProbe", L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);

    SOCKET lst = INVALID_SOCKET;
    if (MakePeer(47901, &lst) == INVALID_SOCKET && lst == INVALID_SOCKET) { printf("bind failed %d\n", WSAGetLastError()); return 2; }

    NetEngine eng;
    g_eng = &eng;
    NetParams p;
    p.mode = kNetTcpClient;
    p.remote_ip = L"127.0.0.1";
    p.remote_port = 47901;
    if (!eng.Open(h, p)) { printf("Open FAILED\n"); return 2; }
    DWORD t0 = GetTickCount();
    while (!g_stOpen && GetTickCount() - t0 < 3000) Pump(50);
    if (!g_stOpen) { printf("connect FAILED\n"); return 2; }
    printf("connected=1\n");

    SOCKET s = accept(lst, nullptr, nullptr);
    if (s == INVALID_SOCKET) { printf("accept failed %d\n", WSAGetLastError()); return 2; }

    const size_t N1 = 64u << 20;
    std::vector<uint8_t> A(N1, 0xAA), B(64, 0xBB);
    int nA = eng.Write(A.data(), A.size());
    int nB = eng.Write(B.data(), B.size());
    printf("first Write(64MiB) -> %d ; second Write(64B) -> %d\n", nA, nB);
    Pump(1500);                              // let FD_WRITE run (peer still not reading)

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    size_t total = 0, bTail = 0;
    DWORD deadline = GetTickCount() + 2500;
    std::vector<uint8_t> sink(65536);
    while (GetTickCount() < deadline) {
        int r = recv(s, (char*)sink.data(), (int)sink.size(), 0);
        if (r > 0) {
            total += (size_t)r;
            for (int i = 0; i < r; ++i) if (sink[i] == 0xBB) ++bTail;
            continue;
        }
        if (r == 0) break;
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) break;
        Pump(20);
    }
    size_t expect = N1 + B.size();
    printf("peer received = %llu bytes (expect %llu), 0xBB bytes seen = %llu\n",
           (unsigned long long)total, (unsigned long long)expect, (unsigned long long)bTail);
    printf("VERDICT: %s\n", total == expect ? "OK (no loss)" : "DATA LOSS in engine pending queue");

    closesocket(s);
    closesocket(lst);
    RawControl();

    eng.Close();
    WSACleanup();
    return total == expect ? 0 : 1;
}