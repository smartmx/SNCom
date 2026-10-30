// bignum.h —— 任意精度无符号整数（§97 进制转换页升级：≥256 位，上限 65536 位）
// 表示：小端字节序数组（bytes[0] = 最低字节），不含前导零字节（空 → {0}）。
// 只做进制转换页需要的解析/格式化；十进制与二进制的乘加/长除均按 10^9 分块（§118）——
// 上限 65536 位（8KB）时一次转换 ≈ 2000 趟 × 8KB，毫秒级（旧逐位实现约 9 倍慢）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sncom {

using BigBytes = std::vector<uint8_t>;
constexpr size_t kBigMaxBytes = 8192;            // 65536 位 / hex 16384 字符上限（防误操作卡死）

// 解析（成功才写 out；失败 input 非法/空/超上限）
bool BigParseDec(const std::wstring& s, BigBytes& out);         // 十进制数字（数值语义）
bool BigParseHexNum(const std::wstring& s, BigBytes& out);      // hex 数字，可含 0x 前缀（左补 0，数值语义）
bool BigParseBin(const std::wstring& s, BigBytes& out);         // 二进制串（数值语义）
bool BigParseLatin1BE(const std::wstring& s, BigBytes& out);    // 字符按大端入字节（首字符=最高字节）

// 规范化：去最高零字节；全零/空 → {0}
void BigTrim(BigBytes& b);

// 格式化
std::wstring BigToDec(const BigBytes& b);                       // 十进制（无前导零）
std::wstring BigToHexUpper(const BigBytes& b);                  // hex 大写（无前导零）
std::wstring BigToBin(const BigBytes& b);                       // 二进制（无前导零）
std::wstring BigToHexLE(const BigBytes& b);                     // 字节展开 "AA BB"（最低字节在前）
std::wstring BigToHexBE(const BigBytes& b);                     // 字节展开（最高字节在前）
std::wstring BigToLatin1(const BigBytes& b);                    // 大端字符序，跳过 NUL；空 → " "
std::wstring BigToCArray(const BigBytes& b);                    // "0x01, 0x02"（小端内存序）

// 取低 n 字节（补零到 n）——供 float32/64 位模式解释
void BigLowBytes(const BigBytes& b, size_t n, uint8_t* out);

}  // namespace sncom
