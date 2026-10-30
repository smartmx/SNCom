// sncom/src/core/hexutil.cpp —— ← PyCom hexutil.py 逐语义移植 + §54 成对扩展
// parse_hex：按 [空白,中英逗号,中英分号,-] 切词元；词元 = 可选 0x/0X 前缀 + hex 位；
// 词元 1~2 位 = 单字节（1 位=低半字节）；≥3 位连续串按 2 位自动成对、奇数末位右补 0
// （§54：无空格「313233」=「31 32 33」）。任一词元非法 → 整体无效（out 清空返回 false）。
#include "hexutil.h"

namespace sncom {

static int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

static bool IsSep(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f' ||
           c == L',' || c == L';' || c == 0xFF0C || c == 0xFF1B || c == L'-';
}

bool ParseHex(const std::wstring& s, std::vector<uint8_t>& out) {
    out.clear();
    auto bail = [&out]() { out.clear(); return false; };     // 全有或全无（← try_parse_hex None）
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        while (i < n && IsSep(s[i])) ++i;                   // re.split 语义：连续分隔符
        if (i >= n) break;
        size_t j = i;
        while (j < n && !IsSep(s[j])) ++j;
        std::wstring tok = s.substr(i, j - i);
        i = j;
        if (tok.size() > 2 && tok[0] == L'0' && (tok[1] == L'x' || tok[1] == L'X'))
            tok.erase(0, 2);                                 // 可选 0x 前缀
        if (tok.empty()) return bail();                      // 「0x」光前缀 → 整体无效
        auto push_byte = [&](const std::wstring& t) {        // 1~2 位 = 单字节（1 位=低半字节）
            int v = 0;
            for (wchar_t c : t) {
                int h = HexVal(c);
                if (h < 0) return false;
                v = v * 16 + h;
            }
            out.push_back((uint8_t)v);
            return true;
        };
        if (tok.size() <= 2) {
            if (!push_byte(tok)) return bail();
        } else {
            // §54：无空格连续串按 2 位自动成对（「313233」→ 31 32 33）；奇数末位右补 0
            if (tok.size() % 2) tok += L'0';
            for (size_t k = 0; k < tok.size(); k += 2)
                if (!push_byte(tok.substr(k, 2))) return bail();
        }
    }
    return true;                                             // 空串 → 0 字节（PyCom 一致）
}

std::wstring ToHex(const uint8_t* d, size_t n) {
    static const wchar_t* kHex = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(n ? n * 3 - 1 : 0);
    for (size_t i = 0; i < n; ++i) {
        if (i) out.push_back(L' ');
        out.push_back(kHex[d[i] >> 4]);
        out.push_back(kHex[d[i] & 0xF]);
    }
    return out;
}

} // namespace sncom
