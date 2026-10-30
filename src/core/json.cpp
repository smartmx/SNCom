// sncom/src/core/json.cpp
#include "json.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace sncom {

JsonPtr JsonValue::Null()            { auto v = std::make_shared<JsonValue>(); v->type = JsonType::Null;   return v; }
JsonPtr JsonValue::Bool(bool b)      { auto v = std::make_shared<JsonValue>(); v->type = JsonType::Bool;   v->b = b; return v; }
JsonPtr JsonValue::Num(double n)     { auto v = std::make_shared<JsonValue>(); v->type = JsonType::Number; v->num = n; return v; }
JsonPtr JsonValue::Str(const std::wstring& s){ auto v = std::make_shared<JsonValue>(); v->type = JsonType::String; v->str = s; return v; }
JsonPtr JsonValue::Arr()             { auto v = std::make_shared<JsonValue>(); v->type = JsonType::Array;  return v; }
JsonPtr JsonValue::Obj()             { auto v = std::make_shared<JsonValue>(); v->type = JsonType::Object; return v; }

const JsonPtr* JsonValue::Get(const wchar_t* key) const {
    if (type != JsonType::Object) return nullptr;
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}
void JsonValue::Set(const wchar_t* key, JsonPtr v) {
    if (type != JsonType::Object) { type = JsonType::Object; }
    for (auto& kv : obj) if (kv.first == key) { kv.second = v; return; }
    obj.emplace_back(key, v);
}
void JsonValue::Push(JsonPtr v) { if (type != JsonType::Array) type = JsonType::Array; arr.push_back(v); }

int JsonValue::I(const wchar_t* k, int def) const {
    const JsonPtr* p = Get(k);
    if (!p || !*p) return def;
    if ((*p)->isNumber()) return (int)(*p)->num;
    if ((*p)->isBool())   return (*p)->b ? 1 : 0;
    if ((*p)->isString()) { try { return (int)std::wcstol((*p)->str.c_str(), nullptr, 10); } catch (...) { return def; } }
    return def;
}
double JsonValue::D(const wchar_t* k, double def) const {
    const JsonPtr* p = Get(k);
    if (!p || !*p) return def;
    if ((*p)->isNumber()) return (*p)->num;
    if ((*p)->isString()) { wchar_t* end = nullptr; double v = std::wcstod((*p)->str.c_str(), &end); return (end && *end == 0) ? v : def; }
    return def;
}
bool JsonValue::B(const wchar_t* k, bool def) const {
    const JsonPtr* p = Get(k);
    if (!p || !*p) return def;
    if ((*p)->isBool())   return (*p)->b;
    if ((*p)->isNumber()) return (*p)->num != 0;
    if ((*p)->isString()) {
        std::wstring s = (*p)->str;
        for (auto& c : s) c = (wchar_t)towlower(c);
        if (s == L"false" || s == L"0" || s == L"no" || s == L"off" || s.empty()) return false;
        return true;                      // 与 PyCom 的宽松真值转换一致
    }
    return def;
}
std::wstring JsonValue::S(const wchar_t* k, const std::wstring& def) const {
    const JsonPtr* p = Get(k);
    if (!p || !*p) return def;
    if ((*p)->isString()) return (*p)->str;
    if ((*p)->isNumber()) { wchar_t buf[64]; std::swprintf(buf, 64, L"%g", (*p)->num); return buf; }
    return def;
}

// ------------------------------- 序列化 -------------------------------
static void DumpStr(const std::wstring& s, std::wstring& out) {
    out.push_back(L'"');
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            case L'\b': out += L"\\b";  break;
            case L'\f': out += L"\\f";  break;
            default:
                if (c < 0x20) { wchar_t b[8]; std::swprintf(b, 8, L"\\u%04x", (unsigned)c); out += b; }
                else out.push_back(c);
        }
    }
    out.push_back(L'"');
}
static void DumpNum(double v, std::wstring& out) {
    wchar_t buf[64];
    if (v == (double)(long long)v && v < 9.0e15 && v > -9.0e15) std::swprintf(buf, 64, L"%lld", (long long)v);
    else std::swprintf(buf, 64, L"%.17g", v);
    out += buf;
}
static void DumpValue(const JsonValue* v, std::wstring& out, int indent, int depth) {
    const std::wstring pad(indent * (depth + 1), L' ');
    const std::wstring padEnd(indent * depth, L' ');
    switch (v->type) {
        case JsonType::Null:   out += L"null"; break;
        case JsonType::Bool:   out += v->b ? L"true" : L"false"; break;
        case JsonType::Number: DumpNum(v->num, out); break;
        case JsonType::String: DumpStr(v->str, out); break;
        case JsonType::Array:
            if (v->arr.empty()) { out += L"[]"; break; }
            out += L"[\n";
            for (size_t i = 0; i < v->arr.size(); ++i) {
                out += pad; DumpValue(v->arr[i].get(), out, indent, depth + 1);
                if (i + 1 < v->arr.size()) out.push_back(L',');
                out.push_back(L'\n');
            }
            out += padEnd; out += L"]";
            break;
        case JsonType::Object:
            if (v->obj.empty()) { out += L"{}"; break; }
            out += L"{\n";
            for (size_t i = 0; i < v->obj.size(); ++i) {
                out += pad; DumpStr(v->obj[i].first, out); out += L": ";
                DumpValue(v->obj[i].second.get(), out, indent, depth + 1);
                if (i + 1 < v->obj.size()) out.push_back(L',');
                out.push_back(L'\n');
            }
            out += padEnd; out += L"}";
            break;
    }
}
std::wstring JsonValue::Dump(int indent) const { std::wstring out; DumpValue(this, out, indent, 0); return out; }

// ------------------------------- 解析 -------------------------------
namespace {
struct Parser {
    const wchar_t* p; const wchar_t* end; std::wstring err;
    // §99（审查 U1）：递归深度上限——深嵌套配置曾致启动即栈溢出崩溃（0xC00000FD 实测）。
    // 真实配置最深 2 层（font 对象 / multisend_rows 数组），64 层余量极大。
    static constexpr int kMaxDepth = 64;

    void SkipWs() { while (p < end && (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\r')) ++p; }
    bool Fail(const wchar_t* what) { if (err.empty()) { err = what; } return false; }

    bool ParseValue(JsonPtr& out) { return ParseValue(out, 0); }
    bool ParseValue(JsonPtr& out, int depth) {
        SkipWs();
        if (p >= end) return Fail(L"意外的结尾");
        if (depth > kMaxDepth) return Fail(L"嵌套过深");
        wchar_t c = *p;
        if (c == L'{') return ParseObject(out, depth);
        if (c == L'[') return ParseArray(out, depth);
        if (c == L'"') { std::wstring s; if (!ParseString(s)) return false; out = JsonValue::Str(s); return true; }
        if (c == L't') { if (Match(L"true"))  { out = JsonValue::Bool(true);  return true; } return Fail(L"无效字面量"); }
        if (c == L'f') { if (Match(L"false")) { out = JsonValue::Bool(false); return true; } return Fail(L"无效字面量"); }
        if (c == L'n') { if (Match(L"null"))  { out = JsonValue::Null();       return true; } return Fail(L"无效字面量"); }
        return ParseNumber(out);
    }
    bool Match(const wchar_t* lit) {
        size_t n = std::wcslen(lit);
        if ((size_t)(end - p) < n) return false;
        for (size_t i = 0; i < n; ++i) if (p[i] != lit[i]) return false;
        p += n; return true;
    }
    bool ParseString(std::wstring& out) {
        if (p >= end || *p != L'"') return Fail(L"期望字符串");
        ++p;
        while (p < end) {
            wchar_t c = *p++;
            if (c == L'"') return true;
            if (c == L'\\') {
                if (p >= end) return Fail(L"转义不完整");
                wchar_t e = *p++;
                switch (e) {
                    case L'"':  out.push_back(L'"');  break;
                    case L'\\': out.push_back(L'\\'); break;
                    case L'/':  out.push_back(L'/');  break;
                    case L'b':  out.push_back(L'\b'); break;
                    case L'f':  out.push_back(L'\f'); break;
                    case L'n':  out.push_back(L'\n'); break;
                    case L'r':  out.push_back(L'\r'); break;
                    case L't':  out.push_back(L'\t'); break;
                    case L'u': {
                        unsigned v = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (p >= end) return Fail(L"\\u 不完整");
                            wchar_t h = *p++;
                            v <<= 4;
                            if (h >= L'0' && h <= L'9') v |= (unsigned)(h - L'0');
                            else if (h >= L'a' && h <= L'f') v |= (unsigned)(h - L'a' + 10);
                            else if (h >= L'A' && h <= L'F') v |= (unsigned)(h - L'A' + 10);
                            else return Fail(L"\\u 非法十六进制");
                        }
                        out.push_back((wchar_t)v);
                        break;
                    }
                    default: return Fail(L"未知转义");
                }
            } else {
                out.push_back(c);
            }
        }
        return Fail(L"字符串未闭合");
    }
    bool ParseNumber(JsonPtr& out) {
        const wchar_t* start = p;
        if (p < end && (*p == L'-' || *p == L'+')) ++p;
        bool digits = false;
        while (p < end && ((*p >= L'0' && *p <= L'9') || *p == L'.' || *p == L'e' || *p == L'E' || *p == L'-' || *p == L'+')) {
            if (*p >= L'0' && *p <= L'9') digits = true;
            ++p;
        }
        if (!digits) return Fail(L"期望数字");
        std::wstring s(start, p);
        out = JsonValue::Num(std::wcstod(s.c_str(), nullptr));
        return true;
    }
    bool ParseArray(JsonPtr& out, int depth) {
        ++p;                                     // '['
        out = JsonValue::Arr();
        SkipWs();
        if (p < end && *p == L']') { ++p; return true; }
        for (;;) {
            JsonPtr v;
            if (!ParseValue(v, depth + 1)) return false;
            out->Push(v);
            SkipWs();
            if (p < end && *p == L',') { ++p; continue; }
            if (p < end && *p == L']') { ++p; return true; }
            return Fail(L"数组未闭合");
        }
    }
    bool ParseObject(JsonPtr& out, int depth) {
        ++p;                                     // '{'
        out = JsonValue::Obj();
        SkipWs();
        if (p < end && *p == L'}') { ++p; return true; }
        for (;;) {
            SkipWs();
            std::wstring key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (p >= end || *p != L':') return Fail(L"期望冒号");
            ++p;
            JsonPtr v;
            if (!ParseValue(v, depth + 1)) return false;
            out->Set(key.c_str(), v);
            SkipWs();
            if (p < end && *p == L',') { ++p; continue; }
            if (p < end && *p == L'}') { ++p; return true; }
            return Fail(L"对象未闭合");
        }
    }
};
} // namespace

JsonPtr JsonValue::Parse(const std::wstring& text, std::wstring* err) {
    Parser ps{ text.c_str(), text.c_str() + text.size(), {} };
    if (!text.empty() && text[0] == 0xFEFF) { ++ps.p; }     // 容忍 BOM
    JsonPtr root;
    if (!ps.ParseValue(root)) { if (err) *err = ps.err; return nullptr; }
    if (err) err->clear();
    return root;
}

// ------------------------------- 文件 IO -------------------------------
bool ReadFileUtf8(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64LL << 20)) { CloseHandle(h); return false; }
    std::string bytes((size_t)sz.QuadPart, 0);
    DWORD got = 0;
    BOOL ok = ReadFile(h, bytes.data(), (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    bytes.resize(got);
    size_t off = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) off = 3;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, (int)(bytes.size() - off), nullptr, 0);
    if (wlen <= 0) { out.clear(); return true; }
    out.resize((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, (int)(bytes.size() - off), &out[0], wlen);
    return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::wstring& text) {
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes((size_t)(len > 0 ? len : 0), 0);
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &bytes[0], len, nullptr, nullptr);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = bytes.empty() ? TRUE : WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == bytes.size();
}

} // namespace sncom
