// sncom/src/core/msini.cpp —— 原版 ini 多条字符串解析核（逐条对齐 PyCom settings.py）
#include "msini.h"
#include <cwchar>

namespace sncom {

static std::wstring TrimWs(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

void ParseIniKeys(const std::wstring& text, std::vector<std::pair<std::wstring, std::wstring>>& keys) {
    keys.clear();
    // ← PyCom _read_ini_keys：line.strip() 后须含 '=' 且不以 ';' 开头；
    // 键 strip、值原样；同名键覆盖（dict：保首插位置、更新值）
    std::wstring line;
    auto flush = [&]() {
        std::wstring t = TrimWs(line);
        line.clear();
        if (t.empty() || t[0] == L';') return;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) return;
        std::wstring k = TrimWs(t.substr(0, eq));
        std::wstring v = t.substr(eq + 1);
        for (auto& kv : keys)
            if (kv.first == k) { kv.second = v; return; }
        keys.emplace_back(k, v);
    };
    for (wchar_t c : text) {
        if (c == L'\n') flush();
        else line += c;
    }
    flush();
}

// ← PyCom keys.get(k, "").strip().lstrip(",")：先 trim 空白，再只剥前导逗号
// （尾部逗号保留——"1,x,," 的尾部空字段进入 split 流程）
static std::wstring HeadValue(const std::vector<std::pair<std::wstring, std::wstring>>& keys,
                              const std::wstring& k) {
    for (const auto& kv : keys) {
        if (kv.first != k) continue;
        std::wstring v = TrimWs(kv.second);
        size_t c = v.find_first_not_of(L',');
        return (c == std::wstring::npos) ? L"" : v.substr(c);
    }
    return L"";
}

static int IniInt(const std::wstring& s, int def, int lo, int hi) {
    // ← PyCom int(s)：整段可解析才生效（"12x" 抛 ValueError → 默认；非 stoi 部分解析）
    if (s.empty()) return def;
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) return def;
        return v < lo ? lo : (v > hi ? hi : v);
    } catch (...) {
        return def;
    }
}

int ParseMsIniRows(const std::vector<std::pair<std::wstring, std::wstring>>& keys,
                   std::vector<MsIniRow>& out) {
    out.clear();
    auto tr = [](const std::wstring& s) {
        size_t a = s.find_first_not_of(L" \t");
        size_t b = s.find_last_not_of(L" \t");
        return (a == std::wstring::npos) ? L"" : s.substr(a, b - a + 1);
    };
    for (int i = 1; i <= 99; ++i) {
        std::wstring head = HeadValue(keys, L"N" + std::to_wstring(100 + i));
        std::wstring body = HeadValue(keys, L"N" + std::to_wstring(i));
        if (head.empty() && body.empty()) continue;
        // head.split(",")（head 为空 → parts=[]，PyCom 条件表达式口径）
        std::vector<std::wstring> parts;
        if (!head.empty()) {
            size_t p = 0;
            for (;;) {
                size_t c = head.find(L',', p);
                if (c == std::wstring::npos) { parts.push_back(tr(head.substr(p))); break; }
                parts.push_back(tr(head.substr(p, c - p)));
                p = c + 1;
            }
        }
        // bp = [p.strip() for p in body.split(",", 1)]：无逗号 → text 置空；
        // HEX 标签整段 == "H"（大小写不敏感）
        std::wstring tag, text;
        {
            size_t c1 = body.find(L',');
            if (c1 == std::wstring::npos) tag = tr(body);
            else { tag = tr(body.substr(0, c1)); text = tr(body.substr(c1 + 1)); }
        }
        MsIniRow m;
        m.hex = (_wcsicmp(tag.c_str(), L"H") == 0);
        m.text = text;
        m.interval = (parts.size() > 2 && !parts[2].empty()) ? IniInt(parts[2], 1000, 0, 3600000) : 1000;
        m.order = (!parts.empty() && !parts[0].empty()) ? IniInt(parts[0], 0, 0, 9999) : 0;
        m.note = parts.size() > 1 ? parts[1] : L"";
        out.push_back(m);
    }
    return (int)out.size();
}

} // namespace sncom
