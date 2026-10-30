// bignum.cpp —— 任意精度无符号整数（小端字节序）。见 bignum.h。
#include "bignum.h"

#include <algorithm>

namespace sncom {

namespace {

// r = r * mul + add（就地乘加，小端）
bool MulAdd(BigBytes& r, uint32_t mul, uint32_t add) {
    uint64_t carry = add;
    for (size_t i = 0; i < r.size(); ++i) {
        uint64_t cur = (uint64_t)r[i] * mul + carry;
        r[i] = (uint8_t)(cur & 0xFF);
        carry = cur >> 8;
    }
    while (carry) {
        r.push_back((uint8_t)(carry & 0xFF));
        carry >>= 8;
        if (r.size() > kBigMaxBytes) return false;
    }
    return true;
}

int HexVal2(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

}  // namespace

void BigTrim(BigBytes& b) {
    while (!b.empty() && b.back() == 0) b.pop_back();
    if (b.empty()) b.push_back(0);
}

bool BigParseDec(const std::wstring& s, BigBytes& out) {
    if (s.empty()) return false;
    // 上限预检：每位十进制 ≈ 3.33 bit，长度 > kBigMaxBytes*3 必超
    if (s.size() > kBigMaxBytes * 3) return false;
    BigBytes r{0};
    // §118（U4 还债）：按 9 位一块乘加（r = r*10^k + 块）——原逐位乘 10 的代价是
    // O(十进制位数 × 字节数)，65536 位时 ≈1.6 亿字节操作；分块后乘加次数降 9 倍。
    static const uint32_t kPow10[10] = { 1u, 10u, 100u, 1000u, 10000u, 100000u,
                                         1000000u, 10000000u, 100000000u, 1000000000u };
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const size_t chunk = (i == 0) ? (n % 9 ? n % 9 : 9) : 9;
        uint32_t val = 0;
        for (size_t k = 0; k < chunk; ++k) {
            wchar_t c = s[i + k];
            if (c < L'0' || c > L'9') return false;
            val = val * 10 + (uint32_t)(c - L'0');
        }
        if (!MulAdd(r, (i == 0) ? kPow10[chunk] : 1000000000u, val)) return false;
        if (r.size() > kBigMaxBytes) return false;
        i += chunk;
    }
    BigTrim(r);
    out.swap(r);
    return true;
}

bool BigParseHexNum(const std::wstring& s, BigBytes& out) {
    std::wstring t = s;
    if (t.size() >= 2 && t[0] == L'0' && (t[1] == L'x' || t[1] == L'X')) t.erase(0, 2);
    if (t.empty()) return false;
    if (t.size() > kBigMaxBytes * 2) return false;               // 2 hex 字符 = 1 字节
    BigBytes r;
    r.reserve(t.size() / 2 + 1);
    const size_t n = t.size();
    const size_t pairs = (n + 1) / 2;                            // 奇数长度左侧补 0（数值语义）
    for (size_t k = 0; k < pairs; ++k) {                         // 从右往左成组 → 小端 push
        size_t loPos = n - 1 - 2 * k;
        int lo = HexVal2(t[loPos]);
        if (lo < 0) return false;
        int hi = 0;
        if (loPos >= 1) {                                        // loPos=0 表示最高组高位补 0
            hi = HexVal2(t[loPos - 1]);
            if (hi < 0) return false;
        }
        r.push_back((uint8_t)(hi * 16 + lo));
    }
    BigTrim(r);
    out.swap(r);
    return true;
}

bool BigParseBin(const std::wstring& s, BigBytes& out) {
    if (s.empty()) return false;
    if (s.size() > kBigMaxBytes * 8) return false;
    for (wchar_t c : s)
        if (c != L'0' && c != L'1') return false;
    BigBytes r;
    r.reserve(s.size() / 8 + 1);
    // 从右往左每 8 位一组 → 一个字节（小端 push）
    size_t n = s.size();
    for (size_t end = n; end > 0; end -= std::min<size_t>(8, end)) {
        size_t beg = end >= 8 ? end - 8 : 0;
        uint8_t byte = 0;
        for (size_t k = beg; k < end; ++k) byte = (uint8_t)((byte << 1) | (s[k] == L'1' ? 1 : 0));
        r.push_back(byte);
    }
    BigTrim(r);
    out.swap(r);
    return true;
}

bool BigParseLatin1BE(const std::wstring& s, BigBytes& out) {
    if (s.empty()) return false;
    if (s.size() > kBigMaxBytes) return false;
    BigBytes r;
    r.reserve(s.size());
    for (size_t k = s.size(); k > 0; --k) r.push_back((uint8_t)(s[k - 1] & 0xFF));  // 首字符=最高字节
    BigTrim(r);
    out.swap(r);
    return true;
}

std::wstring BigToDec(const BigBytes& b) {
    if (b.empty()) return L"0";
    // §118（U4 还债）：长除 10^9（每趟抽 9 位十进制）——原按位除 10 的代价是
    // O(十进制位数 × 字节数)，65536 位（8KB）时 19728 趟全程扫数 ≈1.6 亿字节操作；
    // 改 2192 趟后同量级校验（calcself --bench 前后对比）应约 9 倍加速。
    BigBytes t = b;
    std::wstring digits;
    digits.reserve(t.size() * 3 + 1);                            // 每字节 ≤ 2.41 位十进制
    const uint32_t kChunk = 1000000000u;                         // 10^9
    while (!(t.size() == 1 && t[0] == 0)) {
        uint64_t rem = 0;                                        // 长除（高位到低位）
        for (size_t i = t.size(); i > 0; --i) {
            uint64_t cur = (rem << 8) | t[i - 1];
            t[i - 1] = (uint8_t)(cur / kChunk);
            rem = cur % kChunk;
        }
        while (t.size() > 1 && t.back() == 0) t.pop_back();
        if (t.size() == 1 && t[0] == 0) {
            while (rem) {                                        // 最高块：不带前导零
                digits.push_back((wchar_t)(L'0' + rem % 10));
                rem /= 10;
            }
        } else {
            for (int k = 0; k < 9; ++k) {                        // 低位块：补齐 9 位
                digits.push_back((wchar_t)(L'0' + rem % 10));
                rem /= 10;
            }
        }
    }
    if (digits.empty()) digits = L"0";
    std::reverse(digits.begin(), digits.end());
    return digits;
}

std::wstring BigToHexUpper(const BigBytes& b) {
    if (b.empty()) return L"0";
    static const wchar_t* kHex = L"0123456789ABCDEF";
    std::wstring s;
    s.reserve(b.size() * 2);
    bool started = false;
    for (size_t i = b.size(); i > 0; --i) {
        uint8_t byte = b[i - 1];
        int hi = byte >> 4, lo = byte & 0xF;
        if (!started) {
            if (hi) { s.push_back(kHex[hi]); started = true; }
            if (started || lo) { s.push_back(kHex[lo]); started = true; }
        } else {
            s.push_back(kHex[hi]);
            s.push_back(kHex[lo]);
        }
    }
    if (s.empty()) s = L"0";
    return s;
}

std::wstring BigToBin(const BigBytes& b) {
    if (b.empty()) return L"0";
    std::wstring s;
    s.reserve(b.size() * 8);
    bool started = false;
    for (size_t i = b.size(); i > 0; --i) {
        for (int k = 7; k >= 0; --k) {
            int bit = (b[i - 1] >> k) & 1;
            if (bit) started = true;
            if (started) s.push_back(bit ? L'1' : L'0');
        }
    }
    if (s.empty()) s = L"0";
    return s;
}

namespace {
std::wstring JoinHexBytes(const BigBytes& b, bool lowFirst, bool uppper) {
    static const wchar_t* kHexU = L"0123456789ABCDEF";
    static const wchar_t* kHexL = L"0123456789abcdef";
    const wchar_t* hex = uppper ? kHexU : kHexL;
    std::wstring s;
    if (b.empty()) return L"00";
    s.reserve(b.size() * 3);
    for (size_t k = 0; k < b.size(); ++k) {
        uint8_t byte = lowFirst ? b[k] : b[b.size() - 1 - k];
        if (k) s += L' ';
        s += hex[byte >> 4];
        s += hex[byte & 0xF];
    }
    return s;
}
}  // namespace

std::wstring BigToHexLE(const BigBytes& b) { return JoinHexBytes(b, true, true); }
std::wstring BigToHexBE(const BigBytes& b) { return JoinHexBytes(b, false, true); }

std::wstring BigToLatin1(const BigBytes& b) {
    std::wstring s;
    for (size_t k = b.size(); k > 0; --k) {
        uint8_t c = b[k - 1];
        if (c) s += (wchar_t)c;
    }
    if (s.empty()) s = L" ";
    return s;
}

std::wstring BigToCArray(const BigBytes& b) {
    std::wstring s;
    s.reserve(b.size() * 7);
    for (size_t k = 0; k < b.size(); ++k) {
        wchar_t buf[8];
        swprintf(buf, 8, L"0x%02X", (unsigned)b[k]);
        if (k) s += L", ";
        s += buf;
    }
    if (s.empty()) s = L"0x00";
    return s;
}

void BigLowBytes(const BigBytes& b, size_t n, uint8_t* out) {
    for (size_t k = 0; k < n; ++k) out[k] = k < b.size() ? b[k] : 0;
}

}  // namespace sncom
