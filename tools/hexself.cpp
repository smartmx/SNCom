// sncom/tools/hexself.cpp —— ParseHex 自测（用例 ← hexutil.py 对拍语料，输出到文件供 crosscheck.py 比对）
// 用法：hexself.exe <result.txt>
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/core/hexutil.h"
#include "../src/core/checksum.h"

using namespace sncom;

struct Case { const wchar_t* in; const char* want; };
static Case kCases[] = {
    { L"01 02 0A",      "01020A" },
    { L"0x01,0x02",     "0102" },
    { L"1 2 3",         "010203" },
    { L"01-02",         "0102" },
    { L"01；02",         "0102" },
    { L"01，02",         "0102" },
    { L"0xAB",          "AB" },
    { L" 01 , 02 ",     "0102" },
    { L"",              "" },
    { L"0x",            "FAIL" },
    { L"012",           "0120" },   // §54：3 位连续串成对 + 末位补 0（原 FAIL）
    { L"zz",            "FAIL" },
    { L"01 02 0x",      "FAIL" },
    { L"0 0 0",         "000000" },
    { L"ff Fe 0a",      "FFFE0A" },
    { L"313233",        "313233" }, // §54：无空格按 2 位成对
    { L"313",           "3130" },   // §54：奇数末位右补 0
    { L"0xABCD",        "ABCD" },   // §54：前缀 + 4 位成对
};

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: hexself <result.txt>\n"); return 2; }
    FILE* fo = _wfopen(argv[1], L"wb");
    if (!fo) return 1;
    int pass = 0;
    int total = (int)(sizeof(kCases) / sizeof(kCases[0]));
    char b[8];                                         // 复用于 ParseHex/checksum 各段
    for (int i = 0; i < total; ++i) {
        std::vector<uint8_t> out;
        bool ok = ParseHex(kCases[i].in, out);
        std::string got = ok ? "" : "FAIL";
        for (uint8_t x : out) { snprintf(b, sizeof(b), "%02X", x); got += b; }
        bool good = (got == kCases[i].want);
        pass += good;
        fprintf(fo, "%d %s %s\n", i, got.c_str(), good ? "ok" : "MISMATCH");
    }
    // checksum 固化向量（'123456789' 六算法 + 插入/追加）
    const uint8_t v[] = { '1','2','3','4','5','6','7','8','9' };
    size_t vn = sizeof(v);
    int csPass = 0;
    auto cs = [&](const char* tag, const std::string& got, const std::string& want) {
        bool good = (got == want);
        csPass += good;
        fprintf(fo, "%s %s %s\n", tag, got.c_str(), good ? "ok" : "MISMATCH");
    };
    cs("modbus", [&]{ char t[8]; snprintf(t, sizeof(t), "%04X", ModbusCrc16(v, vn)); return std::string(t); }(), "4B37");
    cs("ccitt",  [&]{ char t[8]; snprintf(t, sizeof(t), "%04X", CcittCrc16(v, vn)); return std::string(t); }(), "31C3");
    cs("crc8",   [&]{ char t[8]; snprintf(t, sizeof(t), "%02X", Crc8(v, vn)); return std::string(t); }(), "F4");
    cs("crc32",  [&]{ char t[16]; snprintf(t, sizeof(t), "%08X", Crc32(v, vn)); return std::string(t); }(), "CBF43926");   // §77 ISO-HDLC 标准向量
    cs("add8",   [&]{ char t[8]; snprintf(t, sizeof(t), "%02X", Add8(v, vn)); return std::string(t); }(), "DD");
    cs("add8_0", [&]{ char t[8]; snprintf(t, sizeof(t), "%02X", Add8Zero(v, vn)); return std::string(t); }(), "23");     // §77 0-ADD：0-0xDD
    cs("add16",  [&]{ char t[8]; snprintf(t, sizeof(t), "%04X", Add16(v, vn)); return std::string(t); }(), "01DD");
    cs("xor8",   [&]{ char t[8]; snprintf(t, sizeof(t), "%02X", Xor8(v, vn)); return std::string(t); }(), "31");
    const uint8_t p[] = { '1','2','3','4','9' };
    auto ins = AppendChecksum(std::vector<uint8_t>(p, p + 5), L"add8", 1, 1);
    std::string is;
    for (uint8_t x : ins) { snprintf(b, sizeof(b), "%02x", x); is += b; }
    cs("insert", is, "31323334ca39");
    auto app = AppendChecksum(std::vector<uint8_t>(p, p + 5), L"modbus_crc16", 1, 9);
    std::string as;
    for (uint8_t x : app) { snprintf(b, sizeof(b), "%02x", x); as += b; }
    cs("append", as, "313233343971a1");
    fprintf(fo, "SUMMARY pass %d/%d\n", pass + csPass, total + 10);
    fclose(fo);
    wprintf(L"hexself: pass %d/%d -> %ls\n", pass + csPass, total + 10, argv[1]);
    return pass + csPass == total + 10 ? 0 : 1;
}
