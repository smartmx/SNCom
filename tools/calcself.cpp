// calcself.cpp —— bignum 自测（§97 进制转换任意精度）：往返 + 已知常量 + 边界
// 用法：calcself.exe <result.txt>；结果行 "bignum <i> ok/MISMATCH"（退出码 0/1）
// 自洽用例（无需跨语言基线）；已知常量由 Python 复核（见 §97 docs）。
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/core/bignum.h"
#include "../src/core/hexutil.h"     // ParseHex（§54 字节流语义用例）

using namespace sncom;

static int gPass = 0, gTotal = 0;
static FILE* gFo = nullptr;

static void chk(int idx, bool ok, const char* note) {
    ++gTotal;
    gPass += ok;
    fprintf(gFo, "bignum %d %s%s%s\n", idx, ok ? "ok" : "MISMATCH", note[0] ? " " : "", note);
}

static std::wstring Rep(wchar_t c, int n) { return std::wstring((size_t)n, c); }

// ---- §118（U4 还债）--bench：上限规模（65536 位）十进制转换计时 + 旧算法同机对照 ----
// 旧算法 = §118 之前的逐位实现，就地内联为参考实现：它同时充当**独立实现的正确性对照**
// （两条实现结果必须逐字符一致），并给出"同样是这台机器"的加速比，避免跨机时间不可比。
static uint64_t NowMs() { return GetTickCount64(); }

static std::wstring RefToDec(BigBytes t) {          // O(位数×字节数) 的旧口径
    std::wstring digits;
    while (!(t.size() == 1 && t[0] == 0)) {
        uint32_t rem = 0;
        for (size_t i = t.size(); i > 0; --i) {
            uint32_t cur = (rem << 8) | t[i - 1];
            t[i - 1] = (uint8_t)(cur / 10);
            rem = cur % 10;
        }
        digits.push_back((wchar_t)(L'0' + rem));
        while (t.size() > 1 && t.back() == 0) t.pop_back();
    }
    if (digits.empty()) digits = L"0";
    std::reverse(digits.begin(), digits.end());
    return digits;
}

static bool RefParseDec(const std::wstring& s, BigBytes& out) {   // 旧口径：逐位乘 10
    BigBytes r{0};
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') return false;
        uint64_t carry = (uint32_t)(c - L'0');
        for (size_t i = 0; i < r.size(); ++i) {      // 必须全量扫描（carry 中间可为 0，高位仍可能产生新进位）
            uint64_t cur = (uint64_t)r[i] * 10 + carry;
            r[i] = (uint8_t)(cur & 0xFF);
            carry = cur >> 8;
        }
        while (carry) { r.push_back((uint8_t)(carry & 0xFF)); carry >>= 8; }
    }
    BigTrim(r);
    out.swap(r);
    return true;
}

static int Bench() {
    // 输入：8192 字节 0xFF = 65536 位（kBigMaxBytes 上限），hex 串 16384 个 'F'
    std::wstring hex = Rep(L'F', 16384);
    BigBytes v;
    if (!BigParseHexNum(hex, v)) { wprintf(L"bench: parse hex failed\n"); return 1; }
    int fails = 0;

    uint64_t t0 = NowMs();
    std::wstring dec = BigToDec(v);
    uint64_t tNew = NowMs() - t0;
    t0 = NowMs();
    std::wstring decRef = RefToDec(v);
    uint64_t tOld = NowMs() - t0;
    bool okDec = (dec == decRef);
    if (!okDec) ++fails;
    // 位数：⌈65536·log10(2)⌉ = 19729
    bool okLen = (dec.size() == 19729);
    if (!okLen) ++fails;

    t0 = NowMs();
    BigBytes back;
    bool okNew = BigParseDec(dec, back);
    uint64_t tNewP = NowMs() - t0;
    t0 = NowMs();
    BigBytes backRef;
    bool okOld = RefParseDec(dec, backRef);
    uint64_t tOldP = NowMs() - t0;
    bool okRound = okNew && okOld && back == v && backRef == v;
    if (!okRound) {
        wprintf(L"bench dbg okNew=%d okOld=%d back=%d ref=%d v=%d\n", (int)okNew, (int)okOld,
                (int)back.size(), (int)backRef.size(), (int)v.size());
        size_t n = back.size() < v.size() ? back.size() : v.size();
        for (size_t i = 0; i < n; ++i) {
            if (back[i] != v[i]) { wprintf(L"bench dbg new first diff at %d: %02x vs %02x\n",
                                           (int)i, back[i], v[i]); break; }
            if (backRef[i] != v[i]) { wprintf(L"bench dbg ref first diff at %d: %02x vs %02x\n",
                                              (int)i, backRef[i], v[i]); break; }
        }
    }
    if (!okRound) ++fails;

    // 跨语言基线（探针用）：十进制串的 FNV-1a 64（按 UTF-16 码元；探针在 Python 侧同法复算）
    uint64_t h = 1469598103934665603ull;            // FNV offset basis
    for (wchar_t c : dec) {
        h ^= (uint64_t)(uint16_t)c;
        h *= 1099511628211ull;                      // FNV prime
    }

    wprintf(L"bench bits=65536 digits=%d\n", (int)dec.size());
    wprintf(L"bench ToDec   new=%llums old=%llums ok=%d\n",
            (unsigned long long)tNew, (unsigned long long)tOld, (int)okDec);
    wprintf(L"bench ParseDec new=%llums old=%llums ok=%d\n",
            (unsigned long long)tNewP, (unsigned long long)tOldP, (int)okRound);
    wprintf(L"bench fnv64=%016llx\n", (unsigned long long)h);
    wprintf(L"bench %s\n", fails ? L"FAIL" : L"PASS");
    return fails ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: calcself <result.txt> | --bench\n"); return 2; }
    if (wcscmp(argv[1], L"--bench") == 0) return Bench();
    gFo = _wfopen(argv[1], L"wb");
    if (!gFo) return 1;

    // 0. 用户报障样例：31 个 'a'（124 位）——≥64 位不再截断，hex 往返一致
    {
        BigBytes b;
        bool ok = BigParseHexNum(Rep(L'a', 31), b);
        bool r1 = false;
        if (ok) {
            BigTrim(b);
            r1 = (BigToDec(b) == L"14178431955039102644307275309657008810") &&
                 (BigToHexUpper(b) == Rep(L'A', 31));
        }
        chk(0, r1, "31xA round-trip (user case)");
    }
    // 1. 256 位边界：64 个 F → 十进制常量
    {
        BigBytes b;
        bool ok = BigParseHexNum(Rep(L'F', 64), b);
        chk(1, ok && BigToDec(b) == L"115792089237316195423570985008687907853269984665640564039457584007913129639935",
            "2^256-1 to dec");
    }
    // 2. 十进制常量 → 64 个 F（dec→hex 方向）
    {
        BigBytes b;
        bool ok = BigParseDec(L"115792089237316195423570985008687907853269984665640564039457584007913129639935", b);
        chk(2, ok && BigToHexUpper(b) == Rep(L'F', 64), "dec to 2^256-1 hex");
    }
    // 3. LE 字节流往返："01 02 03" → ToHexLE 一致
    {
        std::vector<uint8_t> b;
        bool ok = ParseHex(L"01 02 03", b) && b.size() == 3;
        BigBytes big(b.begin(), b.end());
        chk(3, ok && BigToHexLE(big) == L"01 02 03", "LE round-trip");
    }
    // 4. BE 字节流往返：输入 "01 02 03"（BE 语义=反转存）→ ToHexBE 复原
    {
        std::vector<uint8_t> b;
        ParseHex(L"01 02 03", b);
        BigBytes big(b.rbegin(), b.rend());
        chk(4, BigToHexBE(big) == L"01 02 03", "BE round-trip");
    }
    // 5. 超 64 位不截断：9 字节 LE → 全长保留（原实现截到 8 字节）
    {
        std::vector<uint8_t> b;
        ParseHex(L"010203040506070809", b);
        BigBytes big(b.begin(), b.end());
        chk(5, b.size() == 9 && BigToHexLE(big) == L"01 02 03 04 05 06 07 08 09",
            "9-byte no truncation");
    }
    // 6. BIN 往返：{0x01,0x02} 小端 = 0x0201 = 513 → "1000000001" → BigParseBin 回
    {
        BigBytes in{0x01, 0x02};
        std::wstring s = BigToBin(in);
        BigBytes out;
        bool ok = BigParseBin(s, out);
        chk(6, ok && s == L"1000000001" && out == in, "bin round-trip");
    }
    // 7. 非法输入全拒
    {
        BigBytes b;
        bool a = BigParseDec(L"", b);
        bool c = BigParseHexNum(L"zz", b);
        bool d = BigParseBin(L"12", b);
        bool e = BigParseHexNum(L"0x", b);
        bool f = BigParseDec(L"-1", b);
        bool g = BigParseHexNum(L"", b);
        chk(7, !a && !c && !d && !e && !f && !g, "invalid rejected");
    }
    // 8. 前导零正常化
    {
        BigBytes b;
        bool ok = BigParseHexNum(L"0000 1", b);   // 含空格应失败（数值解析不剥空格）
        BigBytes c;
        ok = !ok && BigParseHexNum(L"0001", c) && BigToHexUpper(c) == L"1";
        chk(8, ok, "leading zeros trimmed");
    }
    // 9. C 数组格式
    {
        BigBytes b{0x01, 0x02};
        chk(9, BigToCArray(b) == L"0x01, 0x02", "c-array format");
    }
    // 10. Latin1 往返
    {
        BigBytes b;
        bool ok = BigParseLatin1BE(L"ABC", b) && BigToLatin1(b) == L"ABC";
        chk(10, ok, "latin1 round-trip");
    }
    // 11. 奇数 hex 左补 0："abc" → 0xABC = 2748
    {
        BigBytes b;
        bool ok = BigParseHexNum(L"abc", b) && BigToDec(b) == L"2748";
        chk(11, ok, "odd-length left pad");
    }

    fprintf(gFo, "SUMMARY calcself %d/%d\n", gPass, gTotal);
    fclose(gFo);
    wprintf(L"calcself: %d/%d\n", gPass, gTotal);
    return gPass == gTotal ? 0 : 1;
}
