// sncom/src/platform/win_util.cpp
#include "win_util.h"
#include <shobjidl.h>      // ITaskbarList（§137 任务栏强制上栏）
#include <objbase.h>       // CoInitializeEx / CoCreateInstance（§137）

namespace sncom {

typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

void EnableDpiAwareness() {
    // 优先 PerMonitorV2（Win10 1703+）；manifest 里也声明了，这里是 Win7 的兜底路径
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (pfn && pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
        auto pfn = (PFN_SetProcessDpiAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (pfn) { pfn(2 /*PROCESS_PER_MONITOR_DPI_AWARE*/); FreeLibrary(shcore); return; }
        FreeLibrary(shcore);
    }
    SetProcessDPIAware();                       // Vista/Win7 兜底
}

// §137：任务栏按钮强制上栏。shell 对新窗口的按钮创建是异步的、且偶发迟滞（R25 期间实测
// 秒级~长时间不出现），建窗后由应用主动调一次 ITaskbarList::AddTab，窗口即刻进任务栏；
// 对 shell 已自行加过按钮的窗口是幂等操作。同时不再设置未注册的进程 AUMID（无开始菜单
// 快捷方式可解析，只会让 shell 多走一条回退路径）——任务栏分组按 exe 默认口径，组名取
// exe 版本资源 FileDescription。
// CoInitializeEx 无配对 CoUninitialize：UI 线程存活期=进程存活期。
void TaskbarAddTab(HWND hwnd) {
    if (!hwnd) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // {56FDF344-FD6D-11d0-958A-006097C9A090}=CLSID_TaskbarList；{56FDF342-…}=IID_ITaskbarList
    static const CLSID kTaskbarList =
        {0x56FDF344, 0xFD6D, 0x11D0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
    static const IID kITaskbarList =
        {0x56FDF342, 0xFD6D, 0x11D0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
    ITaskbarList* tl = nullptr;
    if (SUCCEEDED(CoCreateInstance(kTaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                   kITaskbarList, (void**)&tl)) && tl) {
        if (SUCCEEDED(tl->HrInit())) tl->AddTab(hwnd);   // HrInit 先行（接口约定）
        tl->Release();
    }
}

int DpiForWindowSafe(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto pfn = (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (pfn && hwnd) { UINT d = pfn(hwnd); if (d >= 48 && d <= 480) return (int)d; }
    }
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96;
}

int ScalePx(int designPx, int dpi) { return MulDiv(designPx, dpi, 96); }

HFONT CreateUiFont(const FontSpec& f, int dpi) {
    LOGFONTW lf{};
    lf.lfHeight  = -MulDiv(f.size, dpi, 72);                 // 磅 → 逻辑高
    lf.lfWeight  = f.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic    = f.italic ? TRUE : FALSE;
    lf.lfUnderline = f.underline ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, f.family.c_str(), LF_FACESIZE);
    HFONT hf = CreateFontIndirectW(&lf);
    if (hf) return hf;
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

// 系统 UI 字体（外壳观感对齐 PyCom/Qt 默认）：Win10 1607+ 用 SystemParametersInfoForDpi，
// 否则 SystemParametersInfoW + 按 DPI 换算；两者都失败回 DEFAULT_GUI_FONT。
HFONT CreateSystemUiFont(int dpi) {
    typedef BOOL (WINAPI *PFN_SystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    bool ok = false;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto pfn = (PFN_SystemParametersInfoForDpi)GetProcAddress(user32, "SystemParametersInfoForDpi");
        if (pfn) ok = pfn(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)dpi) != 0;
    }
    if (!ok) {
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) != 0;
        if (ok && dpi > 0 && dpi != 96)
            ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, dpi, 96);
    }
    if (!ok) return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    HFONT hf = CreateFontIndirectW(&ncm.lfMessageFont);
    return hf ? hf : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static BOOL CALLBACK SetFontProc(HWND child, LPARAM lp) {
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, MAKELPARAM(TRUE, 0));
    return TRUE;
}
void ApplyFontToChildren(HWND parent, HFONT f) {
    if (!f) return;
    EnumChildWindows(parent, SetFontProc, (LPARAM)f);
}

bool IsDarkModeEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD val = 1, sz = sizeof(val), type = 0;
    RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&val, &sz);
    RegCloseKey(key);
    return val == 0;
}

} // namespace sncom
