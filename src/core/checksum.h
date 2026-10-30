// sncom/src/core/checksum.h —— 校验算法（← PyCom checksum.py 逐语义移植）
// kind: none/modbus_crc16/ccitt_crc16/crc8/xor8/add8/add16/crc32/add8_0
// tail: 1~4=末尾N字节是帧尾(校验插帧尾前) | 9=旧版末尾追加 | 0=不附加
// 16 位校验低字节在前（与 Modbus 一致）；ADD/XOR 按通行惯例 8 位累加/异或；
// §77：新增 crc32（ISO-HDLC 标准值，字节低字节在前与既有 16 位一致）与
// add8_0（原版 V5.09a2「0-ADD」：加完后用 0 减 = 两补数）
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace sncom {

uint16_t ModbusCrc16(const uint8_t* d, size_t n);      // init FFFF, poly A001 (反转)
uint16_t CcittCrc16(const uint8_t* d, size_t n);       // XMODEM 型：poly 1021, init 0000（'123456789'→0x31C3）
uint8_t  Crc8(const uint8_t* d, size_t n);             // poly 07, init 00（'123456789'→0xF4）
uint32_t Crc32(const uint8_t* d, size_t n);            // poly 04C11DB7 反转（'123456789'→0xCBF43926）
uint8_t  Xor8(const uint8_t* d, size_t n);
uint8_t  Add8(const uint8_t* d, size_t n);
uint8_t  Add8Zero(const uint8_t* d, size_t n);         // §77 0-ADD：(0 - 累加和) mod 256
uint16_t Add16(const uint8_t* d, size_t n);

// kind 对应的校验附加字节（低字节在前）；none → 空
std::vector<uint8_t> ChecksumAppendix(const std::wstring& kind, const uint8_t* d, size_t n);

// 校验值覆盖的字节段（SSCOM 语义）
std::vector<uint8_t> ChecksumBodySlice(const std::vector<uint8_t>& payload, int start, int tail);

// 按 SSCOM 语义附加校验：帧尾档(1~4)插在帧尾前（数据+校验+帧尾）；数据不长于帧尾时原样返回
std::vector<uint8_t> AppendChecksum(const std::vector<uint8_t>& payload, const std::wstring& kind,
                                    int start = 1, int tail = 0);

} // namespace sncom
