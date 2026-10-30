// sncom/src/core/hexutil.h —— HEX 解析 / 格式化（对应 PyCom hexutil.py 的 spike 子集）
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace sncom {

// "01 02 FF,0a" → 字节；容忍空格/逗号/制表符分隔；奇数个十六进制位或非法字符 → 返回 false（out 清空）
bool ParseHex(const std::wstring& s, std::vector<uint8_t>& out);

// 字节 → "41 42 43 ..."（大写、空格分隔）；16 字节/行的分行由调用方处理
std::wstring ToHex(const uint8_t* d, size_t n);

} // namespace sncom
