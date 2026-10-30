// sncom/src/platform/win_util.h —— Win32 平台小工具（DPI / 任务栏 / 字体）
// 注意：所有 Win8+ 才有的 API 一律用 GetProcAddress 动态取，保证 Win7 上 exe 能加载。
#pragma once
#include <windows.h>
#include <string>
#include "../core/settings.h"

namespace sncom {

void  EnableDpiAwareness();                      // PerMonitorV2（Win10 1703+）→ 回退 SetProcessDPIAware
void  TaskbarAddTab(HWND hwnd);                  // 建窗后主动上任务栏（ITaskbarList::AddTab；§137）
int   DpiForWindowSafe(HWND hwnd);               // GetDpiForWindow（Win10+）→ 否则系统 DPI
int   ScalePx(int designPx, int dpi);            // 设计像素 → 实际像素（96dpi 为基准）
HFONT CreateUiFont(const FontSpec& f, int dpi);  // 由配置字体创建 HFONT
HFONT CreateSystemUiFont(int dpi);               // 系统 UI 字体（NONCLIENTMETRICS.lfMessageFont，外壳用）
void  ApplyFontToChildren(HWND parent, HFONT f); // 递归 WM_SETFONT
bool  IsDarkModeEnabled();                       // 系统是否为深色应用模式（仅用于可能的外观适配留钩）

} // namespace sncom
