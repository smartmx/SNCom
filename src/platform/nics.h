// sncom/src/platform/nics.h —— 网卡枚举（← PyCom _refresh_nics/_refresh_local_ips 语义）
// 口径：只要 Up 且有单播地址的接口；显示名 = FriendlyName；地址 = 单播（IPv6 链路本地
// 自带 %作用域）；label = "名字 (首地址)"；全部网卡本地表 = 0.0.0.0 + 各IPv4 + :: + 各IPv6
// （IPv6 全局在前、fe80 靠后；IPv4 补 127.0.0.1）；指定网卡 = 该网卡 v4+v6。
#pragma once
#include <string>
#include <vector>

namespace sncom {

struct NicInfo {
    std::wstring name;                  // FriendlyName（= Qt humanReadableName 口径）
    std::wstring label;                 // "名字 (首地址)"（下拉显示）
    std::vector<std::wstring> addrs;    // IPv4 在前、IPv6 在后（含 %scope）
};

// 需要 WSA 已初始化（main 里 WSAStartup 一次）
std::vector<NicInfo> EnumNics();

// "全部网卡"的本地地址表（PyCom _refresh_local_ips 的 else 分支）
std::vector<std::wstring> AllLocalAddresses(const std::vector<NicInfo>& nics);

} // namespace sncom
