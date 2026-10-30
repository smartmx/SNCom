// sncom/tools/dump_native.cpp —— 对拍工具：DAT/向量文件 → 行表/保存txt/解码文本/多条ini JSON
// 用法：
//   dump_native line <in.dat> <out.txt> [encoding=gbk] [hex=0]   行表（+ .meta 字节跨度）
//   dump_native txt  <in.dat> <out.txt> [encoding=gbk]           保存 txt 口径（anno+CRLF+open 行）
//   dump_native enc  <in.bin> <out.txt> [encoding=gbk]           增量解码（1KB 分块+flush）→ UTF-8
//   dump_native msini <in.ini> <out.json>                        多条 ini 解析核 → JSON
// 旧 2 参数形式 = line（runcross.bat 历史调用兼容）。
// 行表输出与 PyCom LineBuilder.lines[].text 一致（不含时间戳前缀——时间戳取真实时钟
// 无法对齐，对拍只比 text/行数/字节跨度）；txt 模式喂固定 ts=1000.0，prefix 两侧同源可对齐。
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/core/display.h"
#include "../src/core/encoding.h"
#include "../src/core/msini.h"

using namespace sncom;

static bool ReadAllBytes(const wchar_t* path, std::vector<uint8_t>& data) {
    FILE* fi = _wfopen(path, L"rb");
    if (!fi) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(fi);
    return true;
}

static std::string ToUtf8(const std::wstring& s) {
    std::string utf8;
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        utf8.resize((size_t)len);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], len, nullptr, nullptr);
    }
    return utf8;
}

static void WriteStr(FILE* fo, const std::string& s) { fwrite(s.data(), 1, s.size(), fo); }

static void JsonEscape(const std::wstring& s, std::string& out) {
    out += '"';
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  out += "\\\""; break;
            case L'\\': out += "\\\\"; break;
            case L'\n': out += "\\n"; break;
            case L'\r': out += "\\r"; break;
            case L'\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    sprintf(b, "\\u%04x", (unsigned)c);
                    out += b;
                } else {
                    out += ToUtf8(std::wstring(1, c));
                }
        }
    }
    out += '"';
}

// 喂数据（1KB 分块，模拟跨包半截序列；split=false → 行为只由换行决定）
static void FeedAll(LineBuilder& b, const std::vector<uint8_t>& data) {
    double ts = 1000.0;
    for (size_t off = 0; off < data.size(); off += 1024) {
        size_t k = data.size() - off < 1024 ? data.size() - off : 1024;
        b.feed(false, data.data() + off, k, ts, off);
        b.maybe_flush(ts + 1.0);                 // 持续流：增量续写
    }
    b.flush();
}

static int ModeLine(const std::vector<uint8_t>& data, const std::wstring& enc, bool hex,
                    const wchar_t* outPath) {
    LineBuilder b(enc, hex, /*split*/ false, 20);
    FeedAll(b, data);
    FILE* fo = _wfopen(outPath, L"wb");
    FILE* fm = _wfopen((std::wstring(outPath) + L".meta").c_str(), L"wb");
    if (!fo || !fm) { wprintf(L"cannot open output\n"); return 1; }
    int lines = b.line_count();
    for (int i = 0; i < lines; ++i) {
        const DispLine* l = b.line_at(i);
        if (!l) break;
        WriteStr(fo, ToUtf8(l->text));
        fputc('\n', fo);
        fprintf(fm, "%d %llu\n", l->nbytes, (unsigned long long)l->start_abs);
    }
    fclose(fo);
    fclose(fm);
    wprintf(L"lines=%d bytes=%zu -> %ls\n", lines, data.size(), outPath);
    return 0;
}

static int ModeTxt(const std::vector<uint8_t>& data, const std::wstring& enc,
                   const wchar_t* outPath) {
    // ← MainWindow::SaveReceived txt 分支：anno 开（包首行 prefix+方向标识）+ CRLF + open 行
    LineBuilder b(enc, false, /*split*/ true, 20);
    FeedAll(b, data);
    FILE* fo = _wfopen(outPath, L"wb");
    if (!fo) { wprintf(L"cannot open output\n"); return 1; }
    auto emit = [&](const DispLine* l) {
        std::wstring s = l->prefix + (l->prefix.empty() ? L"" : DirMarker(l->is_tx)) + l->text;
        s += L"\r\n";
        WriteStr(fo, ToUtf8(s));
    };
    int lines = b.line_count();
    for (int i = 0; i < lines; ++i) {
        const DispLine* l = b.line_at(i);
        if (!l) break;
        emit(l);
    }
    if (b.open()) emit(b.open());                // 未收尾行（SaveReceived 同款补写）
    fclose(fo);
    wprintf(L"txt lines=%d bytes=%zu -> %ls\n", lines, data.size(), outPath);
    return 0;
}

static int ModeEnc(const std::vector<uint8_t>& data, const std::wstring& enc,
                   const wchar_t* outPath, size_t chunk) {
    IncrementalDecoder dec(enc);                 // 分块喂：跨包半截序列口径同 Python 增量解码
    std::wstring out;
    for (size_t off = 0; off < data.size(); off += chunk) {
        size_t k = data.size() - off < chunk ? data.size() - off : chunk;
        out += dec.Feed(data.data() + off, k);
    }
    out += dec.FlushFinal();
    FILE* fo = _wfopen(outPath, L"wb");
    if (!fo) { wprintf(L"cannot open output\n"); return 1; }
    WriteStr(fo, ToUtf8(out));
    fclose(fo);
    wprintf(L"enc=%ls bytes=%zu -> %ls\n", enc.c_str(), data.size(), outPath);
    return 0;
}

static int ModeMsIni(const std::vector<uint8_t>& data, const wchar_t* outPath) {
    int wlen = MultiByteToWideChar(936, 0, (const char*)data.data(), (int)data.size(), nullptr, 0);
    std::wstring text((size_t)(wlen > 0 ? wlen : 0), 0);
    if (wlen > 0) MultiByteToWideChar(936, 0, (const char*)data.data(), (int)data.size(), &text[0], wlen);
    std::vector<std::pair<std::wstring, std::wstring>> keys;
    ParseIniKeys(text, keys);
    std::vector<MsIniRow> rows;
    ParseMsIniRows(keys, rows);
    FILE* fo = _wfopen(outPath, L"wb");
    if (!fo) { wprintf(L"cannot open output\n"); return 1; }
    std::string out = "[";
    for (size_t i = 0; i < rows.size(); ++i) {
        const MsIniRow& r = rows[i];
        char buf[96];
        sprintf(buf, "%s{\"order\":%d,\"note\":", i ? "," : "", r.order);
        out += buf;
        JsonEscape(r.note, out);
        out += ",\"hex\":";
        out += r.hex ? "1" : "0";
        out += ",\"text\":";
        JsonEscape(r.text, out);
        sprintf(buf, ",\"interval\":%d}", r.interval);
        out += buf;
    }
    out += "]";
    WriteStr(fo, out);
    fclose(fo);
    wprintf(L"msini rows=%d -> %ls\n", (int)rows.size(), outPath);
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && wcscmp(argv[1], L"line") != 0 && wcscmp(argv[1], L"txt") != 0 &&
        wcscmp(argv[1], L"enc") != 0 && wcscmp(argv[1], L"msini") != 0) {
        // 旧形式：dump_native <in.dat> <out.txt> [enc] [hex]
        std::vector<uint8_t> data;
        if (!ReadAllBytes(argv[1], data)) { wprintf(L"cannot open %ls\n", argv[1]); return 1; }
        return ModeLine(data, argc > 3 ? argv[3] : L"gbk", argc > 4 && argv[4][0] == L'1', argv[2]);
    }
    if (argc < 4) {
        wprintf(L"usage: dump_native line|txt|enc <in> <out> [encoding] | msini <in.ini> <out.json>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!ReadAllBytes(argv[2], data)) { wprintf(L"cannot open %ls\n", argv[2]); return 1; }
    if (wcscmp(argv[1], L"line") == 0)
        return ModeLine(data, argc > 4 ? argv[4] : L"gbk", argc > 5 && argv[5][0] == L'1', argv[3]);
    if (wcscmp(argv[1], L"txt") == 0)
        return ModeTxt(data, argc > 4 ? argv[4] : L"gbk", argv[3]);
    if (wcscmp(argv[1], L"enc") == 0)
        return ModeEnc(data, argc > 4 ? argv[4] : L"gbk", argv[3],
                       argc > 5 ? (size_t)_wtoi(argv[5]) : 1024);
    return ModeMsIni(data, argv[3]);
}
