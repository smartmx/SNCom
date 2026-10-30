// sncom/src/core/paths.cpp
#include "paths.h"
#include <windows.h>
namespace sncom {
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(MAX_PATH * 4));
    if (n == 0) return L".";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}
std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
std::wstring ConfigPath() { return JoinPath(ExeDir(), L"sncom.json"); }
}
