// sncom/src/core/checksum.cpp —— ← checksum.py
#include "checksum.h"

namespace sncom {

uint16_t ModbusCrc16(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

uint16_t CcittCrc16(const uint8_t* d, size_t n) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)(d[i] << 8);
        for (int k = 0; k < 8; ++k) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t Crc8(const uint8_t* d, size_t n) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
            else crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t Crc32(const uint8_t* d, size_t n) {
    // CRC-32/ISO-HDLC（zip/png 标准）：poly 04C11DB7 反射为 EDB88320，init FFFFFFFF，异或输出
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint8_t Xor8(const uint8_t* d, size_t n) {
    uint8_t x = 0;
    for (size_t i = 0; i < n; ++i) x ^= d[i];
    return x;
}

uint8_t Add8(const uint8_t* d, size_t n) {
    uint16_t s = 0;
    for (size_t i = 0; i < n; ++i) s += d[i];
    return (uint8_t)(s & 0xFF);
}

uint8_t Add8Zero(const uint8_t* d, size_t n) {
    // §77：原版 V5.09a2「0-ADD」——加完后用 0 减（两补数）
    return (uint8_t)(0 - (int)Add8(d, n));
}

uint16_t Add16(const uint8_t* d, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s += d[i];
    return (uint16_t)(s & 0xFFFF);
}

std::vector<uint8_t> ChecksumAppendix(const std::wstring& kind, const uint8_t* d, size_t n) {
    std::vector<uint8_t> out;
    auto push16 = [](std::vector<uint8_t>& v, uint16_t x) {
        v.push_back((uint8_t)(x & 0xFF));
        v.push_back((uint8_t)(x >> 8));          // 低字节在前（与 Modbus 一致）
    };
    if (kind == L"modbus_crc16") push16(out, ModbusCrc16(d, n));
    else if (kind == L"ccitt_crc16") push16(out, CcittCrc16(d, n));
    else if (kind == L"crc8") out.push_back(Crc8(d, n));
    else if (kind == L"xor8") out.push_back(Xor8(d, n));
    else if (kind == L"add8") out.push_back(Add8(d, n));
    else if (kind == L"add8_0") out.push_back(Add8Zero(d, n));       // §77：原版 0-ADD
    else if (kind == L"crc32") {                                     // §77：低字节在前（与 16 位口径一致）
        uint32_t c = Crc32(d, n);
        out.push_back((uint8_t)(c & 0xFF));
        out.push_back((uint8_t)((c >> 8) & 0xFF));
        out.push_back((uint8_t)((c >> 16) & 0xFF));
        out.push_back((uint8_t)((c >> 24) & 0xFF));
    }
    else if (kind == L"add16") push16(out, Add16(d, n));
    return out;
}

std::vector<uint8_t> ChecksumBodySlice(const std::vector<uint8_t>& payload, int start, int tail) {
    size_t n = payload.size();
    if (tail == 9) {                              // 旧版：第 start 字节至末尾
        if (n == 0) return {};
        size_t hi = n;
        size_t lo = start > 0 ? (size_t)(start - 1) : 0;
        if (lo > hi) lo = hi;
        return std::vector<uint8_t>(payload.begin() + lo, payload.begin() + hi);
    }
    size_t tn = tail < 1 ? 1 : (tail > 4 ? 4 : (size_t)tail);
    if (n <= tn) return {};                       // 数据不比帧尾长：无处计算
    size_t cut = n - tn;
    size_t lo = start > 0 ? (size_t)(start - 1) : 0;
    if (lo > cut) lo = cut;
    return std::vector<uint8_t>(payload.begin() + lo, payload.begin() + cut);
}

std::vector<uint8_t> AppendChecksum(const std::vector<uint8_t>& payload, const std::wstring& kind,
                                    int start, int tail) {
    if (kind == L"none" || kind.empty() || tail == 0) return payload;
    std::vector<uint8_t> body = ChecksumBodySlice(payload, start, tail);
    std::vector<uint8_t> app = ChecksumAppendix(kind, body.data(), body.size());
    if (app.empty()) return payload;
    if (tail == 9) {
        std::vector<uint8_t> out = payload;
        out.insert(out.end(), app.begin(), app.end());
        return out;
    }
    size_t n = payload.size();
    size_t tn = tail < 1 ? 1 : (tail > 4 ? 4 : (size_t)tail);
    if (n <= tn) return payload;
    size_t cut = n - tn;
    std::vector<uint8_t> out(payload.begin(), payload.begin() + cut);
    out.insert(out.end(), app.begin(), app.end());
    out.insert(out.end(), payload.begin() + cut, payload.end());
    return out;                                   // 数据 + 校验 + 帧尾
}

} // namespace sncom
