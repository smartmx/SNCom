// sncom/src/platform/nics.cpp —— GetAdaptersAddresses 枚举
#include "nics.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace sncom {

static std::wstring AddrToString(const SOCKET_ADDRESS& sa) {
    wchar_t buf[128]{};
    DWORD len = 128;
    if (WSAAddressToStringW(sa.lpSockaddr, sa.iSockaddrLength, nullptr, buf, &len) != 0)
        return L"";
    return buf;                          // IPv6 链路本地自带 %作用域（fe80::x%12）
}

static bool IsV6(const SOCKET_ADDRESS& sa) { return sa.lpSockaddr->sa_family == AF_INET6; }

std::vector<NicInfo> EnumNics() {
    std::vector<NicInfo> out;
    ULONG sz = 16 * 1024;
    std::vector<uint8_t> buf(sz);
    PIP_ADAPTER_ADDRESSES aa = nullptr;
    ULONG r = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        aa = (PIP_ADAPTER_ADDRESSES)buf.data();
        r = GetAdaptersAddresses(AF_UNSPEC,
                                 GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,        // 不传 SKIP_FRIENDLY_NAME：
                                 nullptr, aa, &sz);               // 我们依赖 FriendlyName（审查 P3-3）
        if (r != ERROR_BUFFER_OVERFLOW) break;
        buf.resize(sz);
    }
    if (r != NO_ERROR) return out;

    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;      // ← PyCom IsUp 口径
        if (!a->FirstUnicastAddress) continue;
        if (!a->FriendlyName || !a->FriendlyName[0]) continue;

        std::vector<std::wstring> v4, v6;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            std::wstring t = AddrToString(u->Address);
            if (t.empty()) continue;
            if (IsV6(u->Address)) { if (std::find(v6.begin(), v6.end(), t) == v6.end()) v6.push_back(t); }
            else                  { if (std::find(v4.begin(), v4.end(), t) == v4.end()) v4.push_back(t); }
        }
        if (v4.empty() && v6.empty()) continue;

        NicInfo n;
        n.name = a->FriendlyName;
        n.addrs = v4;
        n.addrs.insert(n.addrs.end(), v6.begin(), v6.end());   // IPv4 在前 IPv6 在后
        n.label = n.name + L" (" + n.addrs[0] + L")";
        out.push_back(std::move(n));
    }
    return out;
}

std::vector<std::wstring> AllLocalAddresses(const std::vector<NicInfo>& nics) {
    // PyCom：全部网卡 = ["0.0.0.0"] + 各IPv4（补 127.0.0.1）+ ["::"] + 各IPv6（全局在前）
    std::vector<std::wstring> v4, v6;
    for (const auto& n : nics)
        for (const auto& t : n.addrs)
            (t.find(L':') != std::wstring::npos ? v6 : v4).push_back(t);
    auto dedup = [](std::vector<std::wstring>& v) {
        std::vector<std::wstring> out;
        for (const auto& t : v)
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        v.swap(out);
    };
    dedup(v4);
    dedup(v6);
    if (std::find(v4.begin(), v4.end(), L"127.0.0.1") == v4.end()) v4.push_back(L"127.0.0.1");
    std::sort(v6.begin(), v6.end(), [](const std::wstring& a, const std::wstring& b) {
        bool fa = a.rfind(L"fe80", 0) == 0, fb = b.rfind(L"fe80", 0) == 0;
        if (fa != fb) return fa < fb;        // 全局地址在前，fe80 靠后
        return a < b;
    });
    std::vector<std::wstring> out;
    out.push_back(L"0.0.0.0");
    out.insert(out.end(), v4.begin(), v4.end());
    out.push_back(L"::");
    out.insert(out.end(), v6.begin(), v6.end());
    return out;
}

} // namespace sncom
