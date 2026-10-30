// sncom/src/ui/mainwindow.cpp —— 外壳 + M0.5 spike + M2-A 显示管线（LineBuilder/环形缓冲/转存/保存）
#include "mainwindow.h"
#include "layout.h"
#include "../core/paths.h"
#include "../core/hexutil.h"
#include "../core/bignum.h"          // §97：进制转换页任意精度（≥256 位）
#include "../core/msini.h"
#include "../platform/win_util.h"
#include <commctrl.h>
#include <strsafe.h>                 // §100：StringCchCopyW 取代弃用的 wcscpy/wcsncpy（消 C4996）
#include <commdlg.h>
#include <shellapi.h>                // ShellExecuteW（新窗口；WIN32_LEAN_AND_MEAN 不再带入）
#include <dbt.h>
#include <uxtheme.h>                 // §98：菜单栏/扩展窗页签去主题（SetWindowTheme/SetMenuInfo）
#include <windowsx.h>
#include <algorithm>
#include <cstdio>

namespace sncom {
using namespace layout;

// ---------------- 命令与控件 ID ----------------
enum : int {
    IDM_MODE_BASE = 2000,            // 串口/TCP客户端/TCP服务端/UDP（单选）
    IDM_ENC_BASE  = 2100,            // 12 个预置编码（单选；自定义编码菜单项已按用户拍板删除）
    IDM_PORT_REFRESH = 2200,         // 通讯端口：刷新串口列表
    IDM_SERIAL_RECONNECT, IDM_SERIAL_OPENSTART, IDM_SERIAL_HELP,
    IDM_VIEW_FONTCOLOR, IDM_VIEW_BG, IDM_VIEW_BUF, IDM_VIEW_TS_COLOR, IDM_VIEW_AUTOSCROLL, IDM_VIEW_FIND,
    IDM_TERM_ENTER, IDM_TERM_KEY, IDM_TERM_ECHO, IDM_TERM_LF,
    IDM_SEND_FILEDELAY, IDM_SEND_NEWLINE, IDM_SEND_HELP,
    IDM_TOOL_CALC, IDM_TOOL_ASCII, IDM_TOOL_MS, IDM_TOOL_DEV, IDM_TOOL_EXPLORER,
    IDM_TOOL_LOOP,                          // M0.5：软件回环演示（无硬件验证收发链路）
    IDM_HELP_ABOUT,
    // §79：显式取值追加——插在中间会使后面隐式枚举的 id 全部平移（探针硬编码 2222 等）
    IDM_HELP_UPDATE = 2400,                 // 帮助→检查更新（打开发布页，应用内不联网）
    IDM_TERM_ENABLE = 2401,                 // §140：终端仿真总开关（默认关——接收区不响应键盘）

    IDM_BUF_BASE = 2300,                    // 显示缓冲上限档位（500K/1/5/20/50/100M，单选，默认 1M）

    IDC_BASE = 3000,                 // 控件（顺序 = 面板行序，对齐 PyCom）
    IDC_BTN_CLEAR = IDC_BASE, IDC_BTN_FIND, IDC_BTN_NEW, IDC_CHK_AUTOSCROLL,
    IDC_BTN_OPENFILE, IDC_EDIT_FILE, IDC_BTN_SENDFILE, IDC_BTN_STOPFILE, IDC_BTN_CLEARSEND,
    IDC_BTN_COLLAPSE, IDC_BTN_TOP, IDC_BTN_SAVEPARAMS, IDC_BTN_EXPAND,
    IDC_LBL_PORT, IDC_CMB_PORT, IDC_BTN_REFRESH, IDC_LED_LINK, IDC_BTN_OPEN,
    IDC_LBL_BAUD, IDC_CMB_BAUD, IDC_LBL_DATA, IDC_CMB_DATABITS, IDC_LBL_STOP, IDC_CMB_STOPBITS,
    IDC_LBL_PARITY, IDC_CMB_PARITY, IDC_LBL_FLOW, IDC_CMB_FLOW, IDC_CHK_DTR, IDC_CHK_RTS,
    IDC_LBL_LINE, IDC_PIN_DCD, IDC_PIN_DSR, IDC_PIN_CTS, IDC_PIN_RI, IDC_PIN_ERR, IDC_CHK_BREAK,
    IDC_CHK_HEXSHOW, IDC_BTN_SAVEDATA, IDC_CHK_RXFILE, IDC_CHK_SPLIT, IDC_CHIP_TS,
    IDC_LBL_TIMEOUT, IDC_EDIT_SPLITMS, IDC_LBL_MS,
    IDC_CHK_HEXSEND, IDC_CHK_TIMED, IDC_EDIT_INTERVAL, IDC_LBL_TIMEDUNIT, IDC_CHK_NEWLINE,
    IDC_LBL_CHKPRE, IDC_EDIT_CHKSTART, IDC_LBL_CHKSUF, IDC_CMB_CHKTAIL, IDC_LBL_CHKLBL,
    IDC_CMB_CHECKSUM, IDC_EDIT_CHKVIEW,
    IDC_EDIT_SEND, IDC_BTN_SEND,
    IDC_RXVIEW, IDC_PROGRESS,

    // 网络连接区（模式≠串口时显示）
    IDC_LED_NET, IDC_LBL_NETMODE, IDC_LBL_REMOTE, IDC_EDIT_RIP, IDC_EDIT_RPORT,
    IDC_BTN_NCONN, IDC_BTN_NDISC, IDC_LBL_NIC, IDC_CMB_NIC, IDC_BTN_NICREF,
    IDC_LBL_LOCAL, IDC_CMB_LIP, IDC_EDIT_LPORT,

    // 查找条（D4；初始隐藏）
    IDC_FIND_EDIT, IDC_FIND_CASE, IDC_FIND_HEX, IDC_FIND_COUNT,
    IDC_FIND_PREV, IDC_FIND_NEXT,
                       // IDC_FIND_CLOSE 已删（§89：查找页不需要独立 ×，扩展窗自身有关闭）
    // §77：追加在末尾并显式取值——中间插入会使后续所有控件 id 平移（探针/映射全崩）；
    // 3099 避开 3074..3080（查找条）与 3081（§73 已删的 BRK 框，探针断言其必须为空）
    IDC_RX_TINT = 3099,                     // RX 段染色覆盖窗（ownerdraw STATIC，仅 ≥60% 时可见）
};


// 与 PyCom settings.py 的 ENCODINGS 完全一致（顺序与取值都比对）
struct Enc { const wchar_t* label; const wchar_t* value; };
static const Enc kEncodings[] = {
    { L"GBK (简体中文)",     L"gbk"      },
    { L"GB2312 (简体中文)",  L"gb2312"   },
    { L"GB18030",           L"gb18030"  },
    { L"UTF-8",             L"utf-8"    },
    { L"UTF-16LE",          L"utf-16-le"},
    { L"UTF-16BE",          L"utf-16-be"},
    { L"Big5 (繁体中文)",    L"big5"     },
    { L"Shift-JIS (日文)",   L"shift_jis"},
    { L"EUC-JP (日文)",      L"euc_jp"   },
    { L"EUC-KR (韩文)",      L"euc_kr"   },
    { L"Windows-1252 (西欧)",L"cp1252"   },
    { L"Latin-1 (字节直通)", L"latin-1"  },
};
static const int kEncCount = (int)(sizeof(kEncodings) / sizeof(kEncodings[0]));

// 校验算法（文字与顺序与 PyCom 的 cmb_checksum 完全一致）
static const wchar_t* kChecksumLabels[] = { L"无校验", L"ModbusCRC16", L"CCITT_CRC16", L"CRC8", L"XOR8", L"ADD8", L"ADD16", L"CRC32", L"ADD8_0" };
static const wchar_t* kChecksumValues[] = { L"none", L"modbus_crc16", L"ccitt_crc16", L"crc8", L"xor8", L"add8", L"add16", L"crc32", L"add8_0" };
static const int kChecksumCount = 9;   // §77：+CRC32/+ADD8_0（原版 V5.09a2「0-ADD」）
// §79：版本号单一来源（原先散落三处字面量，改版易漏）。用宏以便字面量拼接
// （About 里 `L"版本：" SNCOM_VER_N` 需要预处理期拼接，const 变量做不到）
// §109：版本号来源收敛到 src/version.h（sncom.rc 的 VERSIONINFO 同一文件）——
// 数字/字符串/二进制资源/界面四者一致性由 probes/verify_r109.py 守卫。
#include "../version.h"
#define SNCOM_VER_W  L"V" SNCOM_VERSION_STR3   // 状态栏显示 / 段宽测量
#define SNCOM_VER_N  L"" SNCOM_VERSION_STR3    // 关于框正文
// 线路引脚指示（对齐 PyCom：DCD/DSR/CTS/RI/ERR）
static const wchar_t* kPinNames[] = { L"DCD", L"DSR", L"CTS", L"RI", L"ERR" };   // §76：BRK 并入 BREAK 复选框
static const int kPinCount = 5;
// 多条字符串表列宽（设计 px；末列由 MsStretchLastColumn 拉伸填满右缘，见 §78）
static const int kMsColW[6] = { 60, 36, 90, 38, 48, 110 };
// 校验插入档位（与 PyCom 的 cmb_chk_place 逐字一致：闭合态收窄，弹出列表加宽）
static const wchar_t* kTailPlaces[] = {
    L"第1.末尾1字节是帧尾,校验插帧尾前",
    L"第2.末尾2字节是帧尾,校验插帧尾前",
    L"第3.末尾3字节是帧尾,校验插帧尾前",
    L"第4.末尾4字节是帧尾,校验插帧尾前",
    L"无帧尾追加校验",   // §56：原「末尾追加校验[旧版无帧尾]」（用户拍板改名）
};
static const int kTailPlaceCount = 5;

static const wchar_t* kModes[]  = { L"串口", L"TCP 客户端", L"TCP 服务端", L"UDP" };
static const wchar_t* kModeVals[] = { L"serial", L"tcp_client", L"tcp_server", L"udp" };


// 方向标记由 display.h DirMarker 提供（"收←◆"/"发→◇"，前缀自带尾随空格，对齐 PyCom 拼接口径）

bool MainWindow::RegisterClasses(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SNComMain";
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!RegisterClassExW(&wc)) return false;
    return RxView::RegisterClass(hInst);      // 自绘接收视图（M0.5 spike ①②）
}

HWND MainWindow::Create(HINSTANCE hInst, int nCmdShow, bool selftest) {
    auto* self = new MainWindow();
    self->hInst_ = hInst;
    self->cfgPath_ = ConfigPath();
    self->cfg_.Load(self->cfgPath_);          // 失败则用默认值；损坏文件已改名 .bad 备份（§99），建窗后提示
    self->selftest_ = selftest;

    int w = self->cfg_.win_w, h = self->cfg_.win_h;
    HWND hwnd = CreateWindowExW(0, L"SNComMain", L"SNCom 串口/网络数据调试器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, hInst, self);
    if (!hwnd) { delete self; return nullptr; }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    return hwnd;
}

LRESULT CALLBACK MainWindow::NoWheelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR) {
    // 用户反馈：输入类控件禁用鼠标滚轮（滚轮在组合框上会连续切换选项）——只允许手动调整。
    if (msg == WM_MOUSEWHEEL) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

void MainWindow::DisableComboWheel(HWND combo) {
    SetWindowSubclass(combo, NoWheelProc, 1, 0);
    HWND ed = GetWindow(combo, GW_CHILD);              // DROPDOWN 的编辑子框（焦点所在）
    if (ed) SetWindowSubclass(ed, NoWheelProc, 1, 0);
}

// ---------- 波特率「自定义」输入小窗（SSCOM 对齐，非模态 + 主窗禁用；§55 用户拍板恢复） ----------
void MainWindow::BaudApply(int v) {
    wchar_t t[16];
    swprintf(t, 16, L"%d", v);
    int idx = (int)SendMessageW(cmb_baud_, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)t);
    if (idx < 0) idx = (int)SendMessageW(cmb_baud_, CB_INSERTSTRING, baudCustomIdx_ + 1, (LPARAM)t);
    SendMessageW(cmb_baud_, CB_SETCURSEL, idx, 0);
    baudLastValid_ = t;
    if (engine_.IsOpen() && !loopMode_) Notify(L"波特率已切换（串口已自动重开）");
    ReopenSerial();
}

LRESULT CALLBACK MainWindow::BaudDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        auto mk = [&](const wchar_t* cls, DWORD st, const wchar_t* txt, int id, int x, int y, int w, int hh) {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st,
                                     x, y, w, hh, h, (HMENU)(INT_PTR)id, self->hInst_, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)self->uiFont_, TRUE);
            return c;
        };
        mk(L"STATIC", 0, L"波特率", 0, 12, 14, 56, 20);
        self->baudEd_ = mk(L"EDIT", WS_BORDER | ES_NUMBER, self->baudLastValid_.c_str(), 1001, 72, 12, 120, 22);
        mk(L"BUTTON", BS_DEFPUSHBUTTON, L"确定", 1002, 72, 44, 56, 24);
        mk(L"BUTTON", BS_PUSHBUTTON, L"取消", 1003, 136, 44, 56, 24);
        SetFocus(self->baudEd_);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1002) {                              // 确定：校验（300~50000000）→ 应用 → 关窗
            int v = GetDlgItemInt(h, 1001, nullptr, FALSE);
            if (v < 300 || v > 50000000) {
                SetDlgItemTextW(h, 1001, self->baudLastValid_.c_str());
                return 0;
            }
            self->BaudApply(v);
            DestroyWindow(h);
        } else if (id == 1003) {                       // 取消：恢复上一有效值显示
            int idx = (int)SendMessageW(self->cmb_baud_, CB_FINDSTRINGEXACT,
                                        (WPARAM)-1, (LPARAM)self->baudLastValid_.c_str());
            if (idx < 0) idx = self->baudCustomIdx_;
            SendMessageW(self->cmb_baud_, CB_SETCURSEL, idx, 0);
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        EnableWindow(self->hwnd_, TRUE);
        if (self->baudDlg_ == h) self->baudDlg_ = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void MainWindow::OpenBaudInput() {
    if (baudDlg_) { SetForegroundWindow(baudDlg_); return; }
    EnableWindow(hwnd_, FALSE);                        // 伪模态：主窗禁用
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = BaudDlgProc;
        wc.hInstance = hInst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SNComBaudDlg";
        RegisterClassW(&wc);
        reg = true;
    }
    baudDlg_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"SNComBaudDlg", L"自定义波特率",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, ScalePx(220, dpi_), ScalePx(110, dpi_),
                               hwnd_, nullptr, hInst_, this);
    if (!baudDlg_) {                                   // P3-5：建窗失败必须回滚主窗可用
        EnableWindow(hwnd_, TRUE);
        Notify(L"自定义波特率弹窗创建失败");
        return;
    }
    ShowWindow(baudDlg_, SW_SHOW);
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (MainWindow*)cs->lpCreateParams;
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

// 下拉列表（CBS_DROPDOWNLIST）必须用 CB_SETCURSEL 选中项——SetWindowText 不生效
// "#RRGGBB" → COLORREF（无效则用默认）
COLORREF MainWindow::ParseColor(const std::wstring& s, COLORREF def) {
    if (s.size() == 7 && s[0] == L'#') {
        auto hexv = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        int v[6];
        for (int i = 0; i < 6; ++i) { v[i] = hexv(s[1 + i]); if (v[i] < 0) return def; }
        return RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
    }
    return def;
}
static void SelectComboText(HWND cb, const wchar_t* text) {
    if (!cb || !text) return;
    int i = (int)SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)text);
    if (i < 0) i = 0;
    SendMessageW(cb, CB_SETCURSEL, (WPARAM)i, 0);
}
// §78（用户拍板）：单选组（通讯端口/编码/显示缓冲上限）也用系统自带 ✓ 标记——
// CheckMenuRadioItem 会打 MFT_RADIOCHECK（圆点），改用逐项 CheckMenuItem 打勾/取消。
static void CheckOneOf(HMENU m, UINT first, UINT last, UINT target) {
    if (!m) return;
    for (UINT id = first; id <= last; ++id)
        CheckMenuItem(m, id, MF_BYCOMMAND | (id == target ? MF_CHECKED : MF_UNCHECKED));
}
// ---------------- 发送内容框：空内容提示（§54 改四行） + 右键转换菜单 ----------------
static const wchar_t* kSendHint =
    L"发送内容：\r\n"
    L"ASCII：123\r\n"
    L"HEX:   31 32 33\r\n"
    L"选中指定内容右键可转为HEX/字符\r\n"
    L"Ctrl + Enter 发送";
static LRESULT CALLBACK SendEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR idSub, DWORD_PTR refData) {
    switch (msg) {
    case WM_PAINT: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        if (GetWindowTextLengthW(h) == 0) {
            HDC dc = GetDC(h);
            RECT rc{}; GetClientRect(h, &rc);
            rc.left += 2;
            rc.right -= 2;
            rc.top += 2;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0x78, 0x78, 0x78));
            HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
            HFONT old = f ? (HFONT)SelectObject(dc, f) : nullptr;
            DrawTextW(dc, kSendHint, -1, &rc, DT_LEFT | DT_TOP | DT_NOPREFIX);
            if (old) SelectObject(dc, old);
            ReleaseDC(h, dc);
        }
        return r;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN && GetKeyState(VK_CONTROL) < 0) {     // Ctrl+Enter 发送（M3）
            auto* self = (MainWindow*)refData;
            if (self) self->SendCurrentEdit();
            return 0;
        }
        break;
    case WM_CHAR:
        // §75/§79：Ctrl+Enter 发送后 TranslateMessage 仍会派生 WM_CHAR 插入换行——只吞
        // KEYDOWN 不够。**关键：Ctrl 修饰下的 Enter 派生的是 LF(0x0A) 而非 CR(0x0D)**
        //（§75 只吞 '\r'，用户仍见一个回车进入编辑框；§79 定位补吞 '\n'）。
        if ((wp == L'\r' || wp == L'\n') && GetKeyState(VK_CONTROL) < 0) return 0;
        break;
    case WM_CONTEXTMENU:                             // 编辑框不产生此消息（右键私有处理）——
        break;                                       // 真实入口在下方 WM_RBUTTONUP 拦截
    case WM_RBUTTONUP: {                             // §54：右键菜单（标准编辑 + 选区转换）
        auto* self = (MainWindow*)refData;
        if (!self) break;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(h, &pt);
        self->SendContextMenu(pt);
        return 0;                                    // 吞掉：不进 DefSubclassProc（否则弹出编辑框默认菜单）
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_KEYUP: case WM_PASTE:
        InvalidateRect(h, nullptr, TRUE);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------- 控件工厂 ----------------
void MainWindow::MakeButton(HWND& h, int id, const wchar_t* text, DWORD extra) {
    h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | extra,
                        0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
}
void MainWindow::MakeCheck(HWND& h, int id, const wchar_t* text, bool checked) {
    h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                        0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
    SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
void MainWindow::MakeCombo(HWND& h, int id, DWORD extra) {
    h = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | extra,
                        0, 0, 10, 200, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
    SetWindowSubclass(h, NoWheelProc, 1, 0);     // 禁滚轮（§44；必须在创建处做——批量循环曾
                                                 // 跑在创建前、句柄为空漏掉 4 个下拉，§56 修复）
}
void MainWindow::MakeEdit(HWND& h, int id, const wchar_t* text, DWORD extra) {
    h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
                        0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
}
void MainWindow::MakeLabel(HWND& h, int id, const wchar_t* text) {
    h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
}
// 线路引脚指示框（对齐 PyCom 的 pin_leds：小方块居中文字）
void MainWindow::MakePin(HWND& h, int id, const wchar_t* name) {
    h = CreateWindowExW(0, L"STATIC", name,
                        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                        0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
}

// ---------------- 度量（外壳字体实测：M1 标定口径，不照抄 Qt 常量） ----------------
int MainWindow::TextW(const wchar_t* s) const {
    if (!s || !hwnd_) return 0;
    HDC dc = GetDC(hwnd_);
    if (!dc) return 0;
    HFONT f = uiFont_ ? uiFont_ : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old = (HFONT)SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    return sz.cx;
}
int MainWindow::BtnW(const wchar_t* s) const { return TextW(s) + ScalePx(kBtnPadX, dpi_) + 6; }
int MainWindow::ChkW(const wchar_t* s) const { return TextW(s) + ScalePx(kChkBox, dpi_); }


// ---------------- 菜单 ----------------
void MainWindow::BuildMenus() {
    HMENU mb = CreateMenu();

    // 通讯端口：模式四项（单选）——对齐 PyCom（续修5：删「刷新串口列表」项——用户拍板，
    // 刷新走工具行「刷新」按钮；IDM_PORT_REFRESH=2200 枚举保留不另作他用）
    // §70：菜单一律不带快捷键标示（用户拍板，含顶层与各项的 (&X) 助记符）
    HMENU mMode = CreatePopupMenu();
    for (int i = 0; i < 4; ++i) AppendMenuW(mMode, MF_STRING, IDM_MODE_BASE + i, kModes[i]);
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mMode, L"通讯端口");

    // 串口设置
    HMENU mSerial = CreatePopupMenu();
    AppendMenuW(mSerial, MF_STRING, IDM_SERIAL_RECONNECT, L"断线自动重连(串口/网络)");
    AppendMenuW(mSerial, MF_STRING, IDM_SERIAL_OPENSTART, L"启动立刻打开串口");
    AppendMenuW(mSerial, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mSerial, MF_STRING, IDM_SERIAL_HELP, L"帮助…");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mSerial, L"串口设置");

    // 编码：12 预置（单选，取值与 PyCom ENCODINGS 完全一致；自定义编码已删——用户拍板只支持 12 种）
    HMENU mEnc = CreatePopupMenu();
    for (int i = 0; i < kEncCount; ++i) AppendMenuW(mEnc, MF_STRING, IDM_ENC_BASE + i, kEncodings[i].label);
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mEnc, L"编码");

    // 显示：字体与颜色统一一份（裁剪项：无发送/接收字体分区、无深色模式）
    HMENU mView = CreatePopupMenu();
    AppendMenuW(mView, MF_STRING, IDM_VIEW_FONTCOLOR, L"字体与颜色…");
    AppendMenuW(mView, MF_STRING, IDM_VIEW_BG, L"背景颜色…");   // §67：独立背景色入口
    HMENU mBuf = CreatePopupMenu();        // 缓冲上限档位（单选；口径：主线默认10M/上限100M/MIN500K）
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 0,     L"500 KB");
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 1,     L"1 MB（默认）");
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 2,     L"5 MB");
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 3,     L"20 MB");
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 4,     L"50 MB");
    AppendMenuW(mBuf, MF_STRING, IDM_BUF_BASE + 5,     L"100 MB");
    AppendMenuW(mView, MF_POPUP, (UINT_PTR)mBuf, L"显示缓冲上限");
    AppendMenuW(mView, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mView, MF_STRING, IDM_VIEW_TS_COLOR,   L"加时间戳和分包显示");
    AppendMenuW(mView, MF_STRING, IDM_VIEW_AUTOSCROLL, L"自动滚屏");
    AppendMenuW(mView, MF_STRING, IDM_VIEW_FIND,       L"查找");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mView, L"显示");

    // 发送：终端仿真设置 / 发送文件延时设置 / 加回车换行 / 帮助
    HMENU mSend = CreatePopupMenu();
    HMENU mTerm = CreatePopupMenu();
    AppendMenuW(mTerm, MF_STRING, IDM_TERM_ENTER, L"回车后发送新行(带回显)");
    AppendMenuW(mTerm, MF_STRING, IDM_TERM_KEY,   L"按键立即发送该键值");
    AppendMenuW(mTerm, MF_STRING, IDM_TERM_ECHO,  L"本地回显按键值");
    AppendMenuW(mTerm, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mTerm, MF_STRING, IDM_TERM_LF,    L"发送回车0x0d后,自动加上换行符0x0a");
    AppendMenuW(mSend, MF_STRING, IDM_TERM_ENABLE, L"启用终端仿真功能");   // §140：总开关（勾选后子菜单才可选）
    AppendMenuW(mSend, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mSend, MF_POPUP, (UINT_PTR)mTerm, L"终端仿真设置");
    mTermMenu_ = mTerm;                    // §140：句柄存成员（SyncMenuChecks 定位灰/亮用）
    AppendMenuW(mSend, MF_STRING, IDM_SEND_FILEDELAY, L"发送文件延时设置…");
    AppendMenuW(mSend, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mSend, MF_STRING, IDM_SEND_NEWLINE, L"加回车换行");
    AppendMenuW(mSend, MF_STRING, IDM_SEND_HELP,    L"帮助…");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mSend, L"发送");

    // 小工具：扩展区页签直达三项 + 外部工具三项（对齐 PyCom；无独立「多条字符串」菜单）
    HMENU mTool = CreatePopupMenu();
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_CALC,    L"进制转换");
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_ASCII,   L"ASCII码表");
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_MS,      L"多条字符串发送");
    AppendMenuW(mTool, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_DEV,      L"设备管理器");
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_EXPLORER, L"文件资源管理器");
    AppendMenuW(mTool, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mTool, MF_STRING, IDM_TOOL_LOOP, L"回环演示(软件回环)");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mTool, L"小工具");

    HMENU mHelp = CreatePopupMenu();
    AppendMenuW(mHelp, MF_STRING, IDM_HELP_UPDATE, L"检查更新…");     // §79：打开发布页（应用内不联网）
    AppendMenuW(mHelp, MF_STRING, IDM_HELP_ABOUT, L"关于 SNCom");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mHelp, L"帮助");

    SetMenu(hwnd_, mb);
    // §98b（用户拍板：只把菜单栏文字行的底改灰，**标题栏保持系统原样**）：不用
    // SetWindowTheme(hwnd,L"",L"")——本机实测那会把标题栏一并回退 classic（浅蓝老式按钮）。
    // 改法见 Handle 的 WM_NCPAINT：系统画完 NC 后，仅对"菜单栏矩形内的浅色底像素"做
    // 颜色替换（白→BTNFACE 240），文字/热键高亮等非浅色像素不动。
}


// ---------------- 控件树 ----------------
void MainWindow::BuildChildren() {
    // ---- 工具行（位置见 Layout：在接收显示框之下、底部面板之上，对齐 PyCom V0.0.5）----
    MakeButton(btn_clear_,      IDC_BTN_CLEAR,      L"清除窗口");
    MakeButton(btn_find_,       IDC_BTN_FIND,       L"查找");
    MakeButton(btn_new_,        IDC_BTN_NEW,        L"新窗口");
    MakeCheck(chk_autoscroll_,  IDC_CHK_AUTOSCROLL, L"自动滚屏", cfg_.auto_scroll);
    MakeButton(btn_openfile_,   IDC_BTN_OPENFILE,   L"打开文件");
    MakeEdit(edit_file_,        IDC_EDIT_FILE,      L"");
    SendMessageW(edit_file_, EM_SETCUEBANNER, TRUE, (LPARAM)L"发送文件路径");
    MakeButton(btn_sendfile_,   IDC_BTN_SENDFILE,   L"发送文件");
    MakeButton(btn_stopfile_,   IDC_BTN_STOPFILE,   L"停止");
    MakeButton(btn_clearsend_,  IDC_BTN_CLEARSEND,  L"清发送区");
    MakeButton(btn_collapse_,   IDC_BTN_COLLAPSE,   L"收起", BS_AUTOCHECKBOX | BS_PUSHLIKE);
    MakeButton(btn_top_,        IDC_BTN_TOP,        L"最前", BS_AUTOCHECKBOX | BS_PUSHLIKE);
    MakeButton(btn_saveparams_, IDC_BTN_SAVEPARAMS, L"保存参数");
    MakeButton(btn_expand_,     IDC_BTN_EXPAND,     L"扩展", BS_AUTOCHECKBOX | BS_PUSHLIKE);

    // ---- 自绘接收视图（M0.5 spike + M2-A 管线）----
    rxv_.Create(hwnd_, IDC_RXVIEW, hInst_);
    rxv_.SetFollowSink(this, &MainWindow::FollowThunk);
    rxv_.SetMenuSinks(this, &MainWindow::AnnoThunk, &MainWindow::ClearThunk);
    rxv_.SetTermSink(this, &MainWindow::TermSinkThunk);       // §110：终端输入 → TermSend
    rxv_.copy_with_anno = cfg_.copy_with_anno;
    rxv_.save_with_anno = cfg_.save_with_anno;
    ApplyViewColors();
    // 进度条（发送文件时显示；M0 常隐）
    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD,
                                0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_PROGRESS, hInst_, nullptr);

    // ---- 左列：网络连接区（模式≠串口时显示；行序对齐 PyCom 网络面板）----
    // 主窗 WM_CREATE 阶段尚不可见，先建后由 ApplyModeColumns 隐藏（无闪现）
    led2_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                            0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_LED_NET, hInst_, nullptr);
    MakeLabel(lbl_netmode_, IDC_LBL_NETMODE, L"TCP 客户端");
    MakeLabel(lbl_remote_, IDC_LBL_REMOTE, L"远程");
    MakeEdit(edit_rip_, IDC_EDIT_RIP, cfg_.remote_ip.c_str());
    SendMessageW(edit_rip_, EM_SETCUEBANNER, TRUE, (LPARAM)L"远程 IP / 主机名");
    MakeEdit(edit_rport_, IDC_EDIT_RPORT, std::to_wstring(cfg_.remote_port).c_str(), ES_NUMBER);
    MakeButton(btn_nconn_, IDC_BTN_NCONN, L"连接");
    MakeButton(btn_ndisc_, IDC_BTN_NDISC, L"断开");
    MakeLabel(lbl_nic_, IDC_LBL_NIC, L"网卡");
    MakeCombo(cmb_nic_, IDC_CMB_NIC);
    MakeButton(btn_nicref_, IDC_BTN_NICREF, L"刷新");
    MakeLabel(lbl_local_, IDC_LBL_LOCAL, L"本地");
    MakeCombo(cmb_lip_, IDC_CMB_LIP);
    MakeEdit(edit_lport_, IDC_EDIT_LPORT, std::to_wstring(cfg_.local_port).c_str(), ES_NUMBER);
    SendMessageW(cmb_nic_, CB_SETDROPPEDWIDTH, ScalePx(300, dpi_), 0);
    SendMessageW(cmb_lip_, CB_SETDROPPEDWIDTH, ScalePx(280, dpi_), 0);

    // ---- 左列：串口连接区（行序对齐 PyCom：端口号 → 打开/波特率 → 数据位/停止位/校验位 → 流控 → 线路）----
    MakeLabel(lbl_port_, IDC_LBL_PORT, L"端口号");
    MakeCombo(cmb_port_, IDC_CMB_PORT);
    RefreshPorts();                                 // SetupDi 真枚举（M0.5 起替换 COM1..8 占位）
    SendMessageW(cmb_port_, CB_SETDROPPEDWIDTH, ScalePx(220, dpi_), 0);   // 长端口名弹出列表完整显示
    // §102（审查 P2-A）：此处原有 SelectComboText(cfg_.port) 按**纯名精确匹配**项文本
    // （"COM3" vs "COM3 - USB-SERIAL CH343"）失败即回落 index 0——把上面 RefreshPorts
    // 刚做好的按名选中覆盖成 COM1，重启后实际打开错误端口。RefreshPorts 已完整处理
    // 「保持旧选→按配置端口名→第一项」的选中逻辑，此处删除即可。
    MakeButton(btn_refresh_, IDC_BTN_REFRESH, L"刷新");

    led_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                           0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_LED_LINK, hInst_, nullptr);
    MakeButton(btn_open_, IDC_BTN_OPEN, L"打开串口");
    MakeLabel(lbl_baud_, IDC_LBL_BAUD, L"波特率");
    cmb_baud_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
                                0, 0, 10, 200, hwnd_, (HMENU)(INT_PTR)IDC_CMB_BAUD, hInst_, nullptr);
    for (int b : { 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200,
                   230400, 460800, 921600, 1000000, 1500000, 2000000, 3000000 }) {
        wchar_t s[16]; swprintf(s, 16, L"%d", b);
        SendMessageW(cmb_baud_, CB_ADDSTRING, 0, (LPARAM)s);
    }
    {   // 「自定义」槽位（SSCOM 对齐；§55 用户拍板恢复 §46 弹窗式，§54 直输废弃）：
        // 选中「自定义」弹输入窗；DROPDOWNLIST 不可直接键入；cfg 值不在预设则插为数字项
        wchar_t t[16];
        swprintf(t, 16, L"%d", cfg_.baud);
        baudCustomIdx_ = (int)SendMessageW(cmb_baud_, CB_GETCOUNT, 0, 0);
        SendMessageW(cmb_baud_, CB_ADDSTRING, 0, (LPARAM)L"自定义");
        int idx = (int)SendMessageW(cmb_baud_, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)t);
        if (idx < 0) {                                    // cfg 值不在预设 → 插为数字项（「自定义」之后）
            idx = (int)SendMessageW(cmb_baud_, CB_INSERTSTRING, baudCustomIdx_ + 1, (LPARAM)t);
        }
        SendMessageW(cmb_baud_, CB_SETCURSEL, idx, 0);
        baudLastValid_ = t;
    }

    MakeLabel(lbl_data_, IDC_LBL_DATA, L"数据位");
    MakeCombo(cmb_databits_, IDC_CMB_DATABITS);
    for (auto s : { L"8", L"7", L"6", L"5" }) SendMessageW(cmb_databits_, CB_ADDSTRING, 0, (LPARAM)s);
    { wchar_t b[16]; swprintf(b, 16, L"%d", cfg_.databits); SelectComboText(cmb_databits_, b); }
    MakeLabel(lbl_stop_, IDC_LBL_STOP, L"停止位");
    MakeCombo(cmb_stopbits_, IDC_CMB_STOPBITS);
    for (auto s : { L"1", L"1.5", L"2" }) SendMessageW(cmb_stopbits_, CB_ADDSTRING, 0, (LPARAM)s);
    { wchar_t b[16]; swprintf(b, 16, L"%g", cfg_.stopbits); SelectComboText(cmb_stopbits_, b); }
    MakeLabel(lbl_parity_, IDC_LBL_PARITY, L"校验位");
    MakeCombo(cmb_parity_, IDC_CMB_PARITY);
    for (auto s : { L"无校验", L"偶校验", L"奇校验", L"Mark", L"Space" })
        SendMessageW(cmb_parity_, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(cmb_parity_, CB_SETDROPPEDWIDTH, ScalePx(110, dpi_), 0);
    SelectComboText(cmb_parity_, cfg_.parity == L"even"  ? L"偶校验" :
                                 cfg_.parity == L"odd"   ? L"奇校验" :
                                 cfg_.parity == L"mark"  ? L"Mark" :
                                 cfg_.parity == L"space" ? L"Space" : L"无校验");

    MakeLabel(lbl_flow_, IDC_LBL_FLOW, L"流控");
    MakeCombo(cmb_flow_, IDC_CMB_FLOW);
    for (auto s : { L"无流控", L"RTS/CTS", L"XON/XOFF" }) SendMessageW(cmb_flow_, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(cmb_flow_, CB_SETDROPPEDWIDTH, ScalePx(120, dpi_), 0);
    SelectComboText(cmb_flow_, cfg_.flow == L"rtscts"  ? L"RTS/CTS" :
                               cfg_.flow == L"xonxoff" ? L"XON/XOFF" : L"无流控");
    MakeCheck(chk_dtr_, IDC_CHK_DTR, L"DTR", cfg_.dtr);
    MakeCheck(chk_rts_, IDC_CHK_RTS, L"RTS", cfg_.rts);

    MakeLabel(lbl_line_, IDC_LBL_LINE, L"线路");
    for (int i = 0; i < kPinCount; ++i) MakePin(pin_[i], IDC_PIN_DCD + i, kPinNames[i]);
    pinBrush_ = CreateSolidBrush(RGB(0xE6, 0xE6, 0xE6));          // 引脚指示框底（未置位=浅灰）
    breakBrush_ = CreateSolidBrush(RGB(0xFF, 0xDC, 0x00));        // §73/§76：线路 BREAK 点亮=黄
    okBrush_   = CreateSolidBrush(RGB(0x2E, 0xCC, 0x40));         // 置位=绿（← PyCom #2ECC40）
    errBrush_  = CreateSolidBrush(RGB(0xFF, 0x41, 0x36));         // ERR=红（← PyCom #FF4136）
    // 查找条（D4；§75 移入扩展窗）之后：BREAK 复选框（§76：兼线路 BREAK 状态显示，
    // ownerdraw——方框=发送选择，BREAK 文字区检测到线路 BREAK 时黄亮 1.5s）
    chk_break_ = CreateWindowExW(0, L"BUTTON", L"BREAK",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_CHK_BREAK, hInst_, nullptr);

    // 输入类下拉全部禁滚轮（用户反馈）：改动后仅手动调整生效。
    // §56：子类化已移入 MakeCombo（创建处）——此批量循环曾跑在创建前、句柄为空，
    // 漏掉 加校验/校验尾/本地IP/网卡 四个下拉（用户实测滚轮仍切值）。这里只补内联创建的波特率。
    DisableComboWheel(cmb_baud_);

    // ---- 中列：接收选项（HEX显示/保存数据/接收数据到文件 一行；加时间戳行一行）----
    MakeCheck(chk_hex_show_,   IDC_CHK_HEXSHOW,  L"HEX显示", cfg_.hex_mode);
    MakeButton(btn_save_data_, IDC_BTN_SAVEDATA, L"保存数据");
    MakeCheck(chk_rx_file_,    IDC_CHK_RXFILE,   L"接收数据到文件", false);
    MakeCheck(chk_split_,      IDC_CHK_SPLIT,    L"加时间戳和分包显示", cfg_.split_show);
    // 时间戳样式色块：自绘 + SS_NOTIFY（点击 → §67 样式弹窗：颜色/加粗/倾斜/下划线）
    chip_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
                            0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_CHIP_TS, hInst_, nullptr);
    MakeLabel(lbl_timeout_, IDC_LBL_TIMEOUT, L"超时时间:");   // P3-4：原「, 超时」前导逗号观感差（对齐原版「超时时间:」）
    MakeEdit(edit_split_ms_, IDC_EDIT_SPLITMS, L"20", ES_NUMBER);
    MakeLabel(lbl_ms_, IDC_LBL_MS, L"ms");

    // ---- 右列：发送选项 ----
    MakeCheck(chk_hex_send_,   IDC_CHK_HEXSEND,  L"HEX发送", cfg_.send_hex);
    MakeCheck(chk_timed_,      IDC_CHK_TIMED,    L"定时发送:", cfg_.send_timed);   // §132：勾选态持久化
    timedArmed_ = cfg_.send_timed;                 // 仅恢复武装态——点「发送」才开跑（同勾选语义）
    MakeEdit(edit_interval_,   IDC_EDIT_INTERVAL, L"1000", ES_NUMBER);
    SetWindowTextW(edit_interval_, std::to_wstring(cfg_.send_interval_ms).c_str());   // §132：间隔持久化
    MakeLabel(lbl_timed_unit_, IDC_LBL_TIMEDUNIT, L"ms/次");
    MakeCheck(chk_newline_,    IDC_CHK_NEWLINE,  L"加回车换行", cfg_.send_newline);
    EnableWindow(chk_newline_, !cfg_.send_hex);   // SSCOM 对齐：HEX发送时「加回车换行」置灰（§58）
    MakeLabel(lbl_chk_pre_,    IDC_LBL_CHKPRE,   L"第");
    MakeEdit(edit_chk_start_,  IDC_EDIT_CHKSTART, L"1", ES_NUMBER);
    MakeLabel(lbl_chk_suf_,    IDC_LBL_CHKSUF,   L"字节");
    MakeCombo(cmb_chk_tail_,   IDC_CMB_CHKTAIL);
    for (int i = 0; i < kTailPlaceCount; ++i) SendMessageW(cmb_chk_tail_, CB_ADDSTRING, 0, (LPARAM)kTailPlaces[i]);
    SendMessageW(cmb_chk_tail_, CB_SETDROPPEDWIDTH, ScalePx(230, dpi_), 0);
    SelectComboText(cmb_chk_tail_, kTailPlaces[(cfg_.checksum_tail >= 1 && cfg_.checksum_tail <= 4)
                                               ? cfg_.checksum_tail - 1 : kTailPlaceCount - 1]);
    MakeLabel(lbl_chk_lbl_,    IDC_LBL_CHKLBL,   L"加校验:");
    MakeCombo(cmb_checksum_,   IDC_CMB_CHECKSUM);
    for (int i = 0; i < kChecksumCount; ++i) SendMessageW(cmb_checksum_, CB_ADDSTRING, 0, (LPARAM)kChecksumLabels[i]);
    SendMessageW(cmb_checksum_, CB_SETDROPPEDWIDTH, ScalePx(140, dpi_), 0);
    {
        int sel = 0;
        for (int i = 0; i < kChecksumCount; ++i) if (cfg_.checksum_kind == kChecksumValues[i]) sel = i;
        SelectComboText(cmb_checksum_, kChecksumLabels[sel]);
    }
    // §77 修复（老 bug「保存过校验设置重启即丢」）：两个根因——
    // ①起始字节框此前不灌 cfg 值（恒显示默认 1）；②CreateWindow/SetWindowText/ApplyFonts
    // 的 WM_SETFONT 都会在 BuildChildren 早期触发 EN_CHANGE，此时 cmb_checksum_ 还没建、
    // CB_GETCURSEL=-1 → SyncChecksumCfg 把 cfg 覆盖成 none/1。根治靠 uiInitDone_ 闸门
    // （见 Handle WM_CREATE），此处只管把控件初值补齐成 cfg 值。
    {
        wchar_t sb[16];
        swprintf(sb, 16, L"%d", cfg_.checksum_start < 1 ? 1 : cfg_.checksum_start);
        SetWindowTextW(edit_chk_start_, sb);
    }
    MakeEdit(edit_chk_view_,   IDC_EDIT_CHKVIEW, L"", ES_READONLY);

    // ---- 发送内容框 + 大「发 送」按钮 ----
    edit_send_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                 0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_EDIT_SEND, hInst_, nullptr);
    SetWindowSubclass(edit_send_, SendEditProc, 0, (DWORD_PTR)this);   // 提示 + Ctrl+Enter 发送
    MakeButton(btn_send_, IDC_BTN_SEND, L"发  送");

    // ---- 状态栏（5 段；窄窗隐藏第 5 段）----
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                              0, 0, 0, 0, hwnd_, nullptr, hInst_, nullptr);
    // §77：RX 段染色覆盖窗（ownerdraw STATIC；见 SyncTintOverlay 的选型说明）。初始隐藏，
    // 只在缓冲占用 ≥60% 时由 SyncTintOverlay 定位显示。
    tint_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_OWNERDRAW,
                            0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)IDC_RX_TINT, hInst_, nullptr);

    // ---- 查找（D4；§75 用户拍板：迁入扩展窗「查找」页——控件改在 CreateExtWindow 建）----

    // ---- 扩展独立小窗（D5：WS_EX_TOOLWINDOW 不占任务栏/不进 Alt+Tab）----
    CreateExtWindow();
    ApplyCollapse();                               // 恢复上次的收起态（配置暂不记忆，默认展开）
}


void MainWindow::ApplyFonts() {
    if (uiFont_)   DeleteObject(uiFont_);
    if (sysFont_)  DeleteObject(sysFont_);
    if (dataFont_) DeleteObject(dataFont_);
    if (tsRxFont_) DeleteObject(tsRxFont_);
    if (tsTxFont_) DeleteObject(tsTxFont_);
    // §135（用户「整个界面全部改用和 SSCOM 一样的宋体」）：外壳字体改 **宋体 9pt**——真机读原版
    // 普查：SSCOM 主体控件（298×TEdit /108×TBitBtn/106×TCheckBox/端口框/TPageControl）= 宋体
    // -12@96DPI、中英文**同一字体**（ASCII 走宋体自带字形）；少数下拉/小复选是组件默认
    // MS Sans Serif、状态栏是系统字体（见下方 sysFont_）。品质：96dpi 用 DEFAULT(宋体内嵌点阵，
    // 与原版逐像素同款），高 dpi 走 ClearType（缩放后点阵观感差，取清晰度）。
    {
        LOGFONTW lf{};
        lf.lfHeight  = -MulDiv(9, dpi_, 72);
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = (dpi_ == 96) ? DEFAULT_QUALITY : CLEARTYPE_QUALITY;
        lstrcpynW(lf.lfFaceName, L"宋体", LF_FACESIZE);
        uiFont_ = CreateFontIndirectW(&lf);
        if (!uiFont_) uiFont_ = CreateSystemUiFont(dpi_);   // 兜底（宋体缺失的罕见系统）
    }
    sysFont_  = CreateSystemUiFont(dpi_);       // 状态栏沿用系统字体（对齐原版实测：
                                                // SSCOM 的 TStatusBar = Microsoft YaHei UI）
    dataFont_ = CreateUiFont(cfg_.font, dpi_);  // 收发数据：配置里的统一「字体与颜色」
    // §68：收/发时间戳字体 = 同一配置字体 + 各自样式变体（家族/字号不可单独设置）；
    // §69：时间戳不加粗（字宽变化破坏折行 3 格网格），弹窗仅提供倾斜/下划线
    FontSpec rf = cfg_.font;
    rf.bold = false; rf.italic = cfg_.rx_ts_italic; rf.underline = cfg_.rx_ts_underline;
    FontSpec xf = cfg_.font;
    xf.bold = false; xf.italic = cfg_.tx_ts_italic; xf.underline = cfg_.tx_ts_underline;
    tsRxFont_ = CreateUiFont(rf, dpi_);
    tsTxFont_ = CreateUiFont(xf, dpi_);
    SendMessageW(hwnd_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    ApplyFontToChildren(hwnd_, uiFont_);
    if (status_ && sysFont_)                        // §135：状态栏例外=系统字体（对齐原版实测）
        SendMessageW(status_, WM_SETFONT, (WPARAM)sysFont_, TRUE);
    if (extWnd_) {
        // §75：扩展窗子控件（含迁入的查找页）随外壳字体刷新——此前仅创建时设置，
        // 运行期换字体后 ext 控件持旧 HFONT（ApplyFonts 重建时已被 DeleteObject）
        ApplyFontToChildren(extWnd_, uiFont_);
        SendMessageW(extTab_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    }
    rxv_.SetFont(dataFont_);
    rxv_.SetTsFonts(tsRxFont_, tsTxFont_);
    if (edit_send_) SendMessageW(edit_send_, WM_SETFONT, (WPARAM)dataFont_, TRUE);
}


// ---------------- 布局（固定布局：控件位置恒定；超出窗口右缘的部分被裁掉） ----------------
void MainWindow::Layout(bool atomic) {
    // atomic（查找条开关/收起/DPI 等结构变化）：冻结重绘 → 全部 SetWindowPos 一次生效 →
    // 整帧重画。否则十余个子控件分步移动的中间态 + 主窗背景擦除（COLOR_BTNFACE 灰底）
    // 会闪出"组件混乱"画面（用户实测报告）。WM_SIZE 拖拽缩放保持实时重排。
    // P0-1 修复（docs/14）：恢复必须覆盖全部提前 return（collapsed_ / !hwnd_ 等路径）——
    // 此前手写在函数尾，collapsed_ 提前返回把 redraw 永久冻结（整窗树停刷）→ RAII。
    struct RedrawGuard {
        HWND h;
        bool on;
        RedrawGuard(HWND hw, bool a) : h(hw), on(a) {
            if (on && h) SendMessageW(h, WM_SETREDRAW, FALSE, 0);
        }
        ~RedrawGuard() {
            if (on && h) {
                SendMessageW(h, WM_SETREDRAW, TRUE, 0);
                // 不带 RDW_ERASE：整窗先刷 COLOR_BTNFACE 灰底再重画 = 用户可见的"闪一下"
                // （§57）。结构变化中不变的间隙条/标签/按钮自绘不透明底，无需主窗擦除；
                // 接收区经 RDW_ALLCHILDREN 全量重画并自身铺满客户区。
                RedrawWindow(h, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
    } guard(hwnd_, atomic);
    if (!hwnd_ || !status_) return;
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const int cw = rc.right, ch = rc.bottom;
    const int sbH = ScalePx(kStatusH, dpi_);
    const int px  = ScalePx(kGap, dpi_);
    const int g   = ScalePx(kGapTight, dpi_);

    SetWindowPos(status_, nullptr, 0, ch - sbH, cw, sbH, SWP_NOZORDER);
    SendMessageW(status_, WM_SIZE, 0, 0);
    const int workH = ch - sbH;

    narrow_ = cw < ScalePx(kNarrowW, dpi_);

    // ---- 左列宽度实测（自底部面板上提：状态栏三段合计 ≤「发 送」按钮右缘 §58、
    //      打开文件锚点对齐 §49 都依赖 leftW，故置于状态栏之前）----
    const int colGap = ScalePx(kColGap, dpi_);
    const int lx  = ScalePx(kMarginX, dpi_);

    auto cmbW = [&](const wchar_t* widest, int minW) {
        int w = TextW(widest) + ScalePx(kArrowPad, dpi_);
        return w < minW ? minW : w;
    };
    // §79/§80：1 个英文字符宽（当前 UI 字体实测）——左列右缘收窄与各控件收窄/加宽都以它为单位
    const int chAscii = TextW(L"0");

    // 左列：尺寸按实测标定（列宽 = 最宽行）
    const int wLblPort = TextW(L"端口号");
    int wPort    = ScalePx(150, dpi_);
    const int wRefresh = BtnW(L"刷新");
    const int row1W    = wLblPort + g + wPort + g + wRefresh;
    int wBtnOpen = BtnW(L"打开串口");
    const int wLblBaud = TextW(L"波特率");
    int wBaud    = cmbW(L"3000000", ScalePx(74, dpi_));
    const int row2W    = ScalePx(18, dpi_) + wBtnOpen + g + wLblBaud + g + wBaud;
    const int wLblData = TextW(L"数据位"), wLblStop = TextW(L"停止位"), wLblParity = TextW(L"校验位");
    // §80（用户拍板）：数据位/停止位选择窗各收窄 1 英文字符，校验位选择窗加宽 2 字符
    // → 本行总宽不变（仍"正好占满"到左列右缘）
    const int wData    = cmbW(L"8", ScalePx(42, dpi_)) - chAscii;
    const int wStop    = cmbW(L"1.5", ScalePx(48, dpi_)) - chAscii;
    int wParity  = cmbW(L"无校验", ScalePx(64, dpi_)) + 2 * chAscii;
    const int row3W    = wLblData + g + wData + g + wLblStop + g + wStop + g + wLblParity + g + wParity;
    const int wLblFlow = TextW(L"流控");
    int wFlow    = cmbW(L"无流控", ScalePx(66, dpi_));
    const int wDtr     = ChkW(L"DTR"), wRts = ChkW(L"RTS");
    const int row4W    = wLblFlow + g + wFlow + g + wDtr + g + wRts;
    const int wLblLine = TextW(L"线路");
    const int wPin     = ScalePx(kPinW, dpi_), hPin = ScalePx(18, dpi_);
    const int wBreak   = ChkW(L"BREAK");
    const int row5W    = wLblLine + g + kPinCount * (wPin + px) + wBreak;
    int leftW = row1W;
    if (row2W > leftW) leftW = row2W;
    if (row3W > leftW) leftW = row3W;
    if (row4W > leftW) leftW = row4W;
    if (row5W > leftW) leftW = row5W;
    // §79（用户拍板）：左列（含网络区）整体右缘**左移 2 个英文字符**——「发 送」按钮与各
    // "吃余量"控件（端口框/波特率框/流控框）同步收窄，行内其余控件右边界一律对齐按钮右缘
    // （此前第 5 行 BREAK 右缘 276 与按钮 296 不齐）。中列起点/工具行锚点仍按 leftW
    // （用户未要求移动中列），故只缩控件、不动网格。
    const int leftWEff = leftW - 2 * chAscii;

    {
        // §66（用户拍板）：删除「已连接/未连接」段——连接状态由 LED 四态灯 + 打开按钮三态
        // 文案 + 第 3 段通知承载；腾出的 64px 平分给 TX/RX（TX 右缘 104+32=136 设计 px，
        // RX 右缘仍锁「发 送」按钮右缘 → RX 净增 32px）。
        // §58 ③：TX/RX 两段合计 ≤「发 送」按钮右缘（lx+leftW，与 UI-3 最小窗宽同源）。
        // 注意 SB_SETPARTS 数组=各段【右边界坐标】而非宽度（§58 初版曾按宽度传、TX/RX 段被裁成 40/16px）。
        // P3-1（审查 20261026）：段右缘贴窗缘，SBARS_SIZEGRIP 手柄会压住末字符——
        // 段宽补上手柄占位（SM_CXVSCROLL+4），文本保持完整（距右缘 ~手柄宽，视觉仍贴右）。
        int verW = TextW(SNCOM_VER_W) + ScalePx(6, dpi_)
                 + GetSystemMetrics(SM_CXVSCROLL) + 4;
        int sendRight = lx + leftWEff;       // §79：随「发 送」按钮右缘（已左移 2 字符）
        int parts[4];
        parts[0] = ScalePx(104 + 64 / 2, dpi_);            // TX 段右缘（+连接段宽的一半）
        parts[1] = sendRight;                              // RX 段右缘 = 按钮右缘
        if (parts[1] < parts[0] + ScalePx(100, dpi_)) parts[1] = parts[0] + ScalePx(100, dpi_);
        parts[2] = cw - verW;
        parts[3] = -1;
        if (narrow_) {
            // §74（审查 F7）：窄窗保留通知段（版本让位）——否则「重连中…/打开 COMx
            // 失败…」等全部无处显示；RX 右缘适配窄窗（给通知留 ~90px 并保底）
            int rxEnd = sendRight;
            if (rxEnd > cw - ScalePx(90, dpi_)) rxEnd = cw - ScalePx(90, dpi_);
            if (rxEnd < parts[0] + ScalePx(80, dpi_)) rxEnd = parts[0] + ScalePx(80, dpi_);
            parts[1] = rxEnd;
            parts[2] = -1;
        }
        SendMessageW(status_, SB_SETPARTS, narrow_ ? 3 : 4, (LPARAM)parts);
        // §58：文本不在此硬刷——此前每次 Layout 把 RX 段先覆写「就绪」（还错写进第 2 段）
        // 再等 250ms 周期刷回，用户可见闪现；SB_SETPARTS 之后从活值各刷一次即可
        //（周期写手=UpdateCounters；通知段以 noticeText_ 重写——§74 审查 F3）。
        UpdateCounters();
        if (!noticeText_.empty())
            SendMessageW(status_, SB_SETTEXTW, 2, (LPARAM)noticeText_.c_str());
        if (!narrow_) SendMessageW(status_, SB_SETTEXTW, 3, (LPARAM)SNCOM_VER_W);
    }

    // 自下而上：状态栏 → 底部面板 → 工具行 → 接收显示框（对齐 PyCom V0.0.5 分区）
    const int btnH   = ScalePx(kBtnH, dpi_);
    const int rowGap = ScalePx(kRowGap, dpi_);
    // D3 收起：底部参数面板折叠 → 接收区延伸到工具行（连接状态不受影响）
    const int panelH = collapsed_ ? 0 : ScalePx(kPanelH, dpi_);
    const int panelY = workH - panelH;
    // §76：工具行整行下移 3 设计 px（用户要求更紧凑；底部面板位置不动——
    // 工具行与面板的视觉间隔由 rowGap+panelPadY 减至 panelPadY）
    const int toolY  = panelY - rowGap - btnH + ScalePx(3, dpi_);
    // §75：查找条已迁入扩展窗「查找」页——主窗不再预留顶部条、不再有控件显隐同步
    // （F1 教训的"无条件同步显隐"原则随控件移至 ShowExtPage 落地）

    // （左列宽度实测已上提至状态栏段宽之前——§58）

    // ---- 工具行 ----
    {
        struct Item { HWND h; const wchar_t* t; int fixedW; int gap; };
        const Item row[] = {
            { btn_clear_,      L"清除窗口",  0, 0 },
            { btn_find_,       L"查找",      0, kGap },
            { btn_new_,        L"新窗口",    0, kGap },
            { chk_autoscroll_, L"自动滚屏",  0, kToolGapBig },
            { btn_openfile_,   L"打开文件",  0, kToolGapFile },
            { edit_file_,      nullptr,      kEdtFileW, kGap },
            { btn_sendfile_,   L"发送文件",  0, kGap },
            { btn_stopfile_,   L"停止",      0, kGap },
            { btn_clearsend_,  L"清发送区",  0, kGap },
            { btn_collapse_,   L"收起",      0, kToolGapTail },   // §82：960 默认宽收口
            { btn_top_,        L"最前",      0, kGap },
            { btn_saveparams_, L"保存参数",  0, kGap },
            { btn_expand_,     L"扩展",      0, kGap },
        };
        int x = ScalePx(kMarginX, dpi_);
        for (const Item& it : row) {
            int gap = ScalePx(it.gap, dpi_);
            x += gap;
            if (it.h == btn_openfile_) {
                // 打开文件块左缘精确对齐底部面板中列首控件（HEX显示/保存数据行，x = lx+leftW+colGap）。
                // 此前靠「前一串按钮宽度 + kToolGapFile−1W」累加出的巧合对齐，字体度量下
                // 偏左 4px（探针实测 536 vs 540，用户反馈）；改锚点对齐，右侧按钮链式跟随。
                x = lx + leftWEff + colGap;   // §80：中列起点随按钮右缘（§49 锚点不变量）
            }
            int w = it.fixedW ? ScalePx(it.fixedW, dpi_)
                              : (it.h == chk_autoscroll_ ? ChkW(it.t) : BtnW(it.t));
            if (it.h == edit_file_) w -= TextW(L"中文");     // UI-5：路径框缩 2 汉字，右侧跟随
            SetWindowPos(it.h, nullptr, x, toolY, w, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
            x += w;
        }
        toolRight = x;
    }

    // ---- 接收显示框：菜单栏之下 → 工具行之上（§75 起查找条不再占顶部） ----
    {
        const int rxTop0 = ScalePx(kRxTop, dpi_);
        int rxTop = rxTop0;
        int rxH = toolY - rowGap - rxTop;
        // §85（用户报：收起再展开后"组件混乱"）——根因：窗口过矮时把接收区**强撑**到
        // kRxMinH，接收区就压到工具行/面板行之上；接收区 z 序在最上 → 面板行被整片盖住、
        // 只在它下缘露出发送框（正好是用户截图）。改为**不重叠优先**：空间不足就让接收区
        // 自然变矮（≥0）；正常路径由「最小窗高」minClientH_（见 WM_GETMINMAXINFO）保证 ≥ kRxMinH。
        rxHAvailable_ = rxH;                  // 记录"可用高度"（未夹取前）供最小窗高推算
        if (rxH < 0) rxH = 0;
        if (rxv_.hwnd())
            SetWindowPos(rxv_.hwnd(), nullptr, ScalePx(kRxMarginX, dpi_), rxTop,
                         cw - 2 * ScalePx(kRxMarginX, dpi_), rxH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(progress_, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }

    // ---- 底部面板（定高：左列 5 行 + 大「发 送」按钮）----
    // D3 收起：面板整体隐藏（只留接收区/工具行/查找条/状态栏），连接与收发不受影响
    {
        bool serial = (cfg_.mode == L"serial");
        HWND bottomCtrls[64];
        int n = 0;
        HWND sCtrls[] = { lbl_port_, cmb_port_, btn_refresh_, led_, btn_open_, lbl_baud_, cmb_baud_,
                          lbl_data_, cmb_databits_, lbl_stop_, cmb_stopbits_, lbl_parity_, cmb_parity_,
                          lbl_flow_, cmb_flow_, chk_dtr_, chk_rts_, lbl_line_,
                          pin_[0], pin_[1], pin_[2], pin_[3], pin_[4], chk_break_, btn_send_ };
        HWND nCtrls[] = { led2_, lbl_netmode_, lbl_remote_, edit_rip_, edit_rport_,
                          btn_nconn_, btn_ndisc_, lbl_nic_, cmb_nic_, btn_nicref_,
                          lbl_local_, cmb_lip_, edit_lport_ };
        HWND mCtrls[] = { chk_hex_show_, btn_save_data_, chk_rx_file_, chk_split_, chip_,
                          lbl_timeout_, edit_split_ms_, lbl_ms_, edit_send_ };
        HWND rCtrls[] = { chk_hex_send_, chk_timed_, edit_interval_, lbl_timed_unit_, chk_newline_,
                          lbl_chk_pre_, edit_chk_start_, lbl_chk_suf_, cmb_chk_tail_, lbl_chk_lbl_,
                          cmb_checksum_, edit_chk_view_ };
        // 隐藏清单必须与 collapsed_ 无关地建好：收起时正是靠它把面板真正隐藏
        // （此前只在展开分支填充 -> 收起时 n==0，隐藏循环空转，面板只是被接收区盖住，
        //   发送框与「发 送」按钮从接收区下缘露出；审查 P2-2）
        {
            HWND* col = serial ? sCtrls : nCtrls;
            int cn = serial ? (int)(sizeof(sCtrls) / sizeof(sCtrls[0]))
                            : (int)(sizeof(nCtrls) / sizeof(nCtrls[0]));
            for (int i = 0; i < cn; ++i) bottomCtrls[n++] = col[i];
            for (HWND h : mCtrls) bottomCtrls[n++] = h;
            for (HWND h : rCtrls) bottomCtrls[n++] = h;
        }
        static bool prevCollapsed = false;
        if (collapsed_ != prevCollapsed) {
            for (int i = 0; i < n; ++i)
                if (bottomCtrls[i]) ShowWindow(bottomCtrls[i], collapsed_ ? SW_HIDE : SW_SHOW);
            // 大「发 送」按钮属于两模式共用控件，只在串口列的清单里 -> 网络模式下
            // 上面这轮循环漏掉它（收起时仍露在接收区下缘），此处补一刀（审查 P2-2 残留）
            if (btn_send_) ShowWindow(btn_send_, collapsed_ ? SW_HIDE : SW_SHOW);
            // 展开恢复：先全显再收敛到当前模式（串口/网络列互斥 + 锁死联动 + 按钮文案）。
            // 不收敛则网络模式下展开后显示的是串口参数列（审查 P2-3）
            if (!collapsed_) ApplyModeColumns();
            prevCollapsed = collapsed_;
        }
        if (collapsed_) return;                // 面板已隐藏：跳过全部面板定位
    }
    const int rowH   = ScalePx(kRowH, dpi_);
    const int ctlH   = ScalePx(kCtlH, dpi_);
    const int comboH = ScalePx(kComboDropH, dpi_);
    const int topPad = ScalePx(kPanelPadY, dpi_);
    // 左列宽度实测（colGap/lx/左列测量上提至工具行之前：打开文件按钮的 x 要精确
    // 对齐中列起点，见工具行块内注释；此处只保留依赖 panelY/topPad 的 y 定位）
    const int y0  = panelY + topPad;
    const int yLbl = ScalePx(4, dpi_);          // 标签垂直居中微调
    // 行内剩余宽度按 PyCom 的 stretch 归属分配（端口框 / 打开按钮+波特率 / 校验位 / 流控 吃满行）
    // §79：余量**可为负**——收窄 2 英文字符后，自然宽最大的第 2/3 行会超出 leftWEff，
    // 此时由用户点名的控件（端口框/波特率框/校验位框/流控框）全额吸收，其余控件不动
    //（首版只处理 slack>0 → 校验位/波特率不缩，右缘仍停在旧位置 296≠282，探针揪出）。
    {
        auto eat = [&](int& w, int slack, int minW) {
            w += slack;
            if (w < minW) w = minW;              // 兜底：极窄字体/异常 DPI 下不许吃穿
        };
        eat(wPort, leftWEff - row1W, ScalePx(80, dpi_));
        int slack2 = leftWEff - row2W;
        if (slack2 > 0) { wBtnOpen += slack2 / 2; wBaud += slack2 - slack2 / 2; }
        else            { eat(wBaud, slack2, ScalePx(50, dpi_)); }   // 负值只缩波特率框（用户点名）
        eat(wParity, leftWEff - row3W, ScalePx(44, dpi_));
    }
    {
        int slack = leftWEff - row4W;           // §79：正负都吃（流控框是用户点名的收缩对象）
        wFlow += slack;
        if (wFlow < ScalePx(48, dpi_)) wFlow = ScalePx(48, dpi_);
    }
    // -- r1 端口号 / 端口 / 刷新
    {
        int y = y0, x = lx;
    SetWindowPos(lbl_port_, nullptr, x, y + yLbl, wLblPort, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblPort + g;
    SetWindowPos(cmb_port_, nullptr, x, y, wPort, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wPort + g;
    SetWindowPos(btn_refresh_, nullptr, x, y, wRefresh, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // -- r2 连接状态圆点 / 打开串口 / 波特率
    {
        int y = y0 + rowH, x = lx;
    SetWindowPos(led_, nullptr, x, y + ScalePx(5, dpi_), ScalePx(14, dpi_), ScalePx(14, dpi_), SWP_NOZORDER | SWP_NOACTIVATE);
        x += ScalePx(18, dpi_);
    SetWindowPos(btn_open_, nullptr, x, y, wBtnOpen, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wBtnOpen + g;
    SetWindowPos(lbl_baud_, nullptr, x, y + yLbl, wLblBaud, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblBaud + g;
    SetWindowPos(cmb_baud_, nullptr, x, y, wBaud, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // -- r3 数据位 / 停止位 / 校验位
    {
        int y = y0 + 2 * rowH, x = lx;
    SetWindowPos(lbl_data_, nullptr, x, y + yLbl, wLblData, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblData + g;
    SetWindowPos(cmb_databits_, nullptr, x, y, wData, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wData + g;
    SetWindowPos(lbl_stop_, nullptr, x, y + yLbl, wLblStop, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblStop + g;
    SetWindowPos(cmb_stopbits_, nullptr, x, y, wStop, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wStop + g;
    SetWindowPos(lbl_parity_, nullptr, x, y + yLbl, wLblParity, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblParity + g;
    SetWindowPos(cmb_parity_, nullptr, x, y, wParity, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // -- r4 流控 / DTR / RTS
    {
        int y = y0 + 3 * rowH, x = lx;
    SetWindowPos(lbl_flow_, nullptr, x, y + yLbl, wLblFlow, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblFlow + g;
    SetWindowPos(cmb_flow_, nullptr, x, y, wFlow, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wFlow + g;
    SetWindowPos(chk_dtr_, nullptr, x, y, wDtr, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wDtr + g;
    SetWindowPos(chk_rts_, nullptr, x, y, wRts, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // -- r5 线路 / 引脚指示(DCD DSR CTS RI ERR) / BREAK
    {
        int y = y0 + 4 * rowH, x = lx;
    SetWindowPos(lbl_line_, nullptr, x, y + yLbl, wLblLine, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wLblLine + g;
        for (int i = 0; i < kPinCount; ++i) {
    SetWindowPos(pin_[i], nullptr, x, y + ScalePx(3, dpi_), wPin, hPin, SWP_NOZORDER | SWP_NOACTIVATE);
            x += wPin + px;
        }
        // §79：本行余量给 BREAK——用户要求按钮上方所有控件右边界对齐按钮右缘
        //（此前 BREAK 恒定宽，行右缘 276 与按钮 296 不齐）
        int wBreakEff = wBreak;
        {
            int slack = leftWEff - row5W;
            if (slack > 0) wBreakEff += slack;
        }
    SetWindowPos(chk_break_, nullptr, x, y, wBreakEff, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // -- r6 大「发 送」按钮（占满左列宽；串口/网络两区共用；§79 右缘左移 2 英文字符）
    SetWindowPos(btn_send_, nullptr, lx, y0 + 5 * rowH + ScalePx(2, dpi_), leftWEff, ScalePx(kBtnMinH, dpi_), SWP_NOZORDER | SWP_NOACTIVATE);

    // ---- 左列：网络连接区（与串口区同一行网格，模式切换互斥显示）----
    {
        const int wRemote = TextW(L"远程"), wNic = TextW(L"网卡"), wLocal = TextW(L"本地");
        const int wPort = ScalePx(70, dpi_);
        // r0 网络 LED + 模式标签
        SetWindowPos(led2_, nullptr, lx, y0 + ScalePx(5, dpi_), ScalePx(14, dpi_), ScalePx(14, dpi_),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(lbl_netmode_, nullptr, lx + ScalePx(18, dpi_), y0 + yLbl,
                     leftWEff - ScalePx(18, dpi_), ctlH, SWP_NOZORDER | SWP_NOACTIVATE);   // §79
        // r1 远程：IP(吃满) + 端口(70)
        {
            int y = y0 + rowH, x = lx;
            SetWindowPos(lbl_remote_, nullptr, x, y + yLbl, wRemote, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
            x += wRemote + g;
            int wIp = leftWEff - (x - lx) - wPort - g;      // §79
            SetWindowPos(edit_rip_, nullptr, x, y, wIp, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(edit_rport_, nullptr, x + wIp + g, y, wPort, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // r2 连接/断开（各占一半，加长——PyCom 口径）
        {
            int y = y0 + 2 * rowH;
            int half = (leftWEff - g) / 2;                  // §79
            SetWindowPos(btn_nconn_, nullptr, lx, y, half, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(btn_ndisc_, nullptr, lx + half + g, y, leftWEff - half - g, ctlH,
                         SWP_NOZORDER | SWP_NOACTIVATE);          // §79
        }
        // r3 网卡：下拉(吃满) + 刷新(70)
        {
            int y = y0 + 3 * rowH, x = lx;
            SetWindowPos(lbl_nic_, nullptr, x, y + yLbl, wNic, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
            x += wNic + g;
            int wCmb = leftWEff - (x - lx) - ScalePx(70, dpi_) - g;   // §79
            SetWindowPos(cmb_nic_, nullptr, x, y, wCmb, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(btn_nicref_, nullptr, x + wCmb + g, y, ScalePx(70, dpi_), ctlH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // r4 本地：IP 下拉(吃满) + 端口(70)
        {
            int y = y0 + 4 * rowH, x = lx;
            SetWindowPos(lbl_local_, nullptr, x, y + yLbl, wLocal, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
            x += wLocal + g;
            int wCmb = leftWEff - (x - lx) - wPort - g;     // §79
            SetWindowPos(cmb_lip_, nullptr, x, y, wCmb, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(edit_lport_, nullptr, x + wCmb + g, y, wPort, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // 中列：接收选项（一行三项；一行 加时间戳）
    const int wHexShow  = ChkW(L"HEX显示");
    const int wSaveData = BtnW(L"保存数据");
    const int wRxFile   = ChkW(L"接收数据到文件");
    const int wSplit    = ChkW(L"加时间戳和分包显示");
    const int wChip     = ScalePx(20, dpi_);
    const int wTimeout  = TextW(L"超时时间:");
    const int wSplitMs  = ScalePx(kEditSpinW, dpi_) - 2 * chAscii;   // §80：收窄 2 英文字符
    const int wMs       = TextW(L"ms");
    const int mid1W = wHexShow + g + wSaveData + g + wRxFile;
    const int mid2W = wSplit + g + wChip + g + wTimeout + g + wSplitMs + g + wMs;
    const int midW  = mid1W > mid2W ? mid1W : mid2W;
    // §80（用户拍板）：中列（含右列 rx2 与发送框）整体左移 2 英文字符——锚在按钮右缘，
    // 与左列收窄同步（§79 左列右缘左移后此处间距曾被撑大）
    const int mx    = lx + leftWEff + colGap;
    {
        int y = y0, x = mx;
    SetWindowPos(chk_hex_show_, nullptr, x, y, wHexShow, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wHexShow + g;
    SetWindowPos(btn_save_data_, nullptr, x, y, wSaveData, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wSaveData + g;
    SetWindowPos(chk_rx_file_, nullptr, x, y, wRxFile, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        y += rowH; x = mx;
    SetWindowPos(chk_split_, nullptr, x, y, wSplit, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wSplit + g;
    SetWindowPos(chip_, nullptr, x, y + ScalePx(3, dpi_), wChip, ScalePx(18, dpi_), SWP_NOZORDER | SWP_NOACTIVATE);
        x += wChip + g;
    SetWindowPos(lbl_timeout_, nullptr, x, y + yLbl, wTimeout, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wTimeout + g;
    SetWindowPos(edit_split_ms_, nullptr, x, y, wSplitMs, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wSplitMs + g;
    SetWindowPos(lbl_ms_, nullptr, x, y + yLbl, wMs, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // 右列：发送选项（一行 发送开关；一行 校验设置）
    const int wHexSend   = ChkW(L"HEX发送");
    const int wTimed     = ChkW(L"定时发送:");
    const int wTimedEdit = ScalePx(kEditTimedW, dpi_);
    const int wTimedUnit = TextW(L"ms/次");
    const int wNewline   = ChkW(L"加回车换行");
    const int wPre       = TextW(L"第");
    const int wSuf       = TextW(L"字节");
    const int wChkStart  = ScalePx(kEditSpinW, dpi_) - 2 * chAscii;  // §80：收窄 2 英文字符
    const int wTail      = ScalePx(kTailCmbW, dpi_);
    const int wChkLbl    = TextW(L"加校验:");
    const int wChkCmb    = ScalePx(kChkCmbW, dpi_);
    const int wChkView   = ScalePx(kEditChkW, dpi_);
    const int right1W = wHexSend + g + wTimed + g + wTimedEdit + g + wTimedUnit + g + wNewline;
    const int right2W = wPre + g + wChkStart + g + wSuf + g + wTail + colGap + wChkLbl + g + wChkCmb + g + wChkView;
    const int rx2 = mx + midW + colGap;      // 右列起点
    {
        int y = y0, x = rx2;
    SetWindowPos(chk_hex_send_, nullptr, x, y, wHexSend, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wHexSend + g;
    SetWindowPos(chk_timed_, nullptr, x, y, wTimed, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wTimed + g;
    SetWindowPos(edit_interval_, nullptr, x, y, wTimedEdit, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wTimedEdit + g;
    SetWindowPos(lbl_timed_unit_, nullptr, x, y + yLbl, wTimedUnit, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wTimedUnit + g;
    SetWindowPos(chk_newline_, nullptr, x, y, wNewline, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        y += rowH; x = rx2;
    SetWindowPos(lbl_chk_pre_, nullptr, x, y + yLbl, wPre, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wPre + g;
    SetWindowPos(edit_chk_start_, nullptr, x, y, wChkStart, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wChkStart + g;
    SetWindowPos(lbl_chk_suf_, nullptr, x, y + yLbl, wSuf, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wSuf + g;
    SetWindowPos(cmb_chk_tail_, nullptr, x, y, wTail, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wTail + colGap;
    SetWindowPos(lbl_chk_lbl_, nullptr, x, y + yLbl, wChkLbl, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wChkLbl + g;
    SetWindowPos(cmb_checksum_, nullptr, x, y, wChkCmb, comboH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += wChkCmb + g;
    SetWindowPos(edit_chk_view_, nullptr, x, y, wChkView, ctlH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // ---- 发送内容框：中列起点 → 窗口右缘；选项两行之下 → 面板底 ----
    {
        const int ex = mx;
        const int ey = y0 + 2 * rowH + ScalePx(2, dpi_);
        int ew = cw - ex - ScalePx(kMarginX, dpi_);
        int eh = (panelY + panelH - topPad) - ey;
        if (ew < ScalePx(160, dpi_)) ew = ScalePx(160, dpi_);
        if (eh < ScalePx(60, dpi_))  eh = ScalePx(60, dpi_);
        SetWindowPos(edit_send_, nullptr, ex, ey, ew, eh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // UI-3：最小窗宽 = 「发 送」按钮右边界 + 边距（§32 定、§53 用户复拍板）。
    // 不再 max 工具行右缘：工具行按钮绝对定位，窗口更窄时右侧（收起/最前/保存参数/
    // 扩展）按 PyCom「收缩裁右边」语义裁掉——若按工具行全宽抬最小宽，就无法收到
    // 按钮右缘（用户实测复发反馈）。
    minClientW_ = lx + leftWEff + ScalePx(kMarginX, dpi_);   // §79：= 按钮右缘（已左移 2 字符）
    // §85：最小窗高——展开态下接收区恰为 kRxMinH 时所需客户高（含工具行/面板/状态栏）。
    // 由当前 ch 与接收区差额推出，随 DPI/常量自动跟手；WM_GETMINMAXINFO 用它挡住"过矮"。
    minClientH_ = ch + (ScalePx(kRxMinH, dpi_) - rxHAvailable_);
}

void MainWindow::SyncMenuChecks() {
    HMENU mb = GetMenu(hwnd_);
    if (!mb) return;
    HMENU mSerial = GetSubMenu(mb, 1), mView = GetSubMenu(mb, 3), mSend = GetSubMenu(mb, 4);
    HMENU mTerm = mTermMenu_;                // §140：句柄存成员——总开关插入后 GetSubMenu(mSend,0)=NULL
    if (mSend) {
        // §140：终端仿真总开关——未勾选时「终端仿真设置」子菜单整条置灰（不可选、不生效）。
        // 子菜单按成员句柄反查位置（写死 0 号位两连坑：先灰掉总开关自己、后 GetSubMenu=NULL
        // 令四个 TERM 项勾选态失联——实测探针 B1 两次逮到）。
        CheckMenuItem(mSend, IDM_TERM_ENABLE, MF_BYCOMMAND | (cfg_.term_enable ? MF_CHECKED : MF_UNCHECKED));
        int termPos = -1;
        for (int i = (int)GetMenuItemCount(mSend) - 1; mTerm && i >= 0; --i)
            if (GetSubMenu(mSend, i) == mTerm) { termPos = i; break; }
        if (mTerm && termPos >= 0)
            EnableMenuItem(mSend, termPos, MF_BYPOSITION | (cfg_.term_enable ? MF_ENABLED : MF_GRAYED));
    }
    if (mSerial) {
        CheckMenuItem(mSerial, IDM_SERIAL_RECONNECT, MF_BYCOMMAND | (cfg_.auto_reconnect ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mSerial, IDM_SERIAL_OPENSTART, MF_BYCOMMAND | (cfg_.open_on_start ? MF_CHECKED : MF_UNCHECKED));
    }
    if (mView) {
        CheckMenuItem(mView, IDM_VIEW_TS_COLOR,   MF_BYCOMMAND | (cfg_.split_show ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mView, IDM_VIEW_AUTOSCROLL, MF_BYCOMMAND | (cfg_.auto_scroll ? MF_CHECKED : MF_UNCHECKED));
    }
    if (mSend) CheckMenuItem(mSend, IDM_SEND_NEWLINE, MF_BYCOMMAND | (cfg_.send_newline ? MF_CHECKED : MF_UNCHECKED));
    if (mTerm) {
        // §110：终端仿真有消费方（M6 首增量）——勾选态=真实配置；回显仅按键方式可用（置灰）
        CheckMenuItem(mTerm, IDM_TERM_ENTER, MF_BYCOMMAND | (cfg_.term_enter_send ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mTerm, IDM_TERM_KEY,   MF_BYCOMMAND | (cfg_.term_key_send ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mTerm, IDM_TERM_ECHO,  MF_BYCOMMAND |
                      ((cfg_.term_key_send && cfg_.term_key_echo) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(mTerm, IDM_TERM_LF,    MF_BYCOMMAND | (cfg_.term_lf_send ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(mTerm, IDM_TERM_ECHO, MF_BYCOMMAND | (cfg_.term_key_send ? MF_ENABLED : MF_GRAYED));
    }
    // 单选组：模式（通讯端口）与编码（§78：标记一律系统 ✓，不用圆点）
    HMENU mPort = GetSubMenu(mb, 0), mEnc = GetSubMenu(mb, 2);
    if (mPort) {
        int idx = 0;
        for (int i = 0; i < 4; ++i) if (cfg_.mode == kModeVals[i]) idx = i;
        CheckOneOf(mPort, IDM_MODE_BASE, IDM_MODE_BASE + 3, IDM_MODE_BASE + idx);
    }
    if (mEnc) {
        int idx = 0;
        for (int i = 0; i < kEncCount; ++i) if (cfg_.encoding == kEncodings[i].value) idx = i;
        CheckOneOf(mEnc, IDM_ENC_BASE, IDM_ENC_BASE + kEncCount - 1, IDM_ENC_BASE + idx);
    }
    // 显示缓冲上限档位单选（500K/1/5/20/50/100M；按当前 cap 就近选档）
    HMENU mBuf = nullptr;
    {
        HMENU mb2 = GetMenu(hwnd_);
        HMENU mv = mb2 ? GetSubMenu(mb2, 3) : nullptr;          // 显示
        // §73 修复：§67 插入「背景颜色」后显示缓冲上限子菜单索引 1→2（旧索引取到
        // 非弹出项返回 NULL → 勾选逻辑从未执行——用户报"选择后没有勾选"）
        if (mv) mBuf = GetSubMenu(mv, 2);                       // 显示缓冲上限
    }
    if (mBuf) {
        static const int kBufCaps[6] = { 500 * 1024, 1024 * 1024, 5 << 20, 20 << 20, 50 << 20, 100 << 20 };
        int sel = 1;
        int best = 0x7FFFFFFF;
        for (int i = 0; i < 6; ++i) {
            int d = cfg_.buffer_cap > kBufCaps[i] ? cfg_.buffer_cap - kBufCaps[i] : kBufCaps[i] - cfg_.buffer_cap;
            if (d < best) { best = d; sel = i; }                 // 就近选档（兼容旧配置 10M 等）
        }
        CheckOneOf(mBuf, IDM_BUF_BASE, IDM_BUF_BASE + 5, IDM_BUF_BASE + sel);
    }
}

void MainWindow::Notify(const wchar_t* text) {
    // §74（审查 F3）：记录最近通知——Layout 重发 SB_SETPARTS 后以此为重写来源
    //（此前通知段无重写来源，跨窄窗阈值往返后永久空白）
    noticeText_ = text ? text : L"";
    if (status_) SendMessageW(status_, SB_SETTEXTW, 2, (LPARAM)noticeText_.c_str());   // §66：第 3 段=通知
}

void MainWindow::OnCommandImpl(int id, int code) { // 前置声明实现（见 Handle）
    // §138：HEX 显示/分包勾选变化 → 显示 tick 回快拍（空闲降档期间勾选立即生效，不等慢拍）
    if (id == IDC_CHK_HEXSHOW || id == IDC_CHK_SPLIT) RequestFastDisplay();
    // 编辑框/下拉的焦点、更新、滚动事件也经 WM_COMMAND 上报，不是用户命令——
    // 点击发送输入框曾让 EN_SETFOCUS 落进尾部兜底，误报「该控件：当前版本不可用」。
    // 只滤当前无任何消费方的类别；EN_CHANGE / CBN_SELCHANGE 有消费方，必须放行。
    switch (code) {
    case EN_SETFOCUS: case EN_KILLFOCUS: case EN_UPDATE:
    case EN_ERRSPACE: case EN_MAXTEXT: case EN_HSCROLL: case EN_VSCROLL:
    case CBN_SETFOCUS: case CBN_KILLFOCUS: case CBN_DROPDOWN:
    case CBN_CLOSEUP: case CBN_SELENDOK: case CBN_SELENDCANCEL:
        return;
    }
    switch (id) {
    case IDM_HELP_UPDATE: {                        // §79（用户拍板）：检查更新=打开发布页
        // ← PyCom _open_release_page：应用内不联网（最新版发布与下载在网页进行）
        HINSTANCE r = ShellExecuteW(hwnd_, L"open",
                                    L"https://github.com/smartmx/SNCom/releases/latest",
                                    nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32)
            MessageBoxW(hwnd_, L"无法打开浏览器，请手动访问\n"
                               L"https://github.com/smartmx/SNCom/releases/latest",
                        L"检查更新", MB_OK | MB_ICONINFORMATION);
        else
            Notify(L"已在浏览器打开发布页（应用内不联网）");   // §81（审查 Q7）：名实相符
        break;
    }
    case IDM_HELP_ABOUT:
        MessageBoxW(hwnd_,
                    L"SNCom 串口/网络数据调试器\n"
                    L"版本：" SNCOM_VER_N L"\n"
                    L"系统要求：Windows 7 SP1 及以上；32/64 位\n\n"
                    L"开发者：CoderNotCutie\n"
                    L"代码仓库：https://github.com/smartmx/SNCom\n\n"
                    L"基于 C++17 + Win32 API（原生 GDI + Windows 串口/Winsock）构建。\n"
                    L"功能：串口 / TCP 客户端 / TCP 服务端 / UDP 收发调试，\n"
                    L"多条发送、校验、文件发送、数据查找、转存日志等。\n\n"
                    L"开源许可：MIT License（Copyright © 2026 CoderNotCutie）\n"
                    L"使用库：Windows SDK / comctl32 / MSVC 静态 CRT（无第三方运行时）",
                    L"关于 SNCom", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_VIEW_FONTCOLOR: ShowFontColorDialog(); break;
    case IDM_VIEW_BG:        ShowBgColorDialog(); break;    // §67：独立背景颜色入口
    case IDM_VIEW_BUF:       break;                        // 已改为 IDM_BUF_BASE 档位子菜单
    case IDM_SERIAL_HELP:    // 续修5：状态栏放不下 → 弹窗显示（用户拍板）
        MessageBoxW(hwnd_,
                    L"串口：选端口点「打开串口」；改波特率等参数自动重开；\n"
                    L"断线自动重连（可菜单关闭）；引脚指示需真实连接。",
                    L"串口设置 帮助", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_SEND_HELP:      // 续修5：状态栏放不下 → 弹窗显示（用户拍板）
        MessageBoxW(hwnd_,
                    L"发送：Ctrl+Enter 直接发送；勾「定时发送」后点「发 送」启动周期发送\n"
                    L"（按钮变「停止发送」）；多条字符串在「扩展」面板；发送文件经路径框\n"
                    L"+「发送文件」按钮；协议文件发送在「扩展 → 协议发送」页。",
                    L"发送 帮助", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_SEND_FILEDELAY: {                     // 发送文件延时五档（← PyCom 连续/1/10/50/100）
        HMENU pm = CreatePopupMenu();
        struct { const wchar_t* t; int ms; } items[5] = {
            { L"连续发送", 0 }, { L"1 毫秒", 1 }, { L"10 毫秒", 10 },
            { L"50 毫秒", 50 }, { L"100 毫秒", 100 } };
        for (int i = 0; i < 5; ++i)
            AppendMenuW(pm, MF_STRING | (cfg_.file_delay_ms == items[i].ms ? MF_CHECKED : 0),
                        310 + i, items[i].t);
        RECT rc{};
        GetWindowRect(btn_sendfile_, &rc);
        int pick = TrackPopupMenu(pm, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                  rc.left, rc.bottom, 0, hwnd_, nullptr);
        DestroyMenu(pm);
        if (pick >= 310 && pick <= 314) {
            cfg_.file_delay_ms = items[pick - 310].ms;
            Notify((L"发送文件延时：" + std::wstring(items[pick - 310].t)).c_str());
        }
        break;
    }
    case IDC_BTN_OPENFILE: {                       // 打开文件（E：发送文件路径）
        wchar_t file[MAX_PATH]{};
        GetWindowTextW(edit_file_, file, MAX_PATH);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) SetWindowTextW(edit_file_, file);
        break;
    }
    case IDC_BTN_SENDFILE: {                       // 发送文件（E：256B 块 + 延时档）
        // 门禁必须按当前模式（串口/网络）判断：此前只看串口引擎 -> 网络模式下
        // 恒报「未连接，无法发送文件」，网络发送文件功能不可达（审查 P1-1）
        if (protoActive_) { Notify(L"「协议发送」正在占用发送通道"); break; }   // §129 互斥
        if (LinkIsOpen() && !loopMode_) {
            if (txFile_ != INVALID_HANDLE_VALUE) break;
            wchar_t path[MAX_PATH]{};
            GetWindowTextW(edit_file_, path, MAX_PATH);
            if (!path[0]) { Notify(L"请先选择发送文件"); break; }
            HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf == INVALID_HANDLE_VALUE) { Notify(L"发送文件：读取失败"); break; }
            LARGE_INTEGER sz{};
            GetFileSizeEx(hf, &sz);
            txFile_ = hf;
            txFileSize_ = (unsigned long long)sz.QuadPart;
            txFilePos_ = 0;
            txFileStalled_ = false;
            SendMessageW(progress_, PBM_SETRANGE, 0, MAKELPARAM(0, (short)min(txFileSize_, 65535)));
            SendMessageW(progress_, PBM_SETPOS, 0, 0);
            ShowWindow(progress_, SW_SHOW);
            SetTimer(hwnd_, 9, (UINT)max(1, cfg_.file_delay_ms), nullptr);   // 连续=1ms 节流
            wchar_t ok[160];
            swprintf(ok, 160, L"发送文件：%ls（%llu 字节，延时 %d ms）",
                     path, txFileSize_, cfg_.file_delay_ms);
            Notify(ok);
        } else if (loopMode_) {
            Notify(L"软件回环模式下发送文件无意义（演示用）");
        } else {
            Notify(L"未连接，无法发送文件");
        }
        break;
    }
    case IDC_BTN_STOPFILE:                         // 停止（E）
        KillTimer(hwnd_, 9);
        if (txFile_ != INVALID_HANDLE_VALUE) { CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE; }
        txFilePend_.clear();
        Notify(L"发送文件已停止");
        break;
    case IDC_CHK_TIMED: {                          // 定时发送（UI-4：勾选仅配置，点「发送」才启动）
        timedArmed_ = SendMessageW(chk_timed_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        cfg_.send_timed = timedArmed_;             // §132：勾选态持久化（重启只复勾选，不开跑）
        if (!timedArmed_ && timedRunning_) {       // 取消勾选：立即停止定时发送
            KillTimer(hwnd_, 10);
            timedRunning_ = false;
            SetWindowTextW(btn_send_, L"发  送");
            Notify(L"定时发送已停止");
        } else if (timedArmed_) {
            Notify(L"定时发送已就绪：点「发 送」按钮启动");
        }
        break;
    }
    case IDM_SERIAL_RECONNECT: cfg_.auto_reconnect = !cfg_.auto_reconnect; SyncMenuChecks(); break;
    case IDM_SERIAL_OPENSTART: cfg_.open_on_start  = !cfg_.open_on_start;  SyncMenuChecks(); break;
    case IDM_TERM_ENABLE:                                 // §140：总开关
        cfg_.term_enable = !cfg_.term_enable;
        ApplyTerminal();
        SyncMenuChecks();
        Notify(cfg_.term_enable ? L"终端仿真：已启用（接收区可键入）"
                                : L"终端仿真：已停用（接收区不响应键盘）");
        break;
    case IDM_TERM_ENTER:                                  // §110：方式A（互斥单选）
        if (!cfg_.term_enable) break;                     // §140：总闸防守（子菜单已置灰，双保险）
        cfg_.term_enter_send = true;
        cfg_.term_key_send = false;
        ApplyTerminal();
        SyncMenuChecks();
        Notify(L"终端仿真：回车后发送新行（带回显）");
        break;
    case IDM_TERM_KEY:                                    // §110：方式B（互斥单选）
        if (!cfg_.term_enable) break;                     // §140：总闸防守
        cfg_.term_enter_send = false;
        cfg_.term_key_send = true;
        ApplyTerminal();
        SyncMenuChecks();
        Notify(L"终端仿真：按键立即发送该键值");
        break;
    case IDM_TERM_ECHO:                                   // §110：本地回显（仅方式B可开）
        if (!cfg_.term_enable) break;                     // §140：总闸防守
        if (!cfg_.term_key_send) { Notify(L"本地回显仅与「按键立即发送」方式搭配"); break; }
        cfg_.term_key_echo = !cfg_.term_key_echo;
        ApplyTerminal();
        SyncMenuChecks();
        break;
    case IDM_TERM_LF:                                     // §110：回车 0x0d 后自动补 0x0a
        if (!cfg_.term_enable) break;                     // §140：总闸防守
        cfg_.term_lf_send = !cfg_.term_lf_send;
        SyncMenuChecks();
        break;
    case IDC_BTN_REFRESH:    RefreshPorts(); break;   // 续修5：菜单「刷新串口列表」已删（用户拍板）
    case IDM_TOOL_LOOP:      LoopbackDemo(); break;
    case IDM_VIEW_FIND:
    case IDC_BTN_FIND:       ToggleFind(); break;
    case IDC_BTN_TOP:        ToggleTopmost(); break;
    case IDC_BTN_NEW:        NewWindow(); break;
    case IDC_BTN_COLLAPSE:
        collapsed_ = (SendMessageW(btn_collapse_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        ApplyCollapse();
        break;
    case IDC_BTN_EXPAND:     ToggleExt(SendMessageW(btn_expand_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
    case IDC_FIND_PREV:      FindJump(-1); break;
    case IDC_FIND_NEXT:      FindJump(1); break;
    case IDC_BTN_SAVEPARAMS: SaveConfig(); Notify(L"参数已保存"); break;
    case IDC_BTN_SEND:                             // 发送/停止发送（UI-4：定时发送经此启停）
        if (timedRunning_) {
            KillTimer(hwnd_, 10);
            timedRunning_ = false;
            SetWindowTextW(btn_send_, L"发  送");
            Notify(L"定时发送已停止");
            break;
        }
        if (timedArmed_) {
            if (protoActive_) { Notify(L"「协议发送」正在占用发送通道"); break; }   // §129 互斥
            wchar_t b[32]{};
            GetWindowTextW(edit_interval_, b, 32);
            int ms = _wtoi(b);
            SetTimer(hwnd_, 10, (UINT)max(1, ms > 0 ? ms : cfg_.send_interval_ms), nullptr);
            timedRunning_ = true;
            SetWindowTextW(btn_send_, L"停止发送");
            Notify(L"定时发送已开启");
            break;
        }
        SendCurrentEdit();
        break;
    case IDC_BTN_CLEAR:
        ClearDisplayAll();
        Notify(L"已清除接收窗口");
        break;
    case IDC_BTN_SAVEDATA:   SaveReceived(); break;
    case IDC_CHK_RXFILE:
        ToggleRxLog(SendMessageW(chk_rx_file_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        break;
    case IDC_CHK_BREAK: {
        // §76：BS_OWNERDRAW 按钮不维护 BM_*CHECK 状态（实测外层 BM_SETCHECK 亦无效）
        // ——勾选态自持 breakChecked_
        breakChecked_ = !breakChecked_;
        engine_.SetBreak(breakChecked_);               // B3 接线
        InvalidateRect(chk_break_, nullptr, TRUE);
        break;
    }
    case IDC_BTN_OPEN:
        if (engine_.IsOpen()) CloseSerial();
        else if (reconnecting_) CancelReconnect();      // 重连中按钮=「取消」（B1）
        else { wantOpen_ = true; TryOpenSerial(false); }
        break;
    case IDC_CHK_AUTOSCROLL: {
        cfg_.auto_scroll = (SendMessageW(chk_autoscroll_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        rxv_.SetFollow(cfg_.auto_scroll);          // 勾选 → 立即跳回最底（PyCom 语义）
        SyncMenuChecks();
        break;
    }
    case IDC_CHK_HEXSHOW:    cfg_.hex_mode    = (SendMessageW(chk_hex_show_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
    case IDC_CHK_SPLIT:      cfg_.split_show  = (SendMessageW(chk_split_, BM_GETCHECK, 0, 0) == BST_CHECKED); SyncMenuChecks(); break;
    case IDC_CHIP_TS:        if (code == STN_CLICKED) ShowTsStyleDialog(); break;   // §67：色块点击 → 样式弹窗
    case IDC_CHK_HEXSEND:    cfg_.send_hex    = (SendMessageW(chk_hex_send_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                             EnableWindow(chk_newline_, !cfg_.send_hex); break;   // §58：HEX 时灰（发送路径本就不追加）
    case IDC_CHK_NEWLINE:    cfg_.send_newline = (SendMessageW(chk_newline_, BM_GETCHECK, 0, 0) == BST_CHECKED); SyncMenuChecks(); break;
    case IDC_CHK_DTR:
    case IDC_CHK_RTS:        ApplyDtrRts(); break;      // 热改（EscapeCommFunction，B4）
    default:
        // 校验参数联动 → 实时预览（含发送内容框输入）
        if (id == IDC_FIND_EDIT && code == EN_CHANGE) { FindRescanSoon(); break; }
        if ((id == IDC_FIND_CASE || id == IDC_FIND_HEX) && code == BN_CLICKED) { FindRescan(); break; }
        if (id == IDC_CMB_PORT && code == CBN_SELCHANGE) {  // 端口选择持久化（审查 P1-1）
            cfg_.port = CurrentPortName();
            break;
        }
        // B4：连接参数变更 → 自动重开（← PyCom _apply_live_baud；打开时才需要）
        if ((id == IDC_CMB_BAUD || id == IDC_CMB_DATABITS || id == IDC_CMB_STOPBITS ||
             id == IDC_CMB_PARITY || id == IDC_CMB_FLOW) && code == CBN_SELCHANGE) {
            if (id == IDC_CMB_BAUD) {
                if ((int)SendMessageW(cmb_baud_, CB_GETCURSEL, 0, 0) == baudCustomIdx_) {
                    OpenBaudInput();                   // 「自定义」→ 弹输入窗（确定后内部重开）
                    break;
                }
                wchar_t bb[32]{};
                GetWindowTextW(cmb_baud_, bb, 32);
                baudLastValid_ = bb;                   // 预设值即有效值
                if (engine_.IsOpen() && !loopMode_)
                    Notify(L"波特率已切换（串口已自动重开）");
                ReopenSerial();
                break;
            }
            ReopenSerial();
            break;
        }
        if (id == IDC_BTN_NCONN) {                          // 连接/侦听（M4）
            CloseNet();                                     // ← PyCom _open_link：先静默关旧链路
            wantOpen_ = true;                               // CloseNet 撤意图，须在其后置位
            TryOpenNet();
            break;
        }
        if (id == IDC_BTN_NDISC) { CloseNet(); break; }     // 断开（重连中点击=取消重连）
        if (id == IDC_BTN_NICREF) { RefreshNics(); break; }
        if (id == IDC_CMB_NIC && code == CBN_SELCHANGE) { RefreshLocalIps(); break; }
        if (!uiInitDone_) break;                     // §77：建窗期 EN_CHANGE 不回写 cfg（见 .h 说明）
        if (id == IDC_EDIT_INTERVAL && code == EN_CHANGE) {          // §132：定时发送间隔持久化
            wchar_t b[32]{};
            GetWindowTextW(edit_interval_, b, 32);
            int ms = _wtoi(b);
            if (ms >= 1 && ms <= 3600000) cfg_.send_interval_ms = ms;   // 越界不入（启动时再钳）
            break;
        }
        if ((id == IDC_EDIT_SEND && code == EN_CHANGE) ||
            (id == IDC_CMB_CHECKSUM && code == CBN_SELCHANGE) ||
            (id == IDC_CMB_CHKTAIL && code == CBN_SELCHANGE) ||
            (id == IDC_EDIT_CHKSTART && code == EN_CHANGE) ||
            id == IDC_CHK_HEXSEND) {
            if (id == IDC_CMB_CHECKSUM || id == IDC_CMB_CHKTAIL || id == IDC_EDIT_CHKSTART) SyncChecksumCfg();
            UpdateChecksumPreview();
            break;
        }
        if (id >= IDM_BUF_BASE && id < IDM_BUF_BASE + 6) {           // 显示缓冲上限档位
            static const int kBufCaps[6] = { 500 * 1024, 1024 * 1024, 5 << 20, 20 << 20, 50 << 20, 100 << 20 };
            cfg_.buffer_cap = kBufCaps[id - IDM_BUF_BASE];
            buf_.set_cap((size_t)cfg_.buffer_cap);
            SyncMenuChecks();
            const wchar_t* t[6] = { L"500 KB", L"1 MB", L"5 MB", L"20 MB", L"50 MB", L"100 MB" };
            Notify((L"显示缓冲上限：" + std::wstring(t[id - IDM_BUF_BASE]) + L"（.bin 导出量随之变化）").c_str());
            break;
        }
        if (id >= IDM_MODE_BASE && id < IDM_MODE_BASE + 4) {
            // 切换模式必须先关闭旧连接（← PyCom _set_mode：否则旧连接继续占用串口/收发且状态错乱）
            CloseLink();
            cfg_.mode = kModeVals[id - IDM_MODE_BASE];
            SyncMenuChecks();
            ApplyModeColumns();            // 左列串口区/网络区换列
            Notify((std::wstring(L"模式切换：") + kModes[id - IDM_MODE_BASE]).c_str());
        } else if (id >= IDM_ENC_BASE && id < IDM_ENC_BASE + kEncCount) {
            cfg_.encoding = kEncodings[id - IDM_ENC_BASE].value;
            builder_.set_encoding(cfg_.encoding);   // 半截尾巴先落 U+FFFD，后续数据用新码（PyCom 同款）
            SyncMenuChecks();
            Notify((std::wstring(L"编码切换：") + cfg_.encoding).c_str());
        } else if (id == IDM_VIEW_TS_COLOR)   { cfg_.split_show = !cfg_.split_show; SyncMenuChecks();
                                                SendMessageW(chk_split_, BM_SETCHECK, cfg_.split_show ? BST_CHECKED : BST_UNCHECKED, 0); }
        else if (id == IDM_VIEW_AUTOSCROLL)   { cfg_.auto_scroll = !cfg_.auto_scroll; SyncMenuChecks();
                                                SendMessageW(chk_autoscroll_, BM_SETCHECK, cfg_.auto_scroll ? BST_CHECKED : BST_UNCHECKED, 0);
                                                rxv_.SetFollow(cfg_.auto_scroll); }
        else if (id == IDM_SEND_NEWLINE)      { cfg_.send_newline = !cfg_.send_newline; SyncMenuChecks();
                                                SendMessageW(chk_newline_, BM_SETCHECK, cfg_.send_newline ? BST_CHECKED : BST_UNCHECKED, 0); }
        // §107（用户报两枚小工具「无效」）：设备管理器/文件资源管理器是**外部程序**，
        // 此前被下一行的区间（…<=IDM_TOOL_EXPLORER）一并吞掉、只切扩展窗页 0 → 看似无反应。
        else if (id == IDM_TOOL_DEV)      { OpenDeviceManager(); }
        else if (id == IDM_TOOL_EXPLORER) { OpenComputerFolder(); }
        else if (id >= IDM_TOOL_CALC && id <= IDM_TOOL_MS) {  // 小工具三页 → 扩展小窗切页（D5）
            ToggleExt(true);
            int page = (id == IDM_TOOL_CALC) ? 1 : (id == IDM_TOOL_ASCII) ? 2 : 0;
            SendMessageW(extTab_, TCM_SETCURSEL, (WPARAM)page, 0);
            PostMessageW(extWnd_, WM_APP + 0x111, (WPARAM)page, 0);   // 切页显隐
        }
        else if (id >= IDC_BASE) Notify(L"该控件：当前版本不可用");
        break;
    }
}

void MainWindow::SaveConfig() {
    RECT rc{}; GetWindowRect(hwnd_, &rc);
    if (IsIconic(hwnd_)) {                         // §132：最小化态关闭时 GetWindowRect 是图标矩形
        WINDOWPLACEMENT wp{};                      //（实测 160×28）——曾致下次启动缩成一条；取还原尺寸
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd_, &wp)) rc = wp.rcNormalPosition;
    }
    cfg_.win_w = rc.right - rc.left;
    cfg_.win_h = rc.bottom - rc.top;
    SyncSerialCfg();                               // 保存参数：控件值认入配置（P3-3）
    if (cfg_.mode != L"serial") SyncNetCfg();      // 网络参数同口径认入
    cfg_.copy_with_anno = rxv_.copy_with_anno;      // 右键两开关随参数保存（PyCom 同款）
    cfg_.save_with_anno = rxv_.save_with_anno;
    {                                              // §132：定时/循环/多条表兜底同步（UI 直读）
        wchar_t b[32]{};
        GetWindowTextW(edit_interval_, b, 32);
        int ms = _wtoi(b);
        if (ms >= 1 && ms <= 3600000) cfg_.send_interval_ms = ms;
        cfg_.send_timed = SendMessageW(chk_timed_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        cfg_.multisend_loop = msLoopActive_;
        MsSyncToCfg();
    }
    if (!cfg_.Save(cfgPath_)) Notify(L"参数保存失败（目录不可写？）");
}

namespace {
// §98f：本线程是否处于菜单模态循环（GrayifyMenuBar 位置模型用）
bool InMenuModeOfThisThread() {
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &gti)) return false;
    return (gti.flags & GUI_INMENUMODE) != 0;
}
}  // namespace

bool MainWindow::MenubarNeedsGray() {
    // §98e：轻量守卫——菜单栏矩形内网格采样 18 点；发现"浅色且非 240"（系统重绘的白底/
    // 残留高亮，深色文字与 240 本身忽略）即判定需要全刷。
    // §120（十八轮 P3-2 空闲 CPU 专项）：GetPixel 每点一次 GDI 回读往返，16.7Hz×18 点
    // 是空闲 CPU 的大头 → 改**一次 BitBlt 到缓存 DIB**再内存读点；采样点集与判据逐字节
    // 不变（门禁=verify_r97 系回归 + 真机菜单栏目检）。
    MENUBARINFO mbi{};
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hwnd_, OBJID_MENU, 0, &mbi)) return false;
    int bw = mbi.rcBar.right - mbi.rcBar.left;
    int bh = mbi.rcBar.bottom - mbi.rcBar.top;
    if (bw <= 8 || bh <= 4) return false;
    if (bw > 4096 || bh > 64) return false;
    if (IsIconic(hwnd_)) return false;             // 最小化：菜单栏不可见，也不得读陈旧 DIB
    // §120：仅前台守护——后台窗口的菜单栏残留用户不可见；置回前台必经 WM_NCACTIVATE
    // （§98c 已挂"直绘后补刷"），届时 GrayifyMenuBar 立即纠偏，无需后台持续采样。
    if (GetForegroundWindow() != hwnd_) return false;
    if (!grayMem_ || grayBw_ != bw || grayBh_ != bh) {          // 尺寸变化（DPI/布局）才重建
        if (grayBmp_) {
            if (grayMem_ && grayOld_) SelectObject(grayMem_, grayOld_);
            DeleteObject(grayBmp_);
            grayBmp_ = nullptr;
        }
        if (grayMem_) { DeleteDC(grayMem_); grayMem_ = nullptr; }
        grayBits_ = nullptr;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bw;
        bi.bmiHeader.biHeight = -bh;                  // top-down DIB
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC wdc = GetWindowDC(hwnd_);
        grayMem_ = CreateCompatibleDC(wdc);
        grayBmp_ = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &grayBits_, nullptr, 0);
        ReleaseDC(hwnd_, wdc);
        if (!grayMem_ || !grayBmp_ || !grayBits_) {
            if (grayBmp_) { DeleteObject(grayBmp_); grayBmp_ = nullptr; }
            if (grayMem_) { DeleteDC(grayMem_); grayMem_ = nullptr; }
            grayBits_ = nullptr;
            return false;                             // 宁可不守卫（与原 GetPixel 失败路径同口径）
        }
        grayOld_ = SelectObject(grayMem_, grayBmp_);
        grayBw_ = bw; grayBh_ = bh;
    }
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    int ox = mbi.rcBar.left - wr.left, oy = mbi.rcBar.top - wr.top;
    HDC dc = GetWindowDC(hwnd_);
    BOOL blt = BitBlt(grayMem_, 0, 0, bw, bh, dc, ox, oy, SRCCOPY);
    ReleaseDC(hwnd_, dc);
    if (!blt) return false;                        // 回读失败：按"无需灰刷"处理，不读陈旧数据
    bool need = false;
    const uint8_t* p = (const uint8_t*)grayBits_;     // BGRA 顺序
    for (int gy = 0; gy < 3 && !need; ++gy)
        for (int gx = 0; gx < 6 && !need; ++gx) {
            int x = 2 + gx * (bw - 4) / 5;
            int y = 1 + gy * (bh - 2) / 2;
            const uint8_t* q = p + ((size_t)y * bw + x) * 4;
            int mn = min(min(q[0], q[1]), q[2]);
            if (mn >= 200 && !(q[0] == 240 && q[1] == 240 && q[2] == 240)) need = true;
        }
    return need;
}

void MainWindow::GrayifyMenuBar() {
    // §98b/§98c（用户拍板：菜单栏文字行统一灰；标题栏/边框/文字/局部高亮保持系统原样）：
    // 取菜单栏矩形 → 统计**区域主色**（占比≥50%）→ 若主色是"浅色"（min≥200，不限中性，
    // 系统 accent 淡底如 (245,243,222) 也要盖掉）则把该色像素整体替换为 BTNFACE(240)。
    // 局部 hover/选中高亮是少数色 → 不受影响（保留交互反馈）。
    // 触发点：WM_NCPAINT（系统画完 NC 后）与 WM_NCACTIVATE（活动切换时 DefWindowProc 直绘
    // NC 不发前者——§98c 复报"默认仍白"的根因）。
    grayQuiet_ = 0;                        // §120：任何全刷都是菜单区活动的证据 → 回快拍
    if (!hwnd_) return;
    MENUBARINFO mbi{};
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hwnd_, OBJID_MENU, 0, &mbi)) return;
    int bw = mbi.rcBar.right - mbi.rcBar.left;
    int bh = mbi.rcBar.bottom - mbi.rcBar.top;
    if (bw <= 0 || bh <= 0 || bw > 4096 || bh > 64) return;
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    int ox = mbi.rcBar.left - wr.left, oy = mbi.rcBar.top - wr.top;   // 屏幕→窗口坐标
    HDC dc = GetWindowDC(hwnd_);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;                  // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, bw, bh, dc, ox, oy, SRCCOPY);
        int n = bw * bh;
        // §98g 定稿（用户第六轮拍板："移动到项上一律不变白；**只有点开时才白**"）：
        //   白块唯一来源 = 当前被点开的顶菜单项（openedMenuItem_，WM_INITMENUPOPUP 记录）；
        //   该顶项矩形内浅色 → 255；**其余一切浅色（系统悬停高亮/恢复白/残留白）→ 240**。
        //   深色文字与 ClearType 彩边（min<200）不动。
        //   （历史教训链：§98d 块识别→§98f 位置模型→本版——"悬停白/块识别把恢复白也
        //   保护成白"两类歧义均由"只认打开项"根除。）
        {
            bool hasWhite = false;
            RECT wDib{};
            if (openedMenuItem_ >= 0 && InMenuModeOfThisThread()) {
                HMENU mb = GetMenu(hwnd_);
                RECT ir{};
                if (mb && GetMenuItemRect(hwnd_, mb, openedMenuItem_, &ir) && !IsRectEmpty(&ir)) {
                    wDib = { ir.left - mbi.rcBar.left, ir.top - mbi.rcBar.top,
                             ir.right - mbi.rcBar.left, ir.bottom - mbi.rcBar.top };
                    hasWhite = true;
                }
            }
            uint8_t* p = (uint8_t*)bits;          // BGRA 顺序
            for (int i = 0; i < n; ++i, p += 4) {
                int mn = min(min(p[0], p[1]), p[2]);
                if (mn < 200) continue;                                   // 深色/彩边不动
                if (hasWhite) {
                    int x = i % bw, y = i / bw;
                    if (x >= wDib.left && x < wDib.right &&
                        y >= wDib.top && y < wDib.bottom) {
                        p[0] = p[1] = p[2] = 255;                         // 被点开的项 → 白
                        continue;
                    }
                }
                p[0] = p[1] = p[2] = 240;                                 // 其余浅色一律压灰
            }
        }
        BitBlt(dc, ox, oy, bw, bh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        // §98d：rcBar 之下 2px 是菜单栏底边线区（不在 rcBar 内）——菜单循环退出后系统
        // 可能把它重绘为浅色，rcBar 替换覆盖不到 → 直接填 BTNFACE，保证上下无缝全灰。
        RECT belt{ox, oy + bh, ox + bw, oy + bh + 2};
        FillRect(dc, &belt, GetSysColorBrush(COLOR_BTNFACE));
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd_, dc);
}

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        dpi_ = DpiForWindowSafe(hwnd_);
        BuildMenus();
        BuildChildren();
        builder_ = LineBuilder(cfg_.encoding, cfg_.hex_mode, cfg_.split_show, cfg_.split_ms);
        RxLineSourceInit();                // H-2：接收区行源直连 builder（零行表拷贝）
        rxv_.SetHexUnits(cfg_.hex_mode);   // §64：折行口径与 HEX 显示模式同步（lastHex_ 已初始化，
                                           // TickDisplay 的变化分支首帧不会触发，必须在此显式同步）
        ApplyTerminal();                   // §110：终端方式/回显按配置应用（接收区直接输入）
        buf_.set_cap((size_t)cfg_.buffer_cap);
        lastHex_ = cfg_.hex_mode;
        lastSplit_ = cfg_.split_show;
        lastSplitMs_ = cfg_.split_ms;
        ApplyFonts();
        Layout();
        SyncMenuChecks();
        UpdateChecksumPreview();                       // 校验值只读框初始联动
        RefreshNics();                                 // 网卡枚举（启动按 local_ip 自动选中）
        ApplyModeColumns();                            // 模式 → 左列串口/网络区换列
        SetTimer(hwnd_, 2, 250, nullptr);             // 状态栏速率统计（PyCom 250ms 口径）
        SetTimer(hwnd_, 4, 33, nullptr);              // 显示管线 tick（builder maybe_flush ≈30fps）
        SetTimer(hwnd_, 6, 300, nullptr);             // 引脚轮询 300ms（← PyCom pin_timer）
        SetTimer(hwnd_, 15, 60, nullptr);             // §98e：菜单栏守卫 tick（采样异常才全刷；
                                                      // 悬停/去高亮的系统直绘不经过任何窗口消息）
        uiInitDone_ = true;                           // §77：建窗期通知闸门解除（此前 EN_CHANGE 是构建噪声）
        if (cfg_.wasCorrupt)                          // §99/§100：配置读取失败或已损坏，已重置；
                                                      // 确证损坏的坏文件另存 sncom.json.bad（解析失败才改名）
            Notify(L"配置文件读取失败或已损坏，已重置为默认");
        if (selftest_) SelftestInit();
        {
            std::wstring tip = selftest_
                ? L"就绪｜--spike-selftest：200 行渲染 + 回环收发自检中"
                : L"就绪｜小工具→回环演示可无硬件体验收发；菜单「串口设置/发送」有帮助";
            Notify(tip.c_str());
        }
        PostMessage(hwnd_, WM_APP + 0x112, 0, 0);   // UI-6：消息循环首轮再评估加宽
        return 0;

    case WM_SIZE:
        Layout();
        if (extOpen_ && extWnd_) {                 // 恢复最小化 → 扩展小窗回显（位置保持用户所放）
            if (!IsWindowVisible(extWnd_) && !IsIconic(hwnd_)) ShowWindow(extWnd_, SW_SHOWNA);
        }
        return 0;

    case WM_DEVICECHANGE:                  // 串口热插拔 → 自动刷新端口下拉
        if (wp != DBT_DEVNODES_CHANGED) break;   // 其余设备事件走默认
        if (!selftest_) RefreshPorts();
        return 0;

    case WM_SYSCOMMAND:                        // 最小化 → 扩展小窗隐藏（D5 验收 6）
        if ((wp & 0xFFF0) == SC_MINIMIZE && extWnd_)
            ShowWindow(extWnd_, SW_HIDE);
        break;
    case WM_APP + 0x112: {                     // UI-6：旧配置窗口窄于最小窗宽需求 → 启动加宽
        Layout();                              // 确保 toolRight/minClientW_ 为当前实测值
        RECT rc{}; GetClientRect(hwnd_, &rc);
        int frame = GetSystemMetrics(SM_CXSIZEFRAME) * 2
                  + GetSystemMetrics(SM_CXPADDEDBORDER);
        int need = minClientW_ + frame;
        int cur = rc.right + frame;
        // §85：旧配置高度过矮（低于最小窗高）也要抬起来——否则启动即"接收区压住面板行"
        // §88（审查 T1）：frameY 同改实测差（菜单栏 24px 曾被漏算）
        RECT wr2{};
        GetWindowRect(hwnd_, &wr2);
        int frameY = (wr2.bottom - wr2.top) - (rc.bottom - rc.top);
        int needH = minClientH_ + frameY;
        int curH = rc.bottom + frameY;
        if (cur < need || curH < needH) {
            RECT wr{}; GetWindowRect(hwnd_, &wr);
            SetWindowPos(hwnd_, nullptr, 0, 0, cur < need ? need : wr.right - wr.left,
                         curH < needH ? needH : wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        // §102（审查 P2-B）：接通「启动立刻打开串口」（规格 docs/03 §27，此前为死功能——
        // 菜单可勾选可持久化但无消费点）。此处=消息循环首轮（端口枚举/尺寸钳制已完成），
        // 与手动点「打开串口」同姿势（wantOpen_=true → 断线自动重连随之生效）。
        if (cfg_.open_on_start && cfg_.mode == L"serial" && !selftest_ &&
            !engine_.IsOpen() && !wantOpen_)
        {
            wantOpen_ = true;
            TryOpenSerial(false);
        }
        return 0;
    }
    case WM_APP + 0x110:                       // 扩展小窗 × 关闭 → 按钮弹起同步（D5 验收 7）
        extOpen_ = false;
        SendMessageW(btn_expand_, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(btn_expand_, L"扩展");
        return 0;

    case WM_GETMINMAXINFO: {
        // UI-3：窗口向左收缩不允许越过「发 送」按钮右边界（Layout 实测左列宽 + 边距）
        auto* mmi = (MINMAXINFO*)lp;
        int frame = GetSystemMetrics(SM_CXSIZEFRAME) * 2
                  + GetSystemMetrics(SM_CXPADDEDBORDER);
        mmi->ptMinTrackSize.x = minClientW_ + frame;
        // §85/§88（审查 T1）：Y 向非客户区高改**实测差**（GetWindowRect−GetClientRect）——
        // 原按三个系统度量估算（35px）漏了菜单栏 24px（本窗 SetMenu，实测 59），导致
        // 最小窗高下接收区只有 66px < kRxMinH(90)。实测差随 DPI/主题/菜单自动跟手。
        RECT wr{}, rc2{};
        GetWindowRect(hwnd_, &wr);
        GetClientRect(hwnd_, &rc2);
        int frameY = (wr.bottom - wr.top) - (rc2.bottom - rc2.top);
        mmi->ptMinTrackSize.y = minClientH_ + frameY;
        return 0;
    }

    case WM_ERASEBKGND: {
        // §98：菜单栏整行灰——客户区背景就是 BTNFACE，菜单栏（240）与接收框上边距
        // （kRxTop 带）自然连续；此处显式接管以保证擦除确定性（默认 hbrBackground 同色）。
        HDC dc = (HDC)wp;
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_NCPAINT:
        // §98b/§98c：菜单栏文字行底色改灰（用户拍板），**标题栏/边框/文字/高亮保持系统原样**。
        DefWindowProcW(hwnd_, msg, wp, lp);
        GrayifyMenuBar();
        return 0;

    case WM_NCACTIVATE: {
        // §98c：活动/非活动切换时 DefWindowProc 直绘 NC（不发 WM_NCPAINT）——用户活动窗口
        // 的菜单栏因此保持系统色（复报"默认还是白色"的根因之一）。此处补挂同一替换。
        LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
        GrayifyMenuBar();
        return r;
    }

    case WM_NCMOUSEMOVE:
        // §98e/§98f：菜单栏在非客户区——悬停/热跟踪的系统直绘不发 NCPAINT/MENUSELECT
        // → NC 鼠标移动即"先采样判据再刷"（无条件全刷会让每次划过 NC 的鼠标移动都做
        // 窗口 DC 读改写——r57 灰闪探针实测 0.060→0.239 超阈）。同时注册 NC 离开通知。
        {
            LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            if (MenubarNeedsGray()) GrayifyMenuBar();
            return r;
        }
    case WM_NCMOUSELEAVE:
        // §98f：鼠标离开非客户区（含离开菜单栏）→ 位置模型把一切残留白压回 240（恢复）。
        {
            LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
            if (MenubarNeedsGray()) GrayifyMenuBar();
            return r;
        }

    case WM_ENTERMENULOOP:
        // §98g：进入菜单循环先清"打开项"（若随后点开某顶项，INITMENUPOPUP 会记录并重刷）。
        DefWindowProcW(hwnd_, msg, wp, lp);
        openedMenuItem_ = -1;
        GrayifyMenuBar();
        return 0;
    case WM_EXITMENULOOP:
        DefWindowProcW(hwnd_, msg, wp, lp);
        openedMenuItem_ = -1;
        GrayifyMenuBar();
        return 0;
    case WM_INITMENUPOPUP:
        // §98g：白块的唯一来源——记录"被点开的顶项"。wp=打开的菜单句柄；与各顶项子菜单
        // 逐一比对：匹配 → 记录索引；不匹配（嵌套子菜单/系统菜单）→ 保持原值。
        {
            LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
            HMENU sub = (HMENU)wp;
            HMENU mb = GetMenu(hwnd_);
            int cnt = mb ? GetMenuItemCount(mb) : 0;
            for (int i = 0; i < cnt; ++i)
                if (GetSubMenu(mb, i) == sub) { openedMenuItem_ = i; break; }
            GrayifyMenuBar();
            return r;
        }

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        auto* r = (RECT*)lp;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyFonts();
        Layout();
        return 0;
    }

    case WM_TIMER:
        if (wp == 2) UpdateCounters();
        else if (wp == 3) SelftestTick();
        else if (wp == 4) TickDisplay();
        else if (wp == 5) RxLogPoll();
        else if (wp == 6) PollPins();
        else if (wp == 7) {                           // 自动重连 tick（B1，串口/网络共用）
            if (!wantOpen_) { CancelReconnect(); break; }
            if (cfg_.mode == L"serial") TryOpenSerial(false);
            else TryOpenNet();
            if (reconnecting_) Notify(L"重连中…");     // 持续可见（PyCom 口径）
        }
        else if (wp == 8) FindRescan();               // 查找防抖重扫（D4）
        else if (wp == 9) TxFileTick();               // 发送文件（E）
        else if (wp == 10) SendCurrentEdit();         // 定时发送（UI-4：点「发 送」启动后周期重发）
        else if (wp == 11 && msLoopActive_) MsLoopNext();   // 多条循环发送（E）
        else if (wp == 14) {                        // §96：显示后晚一拍补一次失效（**只置脏，不刷屏**）
            KillTimer(hwnd_, 14);
            if (extOpen_ && extWnd_ && IsWindowVisible(extWnd_)) {
                int sel = (int)SendMessageW(extTab_, TCM_GETCURSEL, 0, 0);
                if (sel == 0) ForceListPaint(msList_);
                else if (sel == 2) ForceListPaint(asciiList_);
                else if (sel == 4)                 // §129 续修2/4：协议+校验+包大小三个组合框
                    for (HWND cb : {cmb_proto_, cmb_xcheck_, cmb_xblk_, edit_proto_timeout_,
                                    edit_proto_retries_})   // §131/续
                        if (cb) InvalidateRect(cb, nullptr, TRUE);   // 同受父窗擦除覆盖，晚一拍置脏
            }
        }
        else if (wp == 15) {                        // §98e：菜单栏守卫 tick（§101 起 SyncExtPageBrush
                                                    // 已随确定性 BTNFACE 底色一并废弃）
            // §120：自适应节流——连续 16 拍干净（~1s）降为 1/4 采样（~240ms）；发现残留
            // （GrayifyMenuBar 触发）立即回快拍。已知残留源（悬停/开合/激活/离开菜单栏）
            // 都有专用钩径即时处理（§98e/§98f），此 tick 只兜未知直绘，慢拍上限 240ms。
            // §138：安静且无待发时把**定时器本体**降到 240ms（§120 只降了采样密度、唤醒
            // 次数没降）——采样密度两者等价（都是 240ms 一次）；菜单活动（专用钩径触发
            // GrayifyMenuBar → grayQuiet_=0）或入队（EnqueueTx → RequestFastDisplay）即回 60ms。
            if (grayQuiet_ >= 16 && txPend_.empty()) {
                if (guardTickMs_ != 240) { guardTickMs_ = 240; SetTimer(hwnd_, 15, 240, nullptr); }
            } else if (guardTickMs_ != 60) {
                guardTickMs_ = 60; SetTimer(hwnd_, 15, 60, nullptr);
            }
            if (guardTickMs_ >= 240 || ++grayTick_ >= (grayQuiet_ >= 16 ? 4 : 1)) {
                grayTick_ = 0;
                if (MenubarNeedsGray()) { GrayifyMenuBar(); grayQuiet_ = 0; }
                else ++grayQuiet_;
            }
            if (!txPend_.empty()) FlushTxPend();    // §99：待发兜底续发——串口引擎队列满拒收后
                                                    // 无 FD_WRITE 类唤醒（仅连接恢复/下次发送触发），
                                                    // 靠此 tick 排空；入队时已回 60ms（§138）
        }
        else if (wp == 16) {                        // §129：协议发送 tick（id 追加末尾，§79 规则）
            ProtoTick();
        }
        else if (wp == 12) {                        // 点击发送按钮按压视觉弹起
            KillTimer(hwnd_, 12);
            if (msPressRow_ >= 0) {
                RECT rr{};
                ListView_GetItemRect(msList_, msPressRow_, &rr, LVIR_BOUNDS);
                msPressRow_ = -1;
                InvalidateRect(msList_, &rr, TRUE);
            }
        }
        else if (wp == 13) {                        // §76：BREAK 黄亮复位
            KillTimer(hwnd_, 13);
            breakLit_ = false;
            if (chk_break_) InvalidateRect(chk_break_, nullptr, TRUE);
        }
        return 0;

    // ---------------- 串口引擎消息（I/O 线程 PostMessage 回传） ----------------
    case kMsgSerialRx: {                            // wp=字节数 lp=new uint8_t[]
        auto* d = (uint8_t*)lp;
        size_t n = (size_t)wp;
        if (d) { OnSerialRx(d, n); delete[] d; }
        return 0;
    }
    case kMsgSerialEvt: {                           // wp = (会话代号<<8) | 事件码
        auto* t = (wchar_t*)lp;
        int gen = (int)(wp >> 8), code = (int)(wp & 0xFF);
        if (gen != engine_.Generation()) { if (t) delete[] t; return 0; }   // 陈旧事件：释放并忽略
        if (t) { OnSerialEvent(code, t); delete[] t; }
        return 0;
    }
    case kMsgSerialSt: {                            // wp = (会话代号<<8) | 打开态
        int gen = (int)(wp >> 8);
        // 陈旧会话的迟到状态必须丢弃：否则"关闭→立刻重开"时旧会话的 St(0) 会把新连接关掉
        // （实测：改波特率/校验位后 40ms 内出现伪断线 + 端口被释放 + 待发被清）
        if (gen != engine_.Generation()) return 0;
        OnSerialState((wp & 1) != 0);
        return 0;
    }

    // ---------------- 网络引擎消息（WSAAsyncSelect → 引擎 post，M4） ----------------
    case kMsgNetRx: {                               // wp = (会话代号<<16)|字节数, lp=new uint8_t[]
        auto* d = (uint8_t*)lp;
        if ((int)(wp >> 16) != net_.Generation()) { if (d) delete[] d; return 0; }
        if (d) { OnNetRx(d, (size_t)(wp & 0xFFFF)); delete[] d; }
        return 0;
    }
    case kMsgNetEvt: {                              // wp = (会话代号<<8) | 事件码
        auto* t = (wchar_t*)lp;
        int gen = (int)(wp >> 8), code = (int)(wp & 0xFF);
        if (gen != net_.Generation()) { if (t) delete[] t; return 0; }
        if (t) { OnNetEvent(code, t); delete[] t; }
        return 0;
    }
    case kMsgNetSt: {                               // wp = (会话代号<<8) | 打开态
        int gen = (int)(wp >> 8);
        if (gen != net_.Generation()) return 0;     // 陈旧会话（CloseNet 已换代号）
        OnNetState((wp & 1) != 0);
        return 0;
    }
    case kMsgNetSock:                               // WSAAsyncSelect 直达：转发引擎分发
        net_.OnSockMsg(wp, lp);
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HWND c = (HWND)lp;
        for (int i = 0; i < kPinCount; ++i) {                 // 线路引脚指示框（三色：灰/绿/红/黄）
            if (c == pin_[i]) {
                HBRUSH br = pinBrush_;
                COLORREF bg = RGB(0xE6, 0xE6, 0xE6);          // ← PyCom 灰#9E9E9E/绿#2ECC40/红#FF4136
                if (pinActive_[i] && i == 4) { br = errBrush_; bg = RGB(0xFF, 0x41, 0x36); }
                else if (pinActive_[i]) { br = okBrush_; bg = RGB(0x2E, 0xCC, 0x40); }
                if (br) {
                    SetBkColor((HDC)wp, bg);
                    return (LRESULT)br;
                }
            }
        }
        // §104（审查第十三轮 P3-3）：其余 STATIC/BUTTON 统一为**显式**「SetBkColor+返回刷」
        // （此前靠 DefWindowProc 隐式设背景，结果对但姿势不一致——显式化防后人仿写出
        // 「只返回刷」再次引入字形白块，§103 真根因与扩展窗/TsStyleProc 同款守卫）。
        SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_DRAWITEM: {                                       // 自绘：连接圆点 / 时间戳色块
        auto* di = (DRAWITEMSTRUCT*)lp;
        if (di && di->CtlID == IDC_RX_TINT) {
            // §77：RX 段显示缓冲占用染色（60%→100% 灰→淡红渐变，SSCOM V5.07 语义）
            RECT& rc = di->rcItem;
            int t = rxTintPct_ < 60 ? 0 : (rxTintPct_ - 60) * 255 / 40;
            if (t > 255) t = 255;
            HBRUSH br = CreateSolidBrush(RGB(240 + 15 * t / 255,
                                             240 - 40 * t / 255,
                                             240 - 40 * t / 255));
            FillRect(di->hDC, &rc, br);
            DeleteObject(br);
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
            HFONT f = uiFont_ ? uiFont_ : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT of = (HFONT)SelectObject(di->hDC, f);
            RECT tr = rc;
            tr.left += ScalePx(6, dpi_);
            DrawTextW(di->hDC, rxText_.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(di->hDC, of);
            return TRUE;
        }
        if (di && di->CtlID == IDC_CHK_BREAK) {
            // §76：BREAK 复选框（ownerdraw）——方框=发送 BREAK 选择态；检测到线路
            // BREAK 时整块文字区黄亮（1.5s，timer 13 复位）
            RECT& rc = di->rcItem;
            HBRUSH bgbr = CreateSolidBrush(breakLit_ ? RGB(0xFF, 0xDC, 0x00)
                                                     : GetSysColor(COLOR_BTNFACE));
            FillRect(di->hDC, &rc, bgbr);
            DeleteObject(bgbr);
            int box = ScalePx(13, dpi_);
            RECT br{ rc.left + ScalePx(1, dpi_),
                     rc.top + (rc.bottom - rc.top - box) / 2,
                     rc.left + ScalePx(1, dpi_) + box,
                     rc.top + (rc.bottom - rc.top + box) / 2 };
            bool checked = breakChecked_;              // §76：自持态（ownerdraw 不维护 BM_*CHECK）
            DrawFrameControl(di->hDC, &br, DFC_BUTTON,
                             DFCS_BUTTONCHECK | DFCS_FLAT | (checked ? DFCS_CHECKED : 0));
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
            HFONT f = uiFont_ ? uiFont_ : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT of = (HFONT)SelectObject(di->hDC, f);
            RECT tr = rc;
            tr.left = br.right + ScalePx(4, dpi_);
            DrawTextW(di->hDC, L"BREAK", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(di->hDC, of);
            return TRUE;
        }
        if (di && di->CtlID == IDC_CHIP_TS) {
            // §68：左半=接收时间戳色，右半=发送时间戳色（点击弹样式窗）
            RECT& rc = di->rcItem;
            COLORREF colR = ParseColor(cfg_.rx_ts_color, RGB(0x9E, 0x9E, 0x9E));
            COLORREF colT = ParseColor(cfg_.tx_ts_color, RGB(0x9E, 0x9E, 0x9E));
            RECT halfL = rc, halfR = rc;
            halfL.right = halfR.left = (rc.left + rc.right) / 2;
            HBRUSH bl = CreateSolidBrush(colR);
            FillRect(di->hDC, &halfL, bl);
            DeleteObject(bl);
            HBRUSH br2 = CreateSolidBrush(colT);
            FillRect(di->hDC, &halfR, br2);
            DeleteObject(br2);
            DrawEdge(di->hDC, &rc, EDGE_ETCHED, BF_RECT);
            return TRUE;
        }
        if (di && di->CtlID == IDC_LED_NET) {
            // 网络 LED：灰=未连接 黄=重连中 红=链路错误 绿=已连接（← PyCom 四态）
            COLORREF col = linkErr_ ? RGB(0xFF, 0x41, 0x36)
                          : reconnecting_ ? RGB(0xFF, 0xDC, 0x00)
                          : (connected_ && !loopMode_ ? RGB(0x21, 0xA1, 0x21) : RGB(0x9E, 0x9E, 0x9E));
            HBRUSH br = CreateSolidBrush(col);
            HPEN   pen = CreatePen(PS_SOLID, 1, col);
            HBRUSH ob = (HBRUSH)SelectObject(di->hDC, br);
            HPEN   op = (HPEN)SelectObject(di->hDC, pen);
            Ellipse(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
            SelectObject(di->hDC, op);
            SelectObject(di->hDC, ob);
            DeleteObject(pen);
            DeleteObject(br);
            return TRUE;
        }
        if (di && di->CtlID == IDC_LED_LINK) {
            // 4 态：灰=未连接 黄=重连中（端口被占用等待/断线重连，§66） 绿=已连接(真口) 金=软件回环
            COLORREF col = connected_ ? (loopMode_ ? RGB(0xE0, 0xA0, 0x00) : RGB(0x21, 0xA1, 0x21))
                          : reconnecting_ ? RGB(0xFF, 0xDC, 0x00)
                          : RGB(0x9E, 0x9E, 0x9E);
            HBRUSH br = CreateSolidBrush(col);
            HPEN   pen = CreatePen(PS_SOLID, 1, col);
            HBRUSH ob = (HBRUSH)SelectObject(di->hDC, br);
            HPEN   op = (HPEN)SelectObject(di->hDC, pen);
            Ellipse(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
            SelectObject(di->hDC, op);
            SelectObject(di->hDC, ob);
            DeleteObject(pen);
            DeleteObject(br);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        OnCommandImpl(id, code);
        return 0;
    }

    case WM_CLOSE:
        SaveConfig();
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        if (extWnd_) DestroyWindow(extWnd_);
        KillTimer(hwnd_, 2);
        KillTimer(hwnd_, 3);
        KillTimer(hwnd_, 4);
        KillTimer(hwnd_, 5);                 // 转存日志定时器（审查 P3-1）
        KillTimer(hwnd_, 6);
        KillTimer(hwnd_, 7);
        KillTimer(hwnd_, 12);
        KillTimer(hwnd_, 13);                // §73 BRK 复位
        if (grayBmp_) {                      // §120：菜单栏守卫缓存 DIB 释放
            if (grayMem_ && grayOld_) SelectObject(grayMem_, grayOld_);
            DeleteObject(grayBmp_);
            if (grayMem_) DeleteDC(grayMem_);
            grayBmp_ = nullptr; grayMem_ = nullptr; grayBits_ = nullptr;
        }
        if (logFile_ != INVALID_HANDLE_VALUE) {        // 转存日志收尾：补写 + 关句柄
            RxLogPoll();
            RxLogFlushTail();
            DetachRxLog();
        }
        engine_.Close();                     // 先收 I/O 线程（join）再退出，杜绝线程向已销毁窗口投递
        net_.Close();                        // 网络引擎：closesocket 防迟到事件向已销毁窗口投递
        if (uiFont_)   DeleteObject(uiFont_);
        if (sysFont_)  DeleteObject(sysFont_);
        if (dataFont_) DeleteObject(dataFont_);
        if (tsRxFont_) DeleteObject(tsRxFont_);
        if (tsTxFont_) DeleteObject(tsTxFont_);
        if (pinBrush_)  DeleteObject(pinBrush_);
        if (breakBrush_) DeleteObject(breakBrush_);   // §73
        if (okBrush_)   DeleteObject(okBrush_);
        if (errBrush_)  DeleteObject(errBrush_);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ================= M0.5 spike 接线：串口引擎 ↔ 视图 ↔ 状态栏 =================

std::wstring MainWindow::TimestampNow() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[24];
    swprintf(buf, 24, L"[%02d:%02d:%02d.%03d]", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

void MainWindow::ApplyViewColors() {
    // 收=蓝 / 发=绿为语义色（固定）；§68：ts=接收时间戳色、txDir=发送时间戳色（标记随各自 ts 色）
    rxv_.SetColors(ParseColor(cfg_.bg_color, RGB(0xFF, 0xFF, 0xFF)),
                   ParseColor(cfg_.text_color, RGB(0x00, 0x00, 0x00)),
                   ParseColor(cfg_.rx_ts_color, RGB(0x80, 0x80, 0x80)),
                   ParseColor(cfg_.tx_ts_color, RGB(0x80, 0x80, 0x80)),
                   RGB(0x1B, 0x7A, 0x1B),
                   GetSysColor(COLOR_HIGHLIGHT));
}

void MainWindow::RefreshPorts() {
    // 下拉显示 "COMx - 设备描述"（描述空则仅 COMx）——配置里存的是纯端口名，
    // portNames_ 维持 项序↔端口名 映射（← PyCom addItem("%s - %s", data=name)）
    int curSel = (int)SendMessageW(cmb_port_, CB_GETCURSEL, 0, 0);
    std::wstring keepPort = (curSel >= 0 && curSel < (int)portNames_.size()) ? portNames_[curSel] : L"";
    SendMessageW(cmb_port_, CB_RESETCONTENT, 0, 0);
    portNames_.clear();
    auto ports = SerialEngine::EnumPorts();
    for (const auto& pd : ports) {
        std::wstring label = pd.second.empty() ? pd.first : (pd.first + L" - " + pd.second);
        SendMessageW(cmb_port_, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        portNames_.push_back(pd.first);
    }
    // 选中保持：优先按端口名匹配旧选择，否则按配置端口
    int sel = -1;
    for (size_t i = 0; i < portNames_.size(); ++i)
        if (portNames_[i] == keepPort) { sel = (int)i; break; }
    if (sel < 0)
        for (size_t i = 0; i < portNames_.size(); ++i)
            if (portNames_[i] == cfg_.port) { sel = (int)i; break; }
    // §104（审查第十三轮 P2-A）：**有意配置 / 已在用**的端口缺失时**不回落到第一项**——
    // 旧策略把选择切到首个端口，叠加 open_on_start / 自动重连即静默改连别的设备
    // （热拔后重连改连 COM1 的机制路径同此）。改为把缺失端口名做成「（未找到）」占位项并
    // 选中：打开会明确失败并提示（不会去开别的口），设备回插（热插拔刷新）即自动恢复。
    // 注意区别：cfg_.port 为空 = 从未选过（首启）→ 保持旧便利回落第一项。
    std::wstring missName;
    if (sel < 0) missName = !keepPort.empty() ? keepPort : cfg_.port;
    if (sel < 0 && !missName.empty()) {
        std::wstring label = missName + L"（未找到）";
        SendMessageW(cmb_port_, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        portNames_.push_back(missName);
        sel = (int)portNames_.size() - 1;
    }
    if (sel < 0 && !portNames_.empty()) sel = 0;         // 首启未选过 → 回落第一项（便利）
    if (sel >= 0) SendMessageW(cmb_port_, CB_SETCURSEL, (WPARAM)sel, 0);
    if (!missName.empty())
        Notify((L"端口 " + missName + L" 未找到（已保留该选择，不会自动改连其他端口）").c_str());
    else if (!ports.empty())
        Notify((std::wstring(L"串口列表已刷新：") + std::to_wstring(ports.size()) + L" 个端口").c_str());
    else
        Notify(L"未发现串口（SetupDi 枚举为空）");
}

void MainWindow::RefreshNics() {
    // ← PyCom _refresh_nics：第 0 项"全部网卡"；各项 "名字 (首地址)"；
    // 启动按 cfg_.local_ip 自动选中包含它的网卡，刷新尽量保持原选择
    nics_ = EnumNics();
    wchar_t cur[128]{};
    int curSel = (int)SendMessageW(cmb_nic_, CB_GETCURSEL, 0, 0);
    std::wstring keep = (curSel > 0 && curSel < (int)nics_.size() + 1) ? nics_[curSel - 1].name : L"";
    GetWindowTextW(cmb_nic_, cur, 128);
    SendMessageW(cmb_nic_, CB_RESETCONTENT, 0, 0);
    SendMessageW(cmb_nic_, CB_ADDSTRING, 0, (LPARAM)L"全部网卡");
    int sel = 0, autoSel = -1;
    for (size_t i = 0; i < nics_.size(); ++i) {
        SendMessageW(cmb_nic_, CB_ADDSTRING, 0, (LPARAM)nics_[i].label.c_str());
        if (nics_[i].name == keep) sel = (int)(i + 1);
        for (const auto& a : nics_[i].addrs)
            if (a == cfg_.local_ip && autoSel < 0) { autoSel = (int)(i + 1); break; }
    }
    SendMessageW(cmb_nic_, CB_SETCURSEL, (WPARAM)(autoSel >= 0 ? autoSel : sel), 0);
    RefreshLocalIps();
}

void MainWindow::RefreshLocalIps() {
    // ← PyCom _refresh_local_ips：指定网卡 = 只列该网卡地址（v4 前 v6 后）；
    // 全部网卡 = 0.0.0.0 + 各IPv4（补 127.0.0.1）+ :: + 各IPv6（全局在前）
    wchar_t cur[128]{};
    GetWindowTextW(cmb_lip_, cur, 128);
    int curSel = (int)SendMessageW(cmb_lip_, CB_GETCURSEL, 0, 0);
    std::wstring keep = cur[0] ? cur : cfg_.local_ip;
    int nicSel = (int)SendMessageW(cmb_nic_, CB_GETCURSEL, 0, 0);
    std::vector<std::wstring> addrs;
    if (nicSel > 0 && nicSel <= (int)nics_.size()) {
        addrs = nics_[nicSel - 1].addrs;
    } else {
        addrs = AllLocalAddresses(nics_);
    }
    SendMessageW(cmb_lip_, CB_RESETCONTENT, 0, 0);
    for (const auto& a : addrs) SendMessageW(cmb_lip_, CB_ADDSTRING, 0, (LPARAM)a.c_str());
    int sel = 0;
    for (size_t i = 0; i < addrs.size(); ++i)
        if (addrs[i] == keep) { sel = (int)i; break; }
    SendMessageW(cmb_lip_, CB_SETCURSEL, (WPARAM)sel, 0);
}

void MainWindow::ApplyModeColumns() {
    // ← PyCom serial_box/net_box 互斥显示；网络区标签随模式换文本
    bool serial = (cfg_.mode == L"serial");
    HWND serialCtrls[] = { lbl_port_, cmb_port_, btn_refresh_, led_, btn_open_, lbl_baud_, cmb_baud_,
                           lbl_data_, cmb_databits_, lbl_stop_, cmb_stopbits_, lbl_parity_, cmb_parity_,
                           lbl_flow_, cmb_flow_, chk_dtr_, chk_rts_, lbl_line_,
                           pin_[0], pin_[1], pin_[2], pin_[3], pin_[4], chk_break_ };
    HWND netCtrls[] = { led2_, lbl_netmode_, lbl_remote_, edit_rip_, edit_rport_,
                        btn_nconn_, btn_ndisc_, lbl_nic_, cmb_nic_, btn_nicref_,
                        lbl_local_, cmb_lip_, edit_lport_ };
    for (HWND h : serialCtrls) if (h) ShowWindow(h, serial ? SW_SHOW : SW_HIDE);
    for (HWND h : netCtrls) if (h) ShowWindow(h, serial ? SW_HIDE : SW_SHOW);
    if (!serial) {
        for (int i = 0; i < 4; ++i)
            if (cfg_.mode == kModeVals[i]) {
                SetWindowTextW(lbl_netmode_, kModes[i]);
                // 连接按钮文案（← PyCom：TCP 服务端显示「侦听」，其余「连接」）
                SetWindowTextW(btn_nconn_, cfg_.mode == L"tcp_server" ? L"侦听" : L"连接");
            }
        ApplyNetLocks();               // 模式锁死联动（PyCom _sync_net_locks）
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

std::wstring MainWindow::CurrentPortName() const {
    // 端口下拉显示 "COMx - 描述"，真正送 CreateFileW 的必须是纯端口名
    // （← PyCom addItem(label, data=name)；审查 P0-1）
    int sel = (int)SendMessageW(cmb_port_, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < (int)portNames_.size()) return portNames_[sel];
    return L"";
}

SerialParams MainWindow::GatherParams() {
    SerialParams p;
    p.port = CurrentPortName();
    wchar_t buf[64]{};
    GetWindowTextW(cmb_baud_, buf, 64);
    p.baud = _wtoi(buf);
    if (p.baud < 300) p.baud = 115200;             // §77：上限 12M→50Mbps（SSCOM V5.08 口径）
    if (p.baud > 50000000) p.baud = 50000000;
    int dsel = (int)SendMessageW(cmb_databits_, CB_GETCURSEL, 0, 0);    // 项序 8/7/6/5
    if (dsel >= 0 && dsel <= 3) p.databits = 8 - dsel;
    p.stopbits = (int)SendMessageW(cmb_stopbits_, CB_GETCURSEL, 0, 0);  // 项序 1/1.5/2 → 0/1/2
    if (p.stopbits < 0) p.stopbits = 0;
    p.parity = (int)SendMessageW(cmb_parity_, CB_GETCURSEL, 0, 0);      // 无/偶/奇/Mark/Space → 0..4
    if (p.parity < 0) p.parity = 0;
    p.dtr = SendMessageW(chk_dtr_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.rts = SendMessageW(chk_rts_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return p;
}

void MainWindow::SyncSerialCfg() {
    // 控件 → cfg 回写：仅在打开成功/保存参数时调用（审查 P3-3——GatherParams 被
    // 重连 tick/自检/回环演示共用，不能有写入副作用，否则重连期间控件值污染配置）
    SerialParams p = GatherParams();
    cfg_.baud = p.baud; cfg_.databits = p.databits;
    cfg_.stopbits = p.stopbits == 0 ? 1.0 : p.stopbits == 1 ? 1.5 : 2.0;
    static const wchar_t* kParityNames[] = { L"none", L"even", L"odd", L"mark", L"space" };
    cfg_.parity = kParityNames[p.parity];
    cfg_.dtr = p.dtr; cfg_.rts = p.rts;
}

void MainWindow::TryOpenSerial(bool allowLoopback) {
    if (engine_.IsOpen()) return;
    SerialParams p = GatherParams();
    if (p.port.empty()) { Notify(L"未选择串口"); wantOpen_ = false; CancelReconnect(); return; }
    if (!engine_.Open(hwnd_, p)) {
        DWORD e = GetLastError();
        const wchar_t* why = L"";
        if (e == ERROR_ACCESS_DENIED) why = L"，端口被占用";          // ← PyCom 占用提示口径
        else if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) why = L"，端口不存在";
        wchar_t msg[160];
        swprintf(msg, 160, L"打开 %ls 失败（错误 %lu%ls）", p.port.c_str(), (unsigned long)e, why);
        if (allowLoopback) {                       // 回退软件回环（自检/演示用，不打扰真口语义）
            SerialParams lp = p;
            lp.loopback = true;
            if (engine_.Open(hwnd_, lp)) {
                loopMode_ = true;
                Notify((std::wstring(msg) + L"；已回退软件回环").c_str());
                return;
            }
        }
        Notify(msg);
        // 打开失败也保持打开意图 → 自动重连（← PyCom _try_reopen 失败路径）。
        // §104（审查第十三轮 P3-1）：该重连受 auto_reconnect 门控——用户关掉自动重连时，
        // 失败只提示并复位打开意图（不循环）；开启时（缺省）保持 PyCom 语义不变。
        if (wantOpen_ && !selftest_) {
            if (cfg_.auto_reconnect) EnterReconnect();
            else wantOpen_ = false;
        }
        return;
    }
    loopMode_ = false;
    linkErr_ = false;
    CancelReconnect();
    connected_ = true;
    cfg_.port = p.port;
    SyncSerialCfg();                               // 打开成功：控件值认入配置（P3-3）
    UpdateConnStatus();
    FlushTxPend();                                 // 连接恢复：排空待发（B2）
    wchar_t ok[96];
    swprintf(ok, 96, L"已打开 %ls @%d（TX 可发；RX 需对端/短接）", p.port.c_str(), p.baud);
    Notify(ok);
}

void MainWindow::CloseSerial() {
    wantOpen_ = false;                             // 先撤意图：断线通知不再触发重连
    CancelReconnect();
    if (engine_.IsOpen()) engine_.Close();
    OnSerialState(false);
    ResetCounters();                               // §103（用户拍板）：关闭/换口/切模式 → 计数清零
}

void MainWindow::EnterReconnect() {
    if (reconnecting_) return;
    reconnecting_ = true;
    SetTimer(hwnd_, 7, (UINT)cfg_.reconnect_ms, nullptr);   // ← PyCom _recon(reconnect_ms)
    Notify(L"重连中…");                             // 每 tick 刷新第 4 段，持续可见
    UpdateConnStatus();
}

void MainWindow::CancelReconnect() {
    if (!reconnecting_) return;
    reconnecting_ = false;
    KillTimer(hwnd_, 7);
    UpdateConnStatus();
}

void MainWindow::ReopenSerial() {
    // ← PyCom _apply_live_baud：CH34x 等驱动动态改参不可靠，必须重新打开端口
    if (!engine_.IsOpen() && !reconnecting_) return;
    CloseSerial();
    wantOpen_ = true;                              // 重开保持打开意图
    TryOpenSerial(false);
}

void MainWindow::ApplyDtrRts() {
    cfg_.dtr = SendMessageW(chk_dtr_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg_.rts = SendMessageW(chk_rts_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (engine_.IsOpen() && !loopMode_) engine_.SetDtrRts(cfg_.dtr, cfg_.rts);   // 热改（PyCom 同款）
}

HWND MainWindow::WheelTarget(const MSG& msg) {
    // C2a：滚轮按鼠标悬停路由（← PyCom）——鼠标下的窗口是接收区则转发
    if (msg.message != WM_MOUSEWHEEL) return nullptr;
    POINT pt{ GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };   // 屏幕坐标
    HWND h = WindowFromPoint(pt);
    if (!h) return nullptr;
    wchar_t cls[32]{};
    GetClassNameW(h, cls, 32);
    return (wcscmp(cls, L"SNComRxView") == 0) ? h : nullptr;
}

void MainWindow::PollPins() {
    // ← PyCom _poll_pins：300ms 轮询 DCD/DSR/CTS/RI；ERR 挂链路错误态
    // modem 状态位（winbase：MS_DCD_ON=0x80 MS_DSR_ON=0x20 MS_CTS_ON=0x10 MS_RING_ON=0x40）
#ifndef MS_DCD_ON
#define MS_DCD_ON  ((DWORD)0x0080)
#define MS_DSR_ON  ((DWORD)0x0020)
#define MS_CTS_ON  ((DWORD)0x0010)
#define MS_RING_ON ((DWORD)0x0040)
#endif
    DWORD ms = 0;
    bool ok = engine_.IsOpen() && !loopMode_ && engine_.ModemStatus(ms);
    bool next[5] = {
        ok && (ms & MS_DCD_ON) != 0,
        ok && (ms & MS_DSR_ON) != 0,
        ok && (ms & MS_CTS_ON) != 0,
        ok && (ms & MS_RING_ON) != 0,
        linkErr_,
    };
    for (int i = 0; i < 5; ++i) {
        if (pinActive_[i] != next[i]) {
            pinActive_[i] = next[i];
            InvalidateRect(pin_[i], nullptr, TRUE);
        }
    }
}

void MainWindow::OnSerialState(bool open) {
    if (open) {
        connected_ = true;
        linkErr_ = false;
        FlushTxPend();                             // 连接恢复：待发自动续发（B2）
        UpdateConnStatus();
        return;
    }
    bool was = connected_;
    connected_ = false;
    loopMode_ = false;
    if (engine_.IsOpen()) engine_.Close();         // 断线自退出 → join + 清句柄
    txPend_.clear();                               // ← PyCom：余量只属于当前连接
    StopTimedSend();                               // §70：关闭串口/断线 → 定时发送停止、按钮复位
    if (protoActive_) ProtoFinish(L"连接已断开", false);   // §129：断线=会话复位（docs/17 §2）
    if (breakChecked_) {                           // §76：BREAK 断言随连接断开复位（视觉不留残留）
        breakChecked_ = false;
        if (chk_break_) InvalidateRect(chk_break_, nullptr, TRUE);
    }
    UpdateConnStatus();
    if (was && wantOpen_ && cfg_.auto_reconnect && !selftest_ &&
        !CurrentPortName().empty())
        EnterReconnect();                          // 无端口不重连（防死循环，PyCom 同款）
}

void MainWindow::UpdateConnStatus() {
    // §66（用户拍板）：状态栏「已连接/未连接」段已删除——连接状态=LED 四态灯
    // （灰/黄=重连中/绿/金）+ 打开按钮三态文案；事件与重连提示走 Notify（第 3 段）。
    SetWindowTextW(btn_open_, connected_ ? L"关闭串口"
                                         : (reconnecting_ ? L"取消" : L"打开串口"));
    InvalidateRect(led_, nullptr, TRUE);           // 自绘圆点变色
    InvalidateRect(led2_, nullptr, TRUE);
}

// §70：定时发送随连接关闭/断开自动停止，「发 送」按钮从「停止发送」复位
void MainWindow::StopTimedSend() {
    if (!timedRunning_) return;
    KillTimer(hwnd_, 10);
    timedRunning_ = false;
    SetWindowTextW(btn_send_, L"发  送");
    Notify(L"定时发送已停止（连接已断开）");
}

void MainWindow::OnSerialEvent(int code, const wchar_t* text) {
    if (code == kEvtError)                         // 仅错误事件亮 ERR 灯（审查 P3-2）
        linkErr_ = true;
    if (code == kEvtBreak) {                       // §76：线路 BREAK → BREAK 复选框文字区黄亮 1.5s
        breakLit_ = true;
        if (chk_break_) InvalidateRect(chk_break_, nullptr, TRUE);
        SetTimer(hwnd_, 13, 1500, nullptr);
    }
    Notify(text);
    InvalidateRect(led_, nullptr, TRUE);
    InvalidateRect(led2_, nullptr, TRUE);
}

void MainWindow::OnSerialRx(const uint8_t* d, size_t n) {
    rxBytes_ += (unsigned)n;
    if (protoActive_) {                            // §129：会话期只喂协议机（不进转存日志）
        proto_.Feed(d, n);
        ProtoPump();                               // §131：踢到达即写出首包+显示（不等 30ms 拍）
        if (protoShow_) {                          // 续修5：勾选=对端控制流也入显示窗口
            buf_.append(d, n);
            newData_ = true;
            RequestFastDisplay();                  // §138：空闲降档后数据到达即回快拍
            builder_.feed(false, d, n, NowSeconds(), buf_.end_abs() - n);
        }
        return;
    }
    buf_.append(d, n);                              // 环形缓冲（保存 bin / 行表裁剪的水位源）
    newData_ = true;
    RequestFastDisplay();                          // §138：空闲降档后数据到达即回快拍
    builder_.feed(false, d, n, NowSeconds(), buf_.end_abs() - n);
}

// ================= M4 网络：链路抽象与网络区接线（← PyCom net_link.py / 网络面板） =================

static std::wstring TrimWs(std::wstring s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool MainWindow::LinkIsOpen() const {
    return (cfg_.mode == L"serial") ? engine_.IsOpen() : net_.IsOpen();
}

int MainWindow::LinkWrite(const uint8_t* d, size_t n) {
    if (cfg_.mode == L"serial") {
        return engine_.Write(d, n);                 // §99：串口按接受量返回（队列满拒收 → UI 待发接管）
    }
    return net_.Write(d, n);                        // 网络按实际接受量返回（余量引擎内部待发）
}

NetParams MainWindow::GatherNetParams() const {
    // 纯读（与 GatherParams 同口径：无副作用，重连 tick/自检共用）
    NetParams p;
    if (cfg_.mode == L"tcp_server") p.mode = kNetTcpServer;
    else if (cfg_.mode == L"udp")   p.mode = kNetUdp;
    wchar_t buf[128]{};
    GetWindowTextW(edit_rip_, buf, 128);
    p.remote_ip = TrimWs(buf);
    GetWindowTextW(edit_rport_, buf, 32);
    p.remote_port = _wtoi(buf);
    if (p.remote_port < 1 || p.remote_port > 65535) p.remote_port = 8080;   // ← PyCom QSpinBox(1,65535)
    GetWindowTextW(cmb_lip_, buf, 128);
    p.local_ip = TrimWs(buf);
    GetWindowTextW(edit_lport_, buf, 32);
    p.local_port = _wtoi(buf);
    if (p.local_port < 1 || p.local_port > 65535) p.local_port = 777;
    return p;
}

void MainWindow::SyncNetCfg() {
    NetParams p = GatherNetParams();
    cfg_.remote_ip = p.remote_ip;
    cfg_.remote_port = p.remote_port;
    cfg_.local_ip = p.local_ip;
    cfg_.local_port = p.local_port;
}

void MainWindow::TryOpenNet() {
    if (net_.IsOpen()) return;
    NetParams p = GatherNetParams();
    if (p.mode == kNetTcpClient && p.remote_ip.empty()) {
        Notify(L"未填写远程 IP / 主机名");
        wantOpen_ = false; CancelReconnect();
        return;
    }
    if (!net_.Open(hwnd_, p)) {
        // 同步失败（引擎未建立会话，无消息回传）：按模式给分类提示（← PyCom errorString 口径）
        const wchar_t* what = (p.mode == kNetTcpServer) ? L"监听失败（端口被占用或无权限）"
                            : (p.mode == kNetUdp)       ? L"绑定失败（端口被占用或无权限）"
                                                        : L"TCP 连接发起失败（解析不到目标地址）";
        Notify(what);
        // 只有 TCP 客户端失败才进自动重连（PyCom 口径：服务端侦听/UDP 绑定失败
        // 不重试——按钮保持可点，改端口后重试即可；重试只会反复弹占用提示）
        if (wantOpen_ && p.mode == kNetTcpClient) EnterReconnect();
        return;
    }
    linkErr_ = false;
    CancelReconnect();
    SyncNetCfg();                                   // 打开成功：控件值认入配置（P3-3 网络口径）
    if (p.mode == kNetTcpClient) {
        wchar_t b[192];
        swprintf(b, 192, L"连接中：%ls:%d（等待对端响应）", p.remote_ip.c_str(), p.remote_port);
        Notify(b);                                  // FD_CONNECT 后经 St(1) 转「已连接」+ 常驻描述
    }
    // 服务端/UDP 同步打开成功：St(1) 随后到达 → OnNetState(true) 统一显示常驻描述
}

void MainWindow::CloseNet() {
    wantOpen_ = false;                              // 先撤意图：断线通知不再触发重连
    CancelReconnect();
    net_.Close();
    OnNetState(false);                              // 幂等（connected_ 已 false 时只刷新显示）
    ResetCounters();                                // §103（用户拍板）：关闭/切 TCP-UDP → 计数清零
}

void MainWindow::CloseLink() {
    // 按当前模式关链路：模式切换必须先关旧连接（← PyCom _set_mode）
    if (cfg_.mode == L"serial") CloseSerial();
    else CloseNet();
    KillTimer(hwnd_, 9);                            // 断链即取消文件发送（PyCom _close_link → file_sender.stop）
    if (txFile_ != INVALID_HANDLE_VALUE) { CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE; }
    txFilePend_.clear();
}

void MainWindow::OnNetState(bool open) {
    if (open) {
        connected_ = true;
        linkErr_ = false;
        UpdateConnStatus();
        FlushTxPend();                              // 连接恢复：待发自动续发（B2）
        Notify(DescribeLink().c_str());             // 常驻描述（PyCom _set_link_base 口径）
        return;
    }
    bool was = connected_;
    connected_ = false;
    net_.Close();                                   // 事件驱动断开 → 清套接字（幂等，换代号滤陈旧消息）
    txPend_.clear();                                // ← PyCom：余量只属于当前连接
    StopTimedSend();                                // §70：网络关闭/断开 → 定时发送停止、按钮复位
    if (protoActive_) ProtoFinish(L"连接已断开", false);   // §129：断线=会话复位（docs/17 §2）
    UpdateConnStatus();
    if (was && wantOpen_ && cfg_.auto_reconnect && !selftest_)
        EnterReconnect();                           // 网络参数恒在，无需串口的空端口检查
}

void MainWindow::OnNetEvent(int code, const wchar_t* text) {
    if (code == kNetEvtError) linkErr_ = true;      // 仅错误事件亮 ERR 灯（与串口 P3-2 同款）
    if (code == kNetEvtUdpPeer) {
        // §77（SSCOM V5.13）：UDP 收到数据包 → 「远程地址」改为数据包来源（ip|port），
        // 之后发送直接回到最近对端
        std::wstring t = text ? text : L"";
        size_t bar = t.rfind(L'|');
        if (bar != std::wstring::npos) {
            std::wstring ip = t.substr(0, bar);
            int port = _wtoi(t.c_str() + bar + 1);
            SetWindowTextW(edit_rip_, ip.c_str());
            if (port > 0 && port < 65536) {
                wchar_t pb[16];
                swprintf(pb, 16, L"%d", port);
                SetWindowTextW(edit_rport_, pb);
            }
            cfg_.remote_ip = ip;
            if (port > 0 && port < 65536) cfg_.remote_port = port;
            Notify((L"已按数据包来源更新远端：" + ip + L":" + std::to_wstring(port)).c_str());
            InvalidateRect(led_, nullptr, TRUE);
            InvalidateRect(led2_, nullptr, TRUE);
            return;                                 // 提示已给具体内容，不再走通用 Notify
        }
    }
    Notify(text);
    InvalidateRect(led_, nullptr, TRUE);
    InvalidateRect(led2_, nullptr, TRUE);
}

void MainWindow::OnNetRx(const uint8_t* d, size_t n) {
    rxBytes_ += (unsigned)n;
    if (protoActive_) {                            // §129：会话期只喂协议机（不进转存日志）
        proto_.Feed(d, n);
        ProtoPump();                               // §131：踢到达即写出首包+显示（不等 30ms 拍）
        if (protoShow_) {                          // 续修5：勾选=对端控制流也入显示窗口
            buf_.append(d, n);
            newData_ = true;
            RequestFastDisplay();                  // §138：空闲降档后数据到达即回快拍
            builder_.feed(false, d, n, NowSeconds(), buf_.end_abs() - n);
        }
        return;
    }
    buf_.append(d, n);
    newData_ = true;
    RequestFastDisplay();                          // §138：空闲降档后数据到达即回快拍
    builder_.feed(false, d, n, NowSeconds(), buf_.end_abs() - n);
}

std::wstring MainWindow::DescribeLink() const {
    NetParams p = GatherNetParams();
    wchar_t b[256];
    if (p.mode == kNetTcpClient) {
        // 本机IP口径（PyCom _bindable_local）：仅具体网卡地址显示，全部档/回环/未配置=自动（随目标）
        const std::wstring& ip = p.local_ip;
        bool specific = !ip.empty() && ip != L"0.0.0.0" && ip != L"::" && ip != L"::1"
                        && ip.rfind(L"127.", 0) != 0;
        swprintf(b, 256, L"TCP客户端 本机IP=%ls → %ls:%d",
                 specific ? ip.c_str() : L"自动（随目标）", p.remote_ip.c_str(), p.remote_port);
    } else if (p.mode == kNetTcpServer) {
        swprintf(b, 256, L"TCP服务器 本机IP=%ls :%d（%d 客户端）",
                 p.local_ip.c_str(), p.local_port, net_.ClientCount());
    } else {
        swprintf(b, 256, L"UDP 本机IP=%ls :%d → %ls:%d",
                 p.local_ip.c_str(), p.local_port, p.remote_ip.c_str(), p.remote_port);
    }
    return b;
}

void MainWindow::ApplyNetLocks() {
    // 模式锁死联动（PyCom _sync_net_locks）：TCP 服务端不用远程 IP/端口；TCP 客户端不用本机端口
    bool server = (cfg_.mode == L"tcp_server");
    bool client = (cfg_.mode == L"tcp_client");
    if (edit_rip_)   EnableWindow(edit_rip_, !server);
    if (edit_rport_) EnableWindow(edit_rport_, !server);
    if (edit_lport_) EnableWindow(edit_lport_, !client);
}

void MainWindow::FollowThunk(void* ctx, bool on) {
    static_cast<MainWindow*>(ctx)->OnFollowChanged(on);
}

void MainWindow::AnnoThunk(void* ctx, bool copyAnno, bool saveAnno) {
    static_cast<MainWindow*>(ctx)->OnAnnoToggled(copyAnno, saveAnno);
}

void MainWindow::ClearThunk(void* ctx) {
    static_cast<MainWindow*>(ctx)->OnClearFromView();
}

void MainWindow::OnFollowChanged(bool on) {
    // 用户回看（滚轮上翻/拖选/拖条）取消 → 勾选框与配置同步；再勾选 → 视图跳回底部（PyCom 语义）
    cfg_.auto_scroll = on;
    SendMessageW(chk_autoscroll_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    SyncMenuChecks();
}

void MainWindow::OnAnnoToggled(bool copyAnno, bool saveAnno) {
    cfg_.copy_with_anno = copyAnno;                 // 右键两开关持久化（与显示 split 解耦，PyCom 同款）
    cfg_.save_with_anno = saveAnno;
}

void MainWindow::OnClearFromView() {
    ClearDisplayAll();
    Notify(L"已清除接收窗口");
}

void MainWindow::LoopbackDemo() {
    // 回环演示：无条件软件回环（不经真口）+ 自动发一串测试帧，点了就能看到 收← 绿行
    if (cfg_.mode != L"serial") { Notify(L"回环演示仅串口模式可用"); return; }
    if (engine_.IsOpen()) { CloseSerial(); Notify(L"回环演示：已关闭当前连接"); return; }
    SerialParams p = GatherParams();
    p.loopback = true;
    if (!engine_.Open(hwnd_, p)) { Notify(L"软件回环打开失败"); return; }
    loopMode_ = true;
    Notify(L"回环演示：软件回环已启动（接收区输入或点「发 送」继续）");
    for (int i = 0; i < 8; ++i) {
        std::vector<uint8_t> b;
        wchar_t buf[96];
        if (i == 1) {
            // §62：超长行样本（ASCII+CJK 混合 ~400 字符）——展示折行与行尾可见性
            std::wstring s = L"回环演示超长行 #2：";
            for (int r = 0; r < 24; ++r)
                s += L"0123456789ABCDEF";
            s += L"行尾哨兵=END_MARK12345（折行后应可滚动到本段可见）";
            if (!WideToBytes(s, cfg_.encoding, b)) continue;
        } else if (i % 2 == 0) {
            // 多行 + 真实空行：首行/空行(仅CRLF)/尾行 —— 展示 LineBuilder 空行保留语义
            swprintf(buf, 96, L"回环演示帧 #%d：分包/多行管线\r\n\r\n上一行与这行之间是真实空行\r\n", i + 1);
            std::wstring s = buf;
            if (!WideToBytes(s, cfg_.encoding, b)) continue;
        } else {
            b.resize(16);
            for (size_t k = 0; k < b.size(); ++k) b[k] = (uint8_t)(i * 16 + k);
        }
        if (!b.empty()) SendBytes(b);
    }
}

void MainWindow::SendBytes(const std::vector<uint8_t>& bytes) {
    SendBytesEx(bytes, true);
}

void MainWindow::SendBytesEx(const std::vector<uint8_t>& bytes, bool echo) {
    // ← PyCom _write：分段待发队列（16MB 上限）+ 未连接入队续发；TX 只计链路接受量
    // §110：echo=false（终端按键模式未开回显）时同链路同计数，只是不入显示管线
    if (bytes.empty()) return;
    if (protoActive_) {
        // R23-P2-2：停等会话期禁止手工字节入链（发送按钮/Ctrl+Enter/多条单击/终端按键
        // 共用此统一管线，单点闸全兜住）——注入字节会被对端当帧头/应答，毁停等交换
        Notify(L"「协议发送」正在占用发送通道");
        return;
    }
    if (TxPendLen() + bytes.size() > kTxPendCap) {
        Notify(L"发送积压超限（>16MB），本次数据已丢弃");
        return;
    }
    if (!LinkIsOpen()) {
        Notify(L"未连接，无法发送");
        EnqueueTx(bytes, echo);                    // 整队待发，连接恢复后续发
        return;
    }
    FlushTxPend();                                 // 先排空旧余量（PyCom 顺序保证）
    if (!txPend_.empty()) {                        // 旧余量未排空：当前数据接队尾
        EnqueueTx(bytes, echo);
        return;
    }
    int n = LinkWrite(bytes.data(), bytes.size());
    if (n <= 0) {
        // 链路拒收（服务端无客户端/UDP 解析失败等）：UDP 错误去重期内引擎已报
        // 具体原因，不叠加通用提示（PyCom F3 门控）；其余给通用提示 + 数据入待发
        if (!(cfg_.mode != L"serial" && net_.SendErrActive()))
            Notify(L"写入失败或对端未就绪（0 字节）");
        EnqueueTx(bytes, echo);
        return;
    }
    if (n < (int)bytes.size()) {                   // 部分接受：余量接队尾待续发（PyCom _write）
        Notify((L"部分发送: " + std::to_wstring(n) + L"/" + std::to_wstring(bytes.size())
                + L" 字节（余量自动续发）").c_str());
        EnqueueTx(std::vector<uint8_t>(bytes.begin() + n, bytes.end()), echo);
    }
    txBytes_ += (unsigned)n;
    if (echo) {
        newData_ = true;
        RequestFastDisplay();                      // §138：空闲降档后回显到达即回快拍
        builder_.feed(true, bytes.data(), n, NowSeconds(), buf_.end_abs() - (bytes.size() - n));
    }
}

// ---------------- §110 终端仿真（M6 首增量）：接收区直接输入 ----------------
void MainWindow::TermSinkThunk(void* ctx, const wchar_t* text, int len, int kind) {
    ((MainWindow*)ctx)->TermSend(text, len, kind);
}

void MainWindow::ApplyTerminal() {
    // 视图只负责输入收集与尾巴绘制；编码/发送/回显在 TermSend（宿主）。
    // §140：总开关 cfg_.term_enable——关闭=视图终端态整体拆除（SetTerminal(false)：清尾巴/停光标/
    // 键盘与 IME 路径全部 termOn_ 短路），接收区不响应键盘；开启后"方式"决定何时成包发送。
    rxv_.SetTerminal(cfg_.term_enable, cfg_.term_key_send, cfg_.term_key_echo);   // §140：总开关
}

void MainWindow::TermSend(const wchar_t* text, int len, int kind) {
    // kind：0=文本（按键模式单键，按当前编码） 1=回车（回车模式=整行+CR[+LF]） 2=退格 0x08。
    // 未连接：只提示不发送、**不入待发队列**（终端是即时按键语义，排队补发会意外连发一串）。
    // §140：总开关关闭 = 终端完全无效——静默丢弃（视图态已拆，这里是最后防线）。
    if (!cfg_.term_enable) return;
    if (!LinkIsOpen()) {
        Notify(L"未连接——终端输入未发送");
        return;
    }
    const bool keyMode = cfg_.term_key_send;
    std::vector<uint8_t> b;
    if (kind == 2) {
        b.push_back(0x08);
    } else if (kind == 1) {
        if (!keyMode && text && len > 0) {                    // 回车模式：整行按当前编码
            if (!WideToBytes(std::wstring(text, (size_t)len), cfg_.encoding, b)) {
                // §126：本地行已消费（rxview 已清尾巴）——编码不可表示也回显原行（专用通道），
                // 只是不发送；吞行会让用户以为没打上
                builder_.feed_terminal_line(true, std::wstring(text, (size_t)len));
                newData_ = true;
                RequestFastDisplay();              // §138：终端回显即回快拍
                rxv_.SyncBase(builder_.base());
                rxv_.NotifyDataChanged();
                Notify(L"终端输入：当前编码无法表示该字符（仅回显未发送）");
                return;
            }
        }
        b.push_back(0x0D);
        if (cfg_.term_lf_send) b.push_back(0x0A);
    } else if (kind == 0 && text && len > 0) {
        if (!WideToBytes(std::wstring(text, (size_t)len), cfg_.encoding, b)) {
            Notify(L"终端输入：当前编码无法表示该字符");
            return;
        }
    } else {
        return;
    }
    const bool echo = keyMode ? (cfg_.term_key_echo && kind != 2) : true;  // 回车模式恒带回显
    // §126（用户拍板）：回车方式的整行回显走**终端专用通道**——不经 builder.feed 的包管线：
    // 不带时间戳前缀/方向标记、不受分包影响，且行尾合成 \n 立即收行 → 光标/输入尾巴
    // 落到新行（终端语义），不在回显行尾继续粘接。发送本体照旧走 SendBytesEx。
    if (kind == 1 && !keyMode) {
        // 回显内容 = 本地行（宽字符原样，编码前；**空行也落**——连按回车=发空行，§111 行为）；
        // 发送失败同样回显（本地编辑动作已完成）
        builder_.feed_terminal_line(true, (text && len > 0) ? std::wstring(text, (size_t)len) : std::wstring());
        newData_ = true;
        rxv_.SyncBase(builder_.base());
        rxv_.NotifyDataChanged();
    }
    SendBytesEx(b, keyMode ? echo : false);   // 回车方式：显示已由专用通道承担，不再 feed
}

void MainWindow::EnqueueTx(const std::vector<uint8_t>& b, bool record) {
    RequestFastDisplay();                          // §138：有待发 → 守卫 tick 回 60ms（兜底续发时延不变）
    // 同语义相邻段合并，减少链路写次数（← PyCom _enqueue_tx）
    if (!txPend_.empty() && txPend_.back().second == record) {
        auto& seg = txPend_.back().first;
        seg.insert(seg.end(), b.begin(), b.end());
        return;
    }
    txPend_.emplace_back(b, record);
}

size_t MainWindow::TxPendLen() const {
    size_t n = 0;
    for (const auto& p : txPend_) n += p.first.size();
    return n;
}

void MainWindow::FlushTxPend() {
    // 待发余量续发（PyCom _flush_tx_pend 口径）：每段按各自 record 语义回显已接受部分；
    // 串口引擎全量接受（一次排空）；网络部分接受时段剩余留队首，等下个 tick/FD_WRITE
    while (!txPend_.empty() && LinkIsOpen()) {
        auto& seg = txPend_.front();
        int n = LinkWrite(seg.first.data(), seg.first.size());
        if (n <= 0) break;                         // 链路暂不可写/拒收：下个 tick 继续
        txBytes_ += (unsigned)n;
        if (seg.second) {
            newData_ = true;
            RequestFastDisplay();                  // §138：待发续发回显即回快拍
            builder_.feed(true, seg.first.data(), n, NowSeconds(),
                          buf_.end_abs() - (seg.first.size() - n));
        }
        if (n < (int)seg.first.size()) {
            seg.first.erase(seg.first.begin(), seg.first.begin() + n);
            break;
        }
        txPend_.pop_front();
    }
}

void MainWindow::SendCurrentEdit() {
    int len = GetWindowTextLengthW(edit_send_);
    if (len <= 0) { Notify(L"发送内容为空"); return; }
    std::vector<uint8_t> b = CurrentSendPayload();
    if (b.empty()) { Notify(L"发送内容为空或 HEX 格式无效"); return; }
    std::wstring kind;
    int start = 1, tail = 0;
    ChecksumParamsNow(kind, start, tail);            // 发送时按当前档附加校验（SSCOM 语义）
    SendBytes(AppendChecksum(b, kind, start, tail));
}

// §54 发送框右键菜单：标准编辑项 + 选区转换（EDIT 控件右键私有、不产生 WM_CONTEXTMENU，
// 故由 SendEditProc 拦 WM_RBUTTONUP 调入；pt 为屏幕坐标）
void MainWindow::SendContextMenu(POINT pt) {
    DWORD selA = 0, selB = 0;
    SendMessageW(edit_send_, EM_GETSEL, (WPARAM)&selA, (LPARAM)&selB);
    bool hasSel = selB > selA;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, SendMessageW(edit_send_, EM_CANUNDO, 0, 0) ? MF_STRING : MF_GRAYED, 1, L"撤销(&U)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, hasSel ? MF_STRING : MF_GRAYED, 2, L"剪切(&T)");
    AppendMenuW(m, hasSel ? MF_STRING : MF_GRAYED, 3, L"复制(&C)");
    AppendMenuW(m, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : MF_GRAYED, 4, L"粘贴(&P)");
    AppendMenuW(m, hasSel ? MF_STRING : MF_GRAYED, 5, L"删除(&D)");
    AppendMenuW(m, MF_STRING, 6, L"全选(&A)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, hasSel ? MF_STRING : MF_GRAYED, 9, L"转换为 HEX（当前编码）");
    AppendMenuW(m, hasSel ? MF_STRING : MF_GRAYED, 10, L"转换为编码字符（当前编码）");
    int pick = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                              pt.x, pt.y, 0, edit_send_, nullptr);
    DestroyMenu(m);
    switch (pick) {
    case 1: SendMessageW(edit_send_, EM_UNDO, 0, 0); break;
    case 2: SendMessageW(edit_send_, WM_CUT, 0, 0); break;
    case 3: SendMessageW(edit_send_, WM_COPY, 0, 0); break;
    case 4: SendMessageW(edit_send_, WM_PASTE, 0, 0); break;
    case 5: SendMessageW(edit_send_, EM_REPLACESEL, TRUE, (LPARAM)L""); break;
    case 6: SendMessageW(edit_send_, EM_SETSEL, 0, -1); break;
    case 9: case 10:
        if (hasSel) SendConvertSelection(pick == 9);
        break;
    }
}

// §54 发送框右键：选区 ↔ HEX 转换（当前编码；hex 解析空格容忍见 ParseHex §54）
void MainWindow::SendConvertSelection(bool toHex) {
    DWORD selA = 0, selB = 0;
    SendMessageW(edit_send_, EM_GETSEL, (WPARAM)&selA, (LPARAM)&selB);
    if (selB <= selA) return;
    int len = GetWindowTextLengthW(edit_send_);
    std::vector<wchar_t> all((size_t)len + 1, 0);
    GetWindowTextW(edit_send_, all.data(), len + 1);
    std::wstring sel(all.data() + selA, selB - selA);
    std::wstring rep;
    if (toHex) {                                     // 字符 → HEX（当前编码）
        std::vector<uint8_t> b;
        if (!WideToBytes(sel, cfg_.encoding, b)) { Notify(L"编码失败"); return; }
        wchar_t hb[4];
        for (uint8_t x : b) { swprintf(hb, 4, L"%02X ", x); rep += hb; }
        if (!rep.empty()) rep.pop_back();            // 去尾空格
    } else {                                         // HEX → 字符（当前编码）
        std::vector<uint8_t> b;
        if (!ParseHex(sel, b) || b.empty()) { Notify(L"HEX 格式无效"); return; }
        rep = BytesToWide(b, cfg_.encoding);
    }
    SendMessageW(edit_send_, EM_SETSEL, selA, selB);
    SendMessageW(edit_send_, EM_REPLACESEL, TRUE, (LPARAM)rep.c_str());
}

std::vector<uint8_t> MainWindow::CurrentSendPayload() {
    int len = GetWindowTextLengthW(edit_send_);
    std::wstring text((size_t)(len > 0 ? len : 0), 0);
    if (len > 0) GetWindowTextW(edit_send_, &text[0], len + 1);
    if (text.empty()) return {};
    bool hex = cfg_.send_hex || SendMessageW(chk_hex_send_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::vector<uint8_t> b;
    if (hex) {
        if (!ParseHex(text, b)) return {};
        return b;
    }
    std::wstring s = text;
    if (cfg_.send_newline || SendMessageW(chk_newline_, BM_GETCHECK, 0, 0) == BST_CHECKED)
        s += L"\r\n";
    if (!TextWithEscapesToBytes(s, cfg_.encoding, b)) return {};   // §77：\x?? \\ \r\n… 转义（SSCOM 语义）
    return b;
}

void MainWindow::ChecksumParamsNow(std::wstring& kind, int& start, int& tail) {
    int k = (int)SendMessageW(cmb_checksum_, CB_GETCURSEL, 0, 0);
    kind = (k >= 0 && k < kChecksumCount) ? kChecksumValues[k] : L"none";
    wchar_t buf[32]{};
    GetWindowTextW(edit_chk_start_, buf, 32);
    start = _wtoi(buf);
    if (start < 1) start = 1;
    int t = (int)SendMessageW(cmb_chk_tail_, CB_GETCURSEL, 0, 0);      // 项序 0..3=帧尾1..4，4=旧版末尾追加
    tail = (t >= 0 && t < 4) ? t + 1 : (t == 4 ? 9 : 1);
}

void MainWindow::SyncChecksumCfg() {
    std::wstring kind;
    int start = 1, tail = 1;
    ChecksumParamsNow(kind, start, tail);
    cfg_.checksum_kind = kind;
    cfg_.checksum_start = start;
    cfg_.checksum_tail = tail;
}

void MainWindow::UpdateChecksumPreview() {
    std::wstring text;
    std::wstring kind;
    int start = 1, tail = 1;
    ChecksumParamsNow(kind, start, tail);
    if (kind != L"none") {
        std::vector<uint8_t> payload = CurrentSendPayload();
        if (!payload.empty()) {
            std::vector<uint8_t> body = ChecksumBodySlice(payload, start, tail);
            std::vector<uint8_t> app = ChecksumAppendix(kind, body.data(), body.size());
            if (!app.empty()) text = ToHex(app.data(), app.size());
        }
    }
    SetWindowTextW(edit_chk_view_, text.c_str());
}

void MainWindow::ShowFontColorDialog() {
    // §67：ChooseFont（含 CF_EFFECTS → 正文颜色 + 粗体/斜体/下划线）。
    // 背景色 = 独立菜单项「背景颜色」；时间戳颜色/样式 = 色块点击弹窗（整体一色）。
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(cfg_.font.size, 96, 72);
    lf.lfWeight = cfg_.font.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = cfg_.font.italic ? TRUE : FALSE;
    lf.lfUnderline = cfg_.font.underline ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, cfg_.font.family.c_str(), LF_FACESIZE);
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd_;
    cf.lpLogFont = &lf;
    cf.rgbColors = ParseColor(cfg_.text_color, RGB(0, 0, 0));
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
    cf.nFontType = SCREEN_FONTTYPE;
    if (!ChooseFontW(&cf)) return;
    cfg_.font.family = lf.lfFaceName;
    int pt = MulDiv(-lf.lfHeight, 72, 96);
    if (pt < 4) pt = 4;
    if (pt > 72) pt = 72;
    cfg_.font.size = pt;
    cfg_.font.bold = (lf.lfWeight >= FW_BOLD);
    cfg_.font.italic = (lf.lfItalic != FALSE);
    cfg_.font.underline = (lf.lfUnderline != FALSE);
    wchar_t cb[16];
    swprintf(cb, 16, L"#%02X%02X%02X", GetRValue(cf.rgbColors), GetGValue(cf.rgbColors), GetBValue(cf.rgbColors));
    cfg_.text_color = cb;
    ApplyFonts();                                      // 字体重建（数据字体）
    ApplyFontColors();
    Notify(L"字体与颜色已应用（保存参数或关窗时写入配置）");
}

// §67：显示框背景颜色（独立菜单项「背景颜色」）
void MainWindow::ShowBgColorDialog() {
    static COLORREF cust[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd_;
    cc.rgbResult = ParseColor(cfg_.bg_color, RGB(0xFF, 0xFF, 0xFF));
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    wchar_t cb[16];
    swprintf(cb, 16, L"#%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    cfg_.bg_color = cb;
    ApplyViewColors();
    Notify(L"背景颜色已应用（保存参数或关窗时写入配置）");
}

// ---------------- §68 时间戳样式弹窗（色块点击）----------------
// 接收/发送两组各自独立：字体颜色/加粗/倾斜/下划线；家族与字号不可设（跟随显示字体）。
// 自建 WS_POPUP 模态窗（控件树全程序化创建），局部消息循环 + PostQuitMessage 收尾。
namespace {

struct TsStyleSide {
    COLORREF color = RGB(0x80, 0x80, 0x80);
    bool italic = false, underline = false;    // §69：加粗移除（字宽变化破坏折行网格）
};

struct TsStyleCtx {
    TsStyleSide rx, tx;                            // §68 接收/发送各自独立
    bool ok = false;
    HWND swRx = nullptr, swTx = nullptr;
    HWND chkRx[2] = {}, chkTx[2] = {};             // 倾斜/下划线
};

constexpr int kTsSwatchRx = 2111, kTsPickRx = 2112, kTsChkRx = 2113;   // 2113..2115
constexpr int kTsSwatchTx = 2116, kTsPickTx = 2117, kTsChkTx = 2118;   // 2118..2120
constexpr int kTsOk     = IDOK;      // 1
constexpr int kTsCancel = IDCANCEL;  // 2

LRESULT CALLBACK TsStyleProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = (TsStyleCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return DefWindowProcW(h, msg, wp, lp);     // 必须放行：窗口文本（标题）在此存入，否则无标题
    case WM_DRAWITEM: {
        auto* di = (DRAWITEMSTRUCT*)lp;
        if (di && ctx && (di->CtlID == kTsSwatchRx || di->CtlID == kTsSwatchTx)) {
            COLORREF col = (di->CtlID == kTsSwatchRx) ? ctx->rx.color : ctx->tx.color;
            HBRUSH br = CreateSolidBrush(col);
            FillRect(di->hDC, &di->rcItem, br);
            DeleteObject(br);
            DrawEdge(di->hDC, &di->rcItem, EDGE_ETCHED, BF_RECT);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));   // §103：字形单元同色（否则文字背后白块）
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_COMMAND:
        if (!ctx) break;
        switch (LOWORD(wp)) {
        case kTsPickRx:
        case kTsPickTx: {
            static COLORREF cust[16] = {};
            bool isRx = (LOWORD(wp) == kTsPickRx);
            CHOOSECOLORW cc{};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = h;
            cc.rgbResult = isRx ? ctx->rx.color : ctx->tx.color;
            cc.lpCustColors = cust;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                if (isRx) ctx->rx.color = cc.rgbResult;
                else ctx->tx.color = cc.rgbResult;
                InvalidateRect(isRx ? ctx->swRx : ctx->swTx, nullptr, TRUE);
            }
            return 0;
        }
        case kTsOk: {
            for (int i = 0; i < 2; ++i) {
                bool b = SendMessageW(ctx->chkRx[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (i == 0) ctx->rx.italic = b; else ctx->rx.underline = b;
                b = SendMessageW(ctx->chkTx[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (i == 0) ctx->tx.italic = b; else ctx->tx.underline = b;
            }
            ctx->ok = true;
            DestroyWindow(h);
            return 0;
        }
        case kTsCancel:
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY:
        PostQuitMessage(0);                            // 结束宿主局部消息循环
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

void MainWindow::ShowTsStyleDialog() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TsStyleProc;
        wc.hInstance = hInst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SNComTsStyle";
        RegisterClassExW(&wc);
        registered = true;
    }
    TsStyleCtx ctx;
    ctx.rx.color = ParseColor(cfg_.rx_ts_color, RGB(0x80, 0x80, 0x80));
    ctx.rx.italic = cfg_.rx_ts_italic;
    ctx.rx.underline = cfg_.rx_ts_underline;
    ctx.tx.color = ParseColor(cfg_.tx_ts_color, RGB(0x80, 0x80, 0x80));
    ctx.tx.italic = cfg_.tx_ts_italic;
    ctx.tx.underline = cfg_.tx_ts_underline;

    const int W = ScalePx(376, dpi_), H = ScalePx(186, dpi_);
    RECT orr{};
    GetWindowRect(hwnd_, &orr);
    int x = orr.left + ((orr.right - orr.left) - W) / 2;
    int y = orr.top + ((orr.bottom - orr.top) - H) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SNComTsStyle", L"时间戳样式",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               x, y, W, H, hwnd_, nullptr, hInst_, &ctx);
    const int m = ScalePx(14, dpi_), btnH = ScalePx(24, dpi_), gap = ScalePx(8, dpi_);
    const int swW = ScalePx(46, dpi_), pickW = ScalePx(86, dpi_), chkW = ScalePx(62, dpi_);
    const int chkUW = ScalePx(72, dpi_), btnW = ScalePx(88, dpi_);
    // 每组：组标题行 + [色样][字体颜色…][✓倾斜][✓下划线]（§69 无加粗）
    auto group = [&](int yTop, const wchar_t* cap, HWND& sw, HWND* chks, int pickId, int swId, int chkId,
                     const TsStyleSide& st) {
        HWND c = CreateWindowExW(0, L"STATIC", cap, WS_CHILD | WS_VISIBLE,
                                 m, yTop, ScalePx(120, dpi_), ScalePx(16, dpi_),
                                 dlg, nullptr, hInst_, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)uiFont_, TRUE);
        int yc = yTop + ScalePx(18, dpi_);
        sw = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                             m, yc, swW, ScalePx(18, dpi_), dlg, (HMENU)(INT_PTR)swId, hInst_, nullptr);
        HWND bp = CreateWindowExW(0, L"BUTTON", L"字体颜色…",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  m + swW + gap, yc - ScalePx(3, dpi_), pickW, btnH,
                                  dlg, (HMENU)(INT_PTR)pickId, hInst_, nullptr);
        SendMessageW(bp, WM_SETFONT, (WPARAM)uiFont_, TRUE);
        const wchar_t* names[2] = { L"倾斜", L"下划线" };
        int xs[2] = { m + swW + pickW + gap * 2, m + swW + pickW + gap * 2 + chkW + gap };
        int widths[2] = { chkW, chkUW };
        for (int i = 0; i < 2; ++i) {
            chks[i] = CreateWindowExW(0, L"BUTTON", names[i],
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                      xs[i], yc, widths[i], ScalePx(20, dpi_),
                                      dlg, (HMENU)(INT_PTR)(chkId + i), hInst_, nullptr);
            SendMessageW(chks[i], WM_SETFONT, (WPARAM)uiFont_, TRUE);
            SendMessageW(chks[i], BM_SETCHECK, (i == 0 ? st.italic : st.underline)
                                             ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        return yc + ScalePx(26, dpi_);                 // 组底
    };
    int ly = m;
    ly = group(ly, L"接收时间戳", ctx.swRx, ctx.chkRx, kTsPickRx, kTsSwatchRx, kTsChkRx, ctx.rx);
    ly = group(ly + gap, L"发送时间戳", ctx.swTx, ctx.chkTx, kTsPickTx, kTsSwatchTx, kTsChkTx, ctx.tx);
    HWND btnOk = CreateWindowExW(0, L"BUTTON", L"确定",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 W - m - 2 * btnW - gap, ly + gap, btnW, btnH,
                                 dlg, (HMENU)(INT_PTR)kTsOk, hInst_, nullptr);
    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     W - m - btnW, ly + gap, btnW, btnH,
                                     dlg, (HMENU)(INT_PTR)kTsCancel, hInst_, nullptr);
    SendMessageW(dlg, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    for (HWND c : {btnOk, btnCancel})
        SendMessageW(c, WM_SETFONT, (WPARAM)uiFont_, TRUE);

    EnableWindow(hwnd_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessage(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(dlg)) DestroyWindow(dlg);
    EnableWindow(hwnd_, TRUE);
    SetFocus(hwnd_);
    if (ctx.ok) {
        wchar_t cb[16];
        swprintf(cb, 16, L"#%02X%02X%02X", GetRValue(ctx.rx.color), GetGValue(ctx.rx.color), GetBValue(ctx.rx.color));
        cfg_.rx_ts_color = cb;
        cfg_.rx_ts_italic = ctx.rx.italic;
        cfg_.rx_ts_underline = ctx.rx.underline;
        swprintf(cb, 16, L"#%02X%02X%02X", GetRValue(ctx.tx.color), GetGValue(ctx.tx.color), GetBValue(ctx.tx.color));
        cfg_.tx_ts_color = cb;
        cfg_.tx_ts_italic = ctx.tx.italic;
        cfg_.tx_ts_underline = ctx.tx.underline;
        ApplyFonts();                                  // 重建收/发 tsFont_ + 视图重排
        ApplyFontColors();                             // 色块重绘
        Notify(L"时间戳样式已应用（保存参数或关窗时写入配置）");
    }
}

void MainWindow::ApplyFontColors() {
    ApplyViewColors();
    if (chip_) InvalidateRect(chip_, nullptr, TRUE);  // 时间戳色块重绘
}

void MainWindow::UpdateCounters() {
    txRate_ = (txBytes_ - txLast_) * 4.0;          // 250ms 采样 → 换算 B/s
    rxRate_ = (rxBytes_ - rxLast_) * 4.0;
    txLast_ = txBytes_;
    rxLast_ = rxBytes_;
    wchar_t b1[64], b2[64];
    swprintf(b1, 64, L"TX: %u (%.0f B/s)", txBytes_, txRate_);
    swprintf(b2, 64, L"RX: %u (%.0f B/s)", rxBytes_, rxRate_);
    // §120（十八轮 P3-2 空闲 CPU 专项）：文本未变化就不重发 SB_SETTEXTW——状态栏段对同文本
    // 也会失效重绘，空闲 4Hz×2 段等于常驻重绘；改「变化才写」后空闲零成本，数据态行为不变。
    if (sbTxText_ != b1) { sbTxText_ = b1; SendMessageW(status_, SB_SETTEXTW, 0, (LPARAM)b1); }
    // §77（SSCOM V5.07/V5.09a 语义）：显示缓冲占用 ≥60% 时 RX 段 owner-draw 渐染淡红
    // （数据过多警示；<60% 恢复普通文本）
    rxText_ = b2;
    size_t cap = buf_.cap();
    rxTintPct_ = cap ? (int)(buf_.size() * 100 / cap) : 0;
    if (rxTintPct_ > 100) rxTintPct_ = 100;
    if (sbRxText_ != b2) { sbRxText_ = b2; SendMessageW(status_, SB_SETTEXTW, 1, (LPARAM)b2); }
    SyncTintOverlay();          // §77：占用 ≥60% 覆盖窗染红（见函数内说明）
}

// §103（用户拍板）：关闭串口 / 切换到其他串口 / 切换到 TCP-UDP 时 RX/TX 计数清零。
// 三类场景都经过 CloseSerial/CloseNet 收口（含换波特率自动重开的 CloseSerial 路径与
// 模式切换 CloseLink）；立即 UpdateCounters 刷新状态栏，不等 250ms tick。
void MainWindow::ResetCounters() {
    txBytes_ = rxBytes_ = 0;
    txLast_ = rxLast_ = 0;
    txRate_ = rxRate_ = 0;
    UpdateCounters();
}

// §77：RX 段染色实现选型——SB_SETTEXTW(MAKEWPARAM(1, SBT_OWNERDRAW)) 在 comctl32 v6
// （Win11 26100 实测）返回 0 被拒，该段直接空白；状态栏亦不支持 NM_CUSTOMDRAW。
// 故改用 ownerdraw STATIC 覆盖窗（SS_OWNERDRAW → 主窗 WM_DRAWITEM），只覆盖不可交互的
// RX 段：≥60% 显示 + 重绘，回落 SW_HIDE。位置每次由 SB_GETRECT 现取，随布局自动跟随。
void MainWindow::SyncTintOverlay() {
    if (!tint_ || !status_) return;
    bool on = rxTintPct_ >= 60;
    if (!on) {
        if (IsWindowVisible(tint_)) ShowWindow(tint_, SW_HIDE);
        return;
    }
    RECT rc{};
    if (!SendMessageW(status_, SB_GETRECT, 1, (LPARAM)&rc)) {   // 失败：宁可不画
        if (IsWindowVisible(tint_)) ShowWindow(tint_, SW_HIDE);
        return;
    }
    MapWindowPoints(status_, hwnd_, (POINT*)&rc, 2);
    SetWindowPos(tint_, HWND_TOP, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(tint_, nullptr, TRUE);
}

// ---------------- --spike-selftest：200 行渲染 + 软件回环收发自检 ----------------
void MainWindow::SelftestInit() {
    for (int i = 0; i < 200; ++i) {
        wchar_t b[160];
        swprintf(b, 160,
                 L"M0.5 spike 渲染压力行 %03d ｜ ASCII: 0123456789 ABCDEFGHIJ ｜ 中文吞吐：串口/网络数据调试器",
                 i + 1);
        std::vector<uint8_t> raw;
        if (WideToBytes(std::wstring(b) + L"\r\n", cfg_.encoding, raw)) {
            newData_ = true;
            builder_.feed(false, raw.data(), raw.size(), NowSeconds(), buf_.end_abs());
        }
    }
    rxv_.NotifyDataChanged();
    stFrame_ = 0;
    SetTimer(hwnd_, 3, 40, nullptr);               // 60 帧 × 40ms
}

void MainWindow::SelftestTick() {
    if (stFrame_ == 0) {
        // 自检用软件回环（确定性 RX；真口收发由用户手动「打开串口」验证）
        SerialParams p = GatherParams();
        p.loopback = true;
        if (engine_.Open(hwnd_, p)) {
            loopMode_ = true;
            SendMessageW(chk_rx_file_, BM_SETCHECK, BST_CHECKED, 0);
            ToggleRxLog(true);                     // 顺带验证 100ms 转存日志
            Notify(L"自检：软件回环 + 转存日志已启动（I/O 线程 + PostMessage + 渲染链路）");
        } else {
            KillTimer(hwnd_, 3);
            Notify(L"自检中止：软件回环打开失败");
            return;
        }
    }
    std::vector<uint8_t> b;
    wchar_t buf[96];
    if (stFrame_ < 62) {                           // 发送阶段固定 0..61（61 收尾一帧二进制）
        if (stFrame_ == 10) {
            // 空行语义用例：真实连续换行保留空行（display.py LineBuilder 行为规格）
            std::wstring s = L"空行测试首行\r\n\r\n尾行\r\n";
            if (!WideToBytes(s, cfg_.encoding, b)) return;
        } else if (stFrame_ % 2 == 0) {
            swprintf(buf, 96, L"回环帧 #%d：中文内容吞吐测试 串口/网络数据调试器 0123456789\r\n", stFrame_);
            std::wstring s = buf;
            if (!WideToBytes(s, cfg_.encoding, b)) return;
        } else {
            b.resize(24);
            for (size_t i = 0; i < b.size(); ++i) b[i] = (uint8_t)(stFrame_ + i);
        }
        if (!b.empty()) SendBytes(b);
    }
    ++stFrame_;
    if (stFrame_ > 60) {
        // P3-2：最后一帧 RX 经 I/O 线程异步回传，立即报数恒差一帧——
        // 等 RX 追平 TX 再收尾报数；40ms×25 = 1 s 上限防意外不追平。
        if (rxBytes_ < txBytes_ && stFrame_ < 86) return;
        KillTimer(hwnd_, 3);
        SendMessageW(chk_rx_file_, BM_SETCHECK, BST_UNCHECKED, 0);
        ToggleRxLog(false);                        // 收尾补写 + 关句柄，供检查日志内容
        wchar_t done[96];
        swprintf(done, 96, L"自检完成：TX %u B / RX %u B（渲染/滚动/选区请目检，IME 请手测）",
                 txBytes_, rxBytes_);
        Notify(done);
    }
}

// ================= M2-A：显示管线 tick / 转存日志 / 保存 =================

static int GetEditInt(HWND h, int def) {
    wchar_t b[32]{};
    GetWindowTextW(h, b, 32);
    int v = _wtoi(b);
    return v > 0 ? v : def;
}

void MainWindow::RxLineSourceInit() {
    rxv_.SetLineSource(this, &MainWindow::RxCountThunk, &MainWindow::RxGetLineThunk);
}

int MainWindow::RxCountThunk(void* ctx) {
    auto* m = static_cast<MainWindow*>(ctx);
    return m->builder_.line_count() - m->builder_.base();   // 现存行 + 活动行
}

bool MainWindow::RxGetLineThunk(void* ctx, int viewIdx, RxView::Line& out) {
    auto* m = static_cast<MainWindow*>(ctx);
    const DispLine* l = m->builder_.line_at(m->builder_.base() + viewIdx);
    if (!l) return false;
    bool split = m->lastSplit_;
    out.ts = split ? l->prefix : L"";
    out.mark = (split && !l->prefix.empty()) ? DirMarker(l->is_tx) : L"";
    out.body = l->text;
    out.isTx = l->is_tx;
    return true;                                    // 颜色由 rxview 按 isTx 上
}

// §138：空闲降档的恢复入口——数据到达/终端回显/HEX·分包勾选/待发入队把 id4（显示 tick）
// 与 id15（守卫+待发兜底）拉回快拍。周期未变时不调 SetTimer（RX 热路径零成本）。
void MainWindow::RequestFastDisplay() {
    dispIdle_ = 0;
    if (dispTickMs_ != 33) { dispTickMs_ = 33; SetTimer(hwnd_, 4, 33, nullptr); }
    if (guardTickMs_ != 60) { guardTickMs_ = 60; SetTimer(hwnd_, 15, 60, nullptr); }
}

void MainWindow::TickDisplay() {
    // 控件 → builder 配置（hex 切换会 flush 当前包，只在变化时调用）
    bool hex = cfg_.hex_mode || (SendMessageW(chk_hex_show_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (hex != lastHex_) { lastHex_ = hex; builder_.set_hex_mode(hex); rxv_.SetHexUnits(hex); }
    bool split = cfg_.split_show && (SendMessageW(chk_split_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int ms = GetEditInt(edit_split_ms_, cfg_.split_ms);
    if (split != lastSplit_ || ms != lastSplitMs_) {
        lastSplit_ = split;
        lastSplitMs_ = ms;
        cfg_.split_show = split;
        cfg_.split_ms = ms;
        // C2c：split 回溯在直渲下天然生效——prefix 存于行记录、带不带出是 GetLine
        // 渲染时决定（PyCom 口径：切换对已收数据同样生效），无需清视图重灌；
        // 但每行文本口径变了 → 折行显示行索引需全量重排（§62）
        rxv_.Reflow();
    }
    builder_.set_split(split, ms);

    // J-1 空闲早退：无新数据且无未收尾行（open 行超时 flush 需 maybe_flush，不可早退）→
    // 跳过全部主体（HEX/split 配置检测在上面，不受影响）。
    // §138：空闲降档——连续早退把显示 tick（id4）33→100→250→500ms 分档拉慢；数据到达/
    // 终端回显/HEX·分包勾选由 RequestFastDisplay 拉回 33ms（周期未变时零成本）。
    // 有未收尾行（builder_.open()）时不降档：open 行超时 flush 仍需 33ms 拍。
    bool nd = newData_;
    newData_ = false;
    if (!nd && !builder_.open()) {
        ++dispIdle_;
        UINT want = (dispIdle_ >= 30) ? 500 : (dispIdle_ >= 12) ? 250 : (dispIdle_ >= 3) ? 100 : 33;
        if (want != dispTickMs_) { dispTickMs_ = want; SetTimer(hwnd_, 4, want, nullptr); }
        return;
    }
    RequestFastDisplay();

    builder_.maybe_flush(NowSeconds());

    // 字节缓冲丢最旧 → 行表跟随裁剪（转存游标前的行不许裁，PyCom 同款）
    if (buf_.start_abs() != lastBufStart_) {
        lastBufStart_ = buf_.start_abs();
        int keep = (logFile_ != INVALID_HANDLE_VALUE) ? logWritten_ : -1;
        builder_.set_log_keep(keep);
        builder_.trim_before_abs(buf_.start_abs(), keep);
    }
    // 直渲同步：视图坐标原点跟随 builder 裁剪基址；仅数据变化时通知视图
    // （I-1：H-2 初版每拍无条件 NotifyDataChanged → 空闲也 30fps 全帧重绘，
    //   实测空闲 CPU 3.6~5.2%——现在行数/base/活动行长度均不变则跳过）
    int cnt = builder_.line_count();
    int base = builder_.base();
    const DispLine* opNow = builder_.open();
    size_t openLen = opNow ? (opNow->text.size() + opNow->prefix.size()) : 0;
    if (cnt != lastCnt_ || base != lastBase_ || openLen != lastOpenLen_) {
        lastCnt_ = cnt; lastBase_ = base; lastOpenLen_ = openLen;
        rxv_.SyncBase(base);
        rxv_.NotifyDataChanged();
    }
}

void MainWindow::ToggleRxLog(bool on) {
    if (on) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t ts[32];
        swprintf(ts, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        std::wstring path = JoinPath(ExeDir(), (std::wstring(L"SNCom-Log-") + ts + L".txt"));
        int k = 1;
        while (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {   // 同秒不覆盖
            wchar_t n[32];
            swprintf(n, 32, L"-%d", k++);
            path = JoinPath(ExeDir(), (std::wstring(L"SNCom-Log-") + ts + n + L".txt"));
        }
        logFile_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logFile_ == INVALID_HANDLE_VALUE) {
            Notify(L"接收数据到文件：目录不可写或创建失败");
            SendMessageW(chk_rx_file_, BM_SETCHECK, BST_UNCHECKED, 0);
            return;
        }
        logWritten_ = builder_.base() + (int)builder_.finished();   // 只记录"之后"的内容
        builder_.set_log_keep(logWritten_);
        SetTimer(hwnd_, 5, 100, nullptr);
        size_t slash = path.find_last_of(L"\\/");
        Notify((L"接收数据将存到：" +
                (slash != std::wstring::npos ? path.substr(slash + 1) : path) +
                L"（编码 " + cfg_.encoding + L"）").c_str());
    } else {
        KillTimer(hwnd_, 5);
        RxLogPoll();
        RxLogFlushTail();
        DetachRxLog();
    }
}

bool MainWindow::WriteLogText(const std::wstring& s) {
    return WriteAllTo(logFile_, s);
}

// 按 cfg_.encoding 把文本编码后整体写入句柄（转存日志 / 保存 txt 共用；审查 P3-4）
bool MainWindow::WriteAllTo(HANDLE h, const std::wstring& s) {
    if (h == INVALID_HANDLE_VALUE || s.empty()) return true;
    int cp = CodePageOf(cfg_.encoding);
    std::vector<uint8_t> b;
    if (cp == 1200) { b.resize(s.size() * 2); memcpy(b.data(), s.data(), b.size()); }
    else if (cp == 1201) {
        b.resize(s.size() * 2);
        for (size_t i = 0; i < s.size(); ++i) { b[i*2] = (uint8_t)(s[i] >> 8); b[i*2+1] = (uint8_t)(s[i] & 0xFF); }
    } else {
        int len = WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return false;
        b.resize((size_t)len);
        WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), (char*)b.data(), len, nullptr, nullptr);
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, b.data(), (DWORD)b.size(), &wrote, nullptr) && wrote == b.size();
    FlushFileBuffers(h);
    return ok;
}

void MainWindow::RxLogPoll() {
    if (logFile_ == INVALID_HANDLE_VALUE) return;
    int base = builder_.base();
    int upper = base + (int)builder_.finished();
    int start = logWritten_ > base ? logWritten_ : base;
    if (start >= upper) { logWritten_ = upper; return; }
    std::wstring parts;
    for (int i = start; i < upper; ++i) {
        DispLine* l = builder_.line_at(i);
        if (!l || l->nl_done) continue;
        // 注记随「加时间戳和分包显示」（PyCom：转存不受复制/保存两开关影响）
        std::wstring prefix = (l->written == 0 && builder_.split) ? l->prefix : L"";
        std::wstring marker = (l->written == 0 && !prefix.empty()) ? DirMarker(l->is_tx) : L"";
        parts += prefix + marker + l->text.substr((size_t)l->written) + L"\r\n";
    }
    if (!parts.empty() && !WriteLogText(parts)) {
        DetachRxLog();
        KillTimer(hwnd_, 5);
        SendMessageW(chk_rx_file_, BM_SETCHECK, BST_UNCHECKED, 0);
        Notify(L"日志写入失败，已停止接收数据到文件");
        return;
    }
    for (int i = start; i < upper; ++i) {           // 写入成功后才推进游标
        DispLine* l = builder_.line_at(i);
        if (!l) continue;
        l->written = (int)l->text.size();
        l->nl_done = true;
    }
    logWritten_ = upper;
}

void MainWindow::RxLogFlushTail() {
    if (logFile_ == INVALID_HANDLE_VALUE) return;
    std::wstring t = builder_.open_line_delta();
    if (t.empty()) return;
    if (!WriteLogText(t + L"\r\n")) { DetachRxLog(); return; }
    builder_.commit_open(true);
}

void MainWindow::DetachRxLog() {
    if (logFile_ != INVALID_HANDLE_VALUE) { CloseHandle(logFile_); logFile_ = INVALID_HANDLE_VALUE; }
    builder_.set_log_keep(-1);
}

void MainWindow::SaveReceived() {
    wchar_t file[MAX_PATH]{};
    StringCchCopyNW(file, MAX_PATH, (JoinPath(ExeDir(), L"received.txt")).c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"文本 (*.txt)\0*.txt\0原始字节 (*.bin)\0*.bin\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::wstring path(file);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Notify(L"保存失败：无法创建文件"); return; }
    bool ok = false;
    size_t dot = path.find_last_of(L'.');
    bool isBin = (dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, L".bin") == 0);
    if (isBin) {
        std::vector<uint8_t> b = buf_.tail(buf_.size());     // 原始接收字节（受 buffer_cap 裁剪）
        DWORD w = 0;
        ok = b.empty() ? true : (WriteFile(h, b.data(), (DWORD)b.size(), &w, nullptr) && w == b.size());
    } else {
        // txt 走 builder 全部行（← PyCom plain_text() 取 builder.visible()）——
        // 不再截断到视图 2 万行（审查 P2-1/C1b）；anno 口径同 PyCom：
        // 包首行（prefix 非空）带 [时间戳]+方向标识
        std::wstring s;
        int n = builder_.line_count();
        for (int i = builder_.base(); i < n; ++i) {
            const DispLine* l = builder_.line_at(i);
            if (!l) continue;
            if (rxv_.save_with_anno)
                s += l->prefix + (l->prefix.empty() ? L"" : DirMarker(l->is_tx));
            s += l->text;
            s += L"\r\n";
        }
        if (builder_.open()) {
            const DispLine* l = builder_.open();
            if (rxv_.save_with_anno)
                s += l->prefix + (l->prefix.empty() ? L"" : DirMarker(l->is_tx));
            s += l->text;
            s += L"\r\n";
        }
        ok = WriteAllTo(h, s);
    }
    CloseHandle(h);
    Notify(ok ? L"保存成功" : L"保存失败：写入出错");
}

void MainWindow::ClearDisplayAll() {
    if (logFile_ != INVALID_HANDLE_VALUE) {         // 清除前把已完成/未收尾行补写进日志
        RxLogPoll();
        RxLogFlushTail();
    }
    builder_.clear();
    buf_.clear();
    rxv_.Clear();
    rxv_.SyncBase(0);
    lastBufStart_ = 0;
    logWritten_ = 0;
}

// ================= 阶段 D 壳层：置顶 / 新窗口 / 收起 / 查找 / 扩展小窗 =================

void MainWindow::ToggleTopmost() {
    topmost_ = !topmost_;
    SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainWindow::NewWindow() {
    // ← PyCom _new_window：再启动一个独立实例（同串口资源由系统互斥）
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (ShellExecuteW(hwnd_, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL) <= (HINSTANCE)32)
        Notify(L"新窗口启动失败");
}

// §107（用户报「无效」）：这两枚小工具此前被并进「小工具→扩展窗切页」区间分支
// （id >= IDM_TOOL_CALC && id <= IDM_TOOL_EXPLORER），只切了扩展窗页 0、从未启动
// 外部程序——修复=显式路由到本处两个方法（区间收窄为 …<=IDM_TOOL_MS）。
void MainWindow::OpenDeviceManager() {
    // devmgmt.msc = 系统设备管理器（Win7~Win11 同名同路径；标准用户由系统弹 UAC）
    if (ShellExecuteW(hwnd_, L"open", L"devmgmt.msc", nullptr, nullptr, SW_SHOWNORMAL) <= (HINSTANCE)32)
        Notify(L"无法打开设备管理器（devmgmt.msc）");
    else
        Notify(L"已打开设备管理器");
}

void MainWindow::OpenComputerFolder() {
    // 小工具「文件资源管理器」= 打开「我的电脑（Win7）/此电脑（Win10+）」——同一 shell
    // 命名空间 {20D04FE0-3AEA-1069-A2D8-08002B30309D}，显示名随系统本地化
    // （← PyCom os.startfile(GUID)；实测 GUID 作 lpFile 由 shell 解析、返回 >32 并弹窗）
    if (ShellExecuteW(hwnd_, L"open", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                      nullptr, nullptr, SW_SHOWNORMAL) <= (HINSTANCE)32)
        Notify(L"无法打开文件资源管理器");
    else
        Notify(L"已打开文件资源管理器");
}

void MainWindow::ApplyCollapse() {
    SetWindowTextW(btn_collapse_, collapsed_ ? L"展开" : L"收起");
    Layout(true);                              // 原子化：面板折叠防中间态闪烁
    // §86（用户报：960×720 下收起→展开仍有残影——被隐藏的「发 送」按钮下半截/收起前行
    // 残留，拖动窗口大小才消失）：原子路径（SETREDRAW→重排→RedrawWindow(UPDATENOW、
    // 无 ERASE)）后 DWM 表面未同步，被隐藏/移走控件的旧像素留在屏幕上。这里等效用户
    // 手工"拖一下窗口"：①补一发 WM_SIZE 走非原子 Layout（所有子控件真实 SetWindowPos
    // 一遍）②带 ERASE+FRAME 的整窗同步重绘（Layout(true) 的守卫刻意无 ERASE 防 §57
    // 灰闪，这里只做一次最终擦除、UPDATENOW 立即上屏，肉眼不可辨）。
    // §88（审查 T4）：此前借 SendMessage(WM_SIZE) 触发非原子重排，但 WM_SIZE 分支里
    // "扩展窗已开且被隐藏→回显"的旁路会在收起/展开时顺带把扩展窗显示出来；改为直接
    // Layout(false)（非原子：子控件真实移动重绘，等效拖动的重绘部分）。
    Layout(false);
    RedrawWindow(hwnd_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    Notify(collapsed_ ? L"已收起底部参数面板（连接与收发不受影响）"
                      : L"已展开底部参数面板");
}

void MainWindow::ToggleFind() {
    // §75：查找页在扩展窗（用户拍板迁出顶部）——点「查找」= 打开扩展并切到查找页；
    // 已在查找页则仅聚焦（不再有「关闭」态按钮文案）
    if (!extOpen_) ToggleExt(true);
    SendMessageW(extTab_, TCM_SETCURSEL, 3, 0);
    ShowExtPage(3);
    SetFocus(findEdit_);
    FindRescanSoon();
}

void MainWindow::FindRescanSoon() {
    SetTimer(hwnd_, 8, 300, nullptr);              // 防抖（← PyCom 重扫防抖）
}

static bool CaselessFind(const std::wstring& hay, const std::wstring& needle, size_t& pos) {
    // 大小写不敏感子串查找（std::wstring 无内建）
    if (needle.empty() || hay.size() < needle.size()) return false;
    size_t n = hay.size(), m = needle.size();
    for (size_t i = 0; i + m <= n; ++i) {
        size_t k = 0;
        while (k < m && towlower(hay[i + k]) == towlower(needle[k])) ++k;
        if (k == m) { pos = i; return true; }
    }
    return false;
}

void MainWindow::FindRescan() {
    KillTimer(hwnd_, 8);
    if (!findOpen_) return;
    int qlen = GetWindowTextLengthW(findEdit_);
    std::wstring q((size_t)qlen, 0);
    if (qlen > 0) GetWindowTextW(findEdit_, &q[0], qlen + 1);
    bool hex = SendMessageW(findHex_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    findHits_.clear();
    findCur_ = 0;
    do {
        if (q.empty()) break;
        if (!hex) {                                // 文本模式：在 builder 行文本里找
            bool cs = SendMessageW(findCase_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            for (int abs = builder_.base(); abs < builder_.line_count(); ++abs) {
                const DispLine* l = builder_.line_at(abs);
                if (!l) continue;
                std::wstring t = (l->prefix.empty() ? L"" : L"") + l->text;   // 搜正文（前缀不参与）
                size_t pos = 0;
                bool hit = cs ? ((pos = t.find(q)) != std::wstring::npos)
                              : CaselessFind(t, q, pos);
                if (hit) findHits_.push_back(abs);
            }
        } else {                                   // HEX 模式：在原始字节流里找 → 行映射
            std::vector<uint8_t> pat;
            if (!ParseHex(q, pat) || pat.empty()) break;
            std::vector<uint8_t> hay = buf_.tail(buf_.size());
            uint64_t baseOff = buf_.start_abs();
            if (hay.size() < pat.size()) break;
            for (size_t i = 0; i + pat.size() <= hay.size(); ++i) {
                if (memcmp(hay.data() + i, pat.data(), pat.size()) == 0) {
                    // offset → 逻辑行映射（行表 start_abs/nbytes 单调推进）
                    uint64_t off = baseOff + i;
                    for (int abs = builder_.base(); abs < builder_.line_count(); ++abs) {
                        const DispLine* l = builder_.line_at(abs);
                        if (!l) continue;
                        if (off >= l->start_abs &&
                            off < l->start_abs + (uint64_t)l->nbytes) {
                            if (findHits_.empty() || findHits_.back() != abs)
                                findHits_.push_back(abs);   // 跨行命中计一次
                            break;
                        }
                    }
                    i += pat.size() - 1;
                }
            }
        }
    } while (false);
    wchar_t cnt[32];
    swprintf(cnt, 32, L"0/%d", (int)findHits_.size());
    SetWindowTextW(findCount_, cnt);
    if (!findHits_.empty()) FindJump(1);           // 重扫后跳第一个命中
}

void MainWindow::FindJump(int dir) {
    if (findHits_.empty()) return;
    findCur_ = (findCur_ + dir + (int)findHits_.size()) % (int)findHits_.size();
    wchar_t cnt[32];
    swprintf(cnt, 32, L"%d/%d", findCur_ + 1, (int)findHits_.size());
    SetWindowTextW(findCount_, cnt);
    RevealAbsLine(findHits_[findCur_]);
}

void MainWindow::RevealAbsLine(int absLine) {
    // H-2 直渲：视图可见范围 = builder 现存行——命中行只要未被裁剪就必然可达，
    // 原副本模型的"钳 0/重灌"机制整体消失
    int view = absLine - builder_.base();
    if (view < 0) view = 0;
    rxv_.RevealLine(view);                         // 跳转即视图移动 → 自动滚屏取消（rxview 内）
}

// ---------------- 扩展独立小窗（D5） ----------------

enum { IDC_EXT_TAB = 1, IDC_CALC_DEC, IDC_CALC_HEX, IDC_CALC_HEXLE, IDC_CALC_HEXBE,
       IDC_CALC_BIN, IDC_CALC_ASCII, IDC_CALC_F32LE, IDC_CALC_F32BE,
       IDC_CALC_F64LE, IDC_CALC_F64BE, IDC_CALC_CARR, IDC_ASCII_LIST,
       // §129 协议发送页（追加末尾——§67 教训：绝不插入中部造成既有 id 平移；
       // XCHECK 显式 =26：多条发送页的原始 id 20~25（msList_/msEdit_/四按钮）先占，
       // 枚举自然延续到 20 会撞 msList_（续修3 实测 GetDlgItem(20) 返回列表框））
       IDC_PROTO_MODE, IDC_PROTO_FILE, IDC_PROTO_OPEN, IDC_PROTO_START,
       IDC_PROTO_CANCEL, IDC_PROTO_STATUS, IDC_XCHECK_MODE = 26, IDC_XBLK_MODE = 27,
       IDC_PROTO_SHOW = 28, IDC_PROTO_TIMEOUT = 29,
       IDC_PROTO_RETRIES = 30 };   // §131/续：超时与次数输入追加末尾（§67 红线）

static const wchar_t* kCalcNames[10] = {
    L"十进制 DEC", L"十六进制 HEX", L"HEX 小端 LE", L"HEX 大端 BE", L"二进制 BIN",
    L"ASCII 字符", L"float32 LE", L"float32 BE", L"float64 LE", L"float64 BE",
};

void MainWindow::CreateExtWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ExtProcThunk;
    wc.hInstance = hInst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SNComExt";
    RegisterClassExW(&wc);

    extWnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"SNComExt", L"SNCom 扩展",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                              WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, ScalePx(480, dpi_), 400,
                              hwnd_, nullptr, hInst_, this);     // owner=hwnd（随主窗最前台）
    if (!extWnd_) return;

    // 页签
    extTab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_EXT_TAB, hInst_, nullptr);
    // §98（用户拍板：参照「HEX发送」行观感，扩展窗整体灰）：页签控件去主题——themed tab
    // 的显示区自绘为浅色(本机 249)，classic tab 的显示区透出父窗背景(ext hbrBackground
    // =COLOR_BTNFACE 240)，于是页面统一为 240，观感与主界面 chrome 一致。§101 起标签/
    // 复选框底色直接用同一 COLOR_BTNFACE（确定性同源）；仅影响本控件自身绘制
    // （§95 教训=勿对 ext 整窗去主题）。
    SetWindowTheme(extTab_, L"", L"");
    SendMessageW(extTab_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"多条发送";
    SendMessageW(extTab_, TCM_INSERTITEMW, 0, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"进制转换";
    SendMessageW(extTab_, TCM_INSERTITEMW, 1, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"ASCII码表";
    SendMessageW(extTab_, TCM_INSERTITEMW, 2, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"查找";                        // §75：查找迁入扩展窗（用户拍板）
    SendMessageW(extTab_, TCM_INSERTITEMW, 3, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"协议发送";                    // §129：XMODEM/YMODEM 发送端（用户拍板第 5 页）
    SendMessageW(extTab_, TCM_INSERTITEMW, 4, (LPARAM)&ti);

    // 页 0：多条字符串发送（E：100 条表格 + 工具按钮，← PyCom multisend）
    msList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                              WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                              0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)20, hInst_, nullptr);
    SendMessageW(msList_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    ListView_SetExtendedListViewStyle(msList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP);   // §77：+悬停提示
    LVCOLUMNW mc{};
    mc.mask = LVCF_TEXT | LVCF_WIDTH;
    const wchar_t* mcols[6] = { L"点击发送", L"HEX", L"内容", L"顺序", L"延时ms", L"注释" };
    // §54：注释列 80→110（占位「点击修改注释」6 汉字需 ~96+边距）
    // §78：末列改拉伸填满（见 MsStretchLastColumn）——右缘不再留"空列"（用户报多一空列）
    int mwds[6] = { kMsColW[0], kMsColW[1], kMsColW[2], kMsColW[3], kMsColW[4], kMsColW[5] };
    for (int c = 0; c < 6; ++c) {
        mc.pszText = (LPWSTR)mcols[c];
        mc.cx = ScalePx(mwds[c], dpi_);
        SendMessageW(msList_, LVM_INSERTCOLUMNW, c, (LPARAM)&mc);
    }
    msEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10,
                              extWnd_, (HMENU)(INT_PTR)21, hInst_, nullptr);
    SendMessageW(msEdit_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    struct { const wchar_t* t; int id; } mbtns[4] = {
        { L"新增", 22 }, { L"删除", 23 }, { L"导入ini", 24 }, { L"循环发送", 25 } };
    for (int i = 0; i < 4; ++i) {
        HWND b = CreateWindowExW(0, L"BUTTON", mbtns[i].t,
                                 WS_CHILD | (i == 3 ? (BS_AUTOCHECKBOX | BS_PUSHLIKE) : BS_PUSHBUTTON),
                                 0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)mbtns[i].id, hInst_, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)uiFont_, TRUE);
        if (i == 0) msAddBtn_ = b;
        if (i == 1) msDelBtn_ = b;
        if (i == 2) msImpBtn_ = b;
        if (i == 3) msLoopBtn_ = b;
    }
    ShowWindow(msList_, SW_HIDE);                  // 各页由页签切换显隐（初始页 0 显示）
    ShowWindow(msEdit_, SW_HIDE);
    ShowWindow(msAddBtn_, SW_HIDE);
    ShowWindow(msDelBtn_, SW_HIDE);
    ShowWindow(msImpBtn_, SW_HIDE);
    ShowWindow(msLoopBtn_, SW_HIDE);
    MsLoadFromCfg();                               // §132：优先恢复持久化表格
    if (msRows_.empty()) msRows_.push_back(MsRow());   // 首次运行：保留一行示例（原行为）
    MsSyncList();
    if (cfg_.multisend_loop) {                     // §132：循环勾选恢复（断开时 MsLoopNext 静默等待）
        SendMessageW(msLoopBtn_, BM_SETCHECK, BST_CHECKED, 0);
        msLoopActive_ = true;
        MsLoopNext();
    }

    // 页 1：进制转换（10 字段联动 + C 数组，← PyCom CalculatorWidget）
    for (int i = 0; i < 10; ++i) {
        calcLbls_[i] = CreateWindowExW(0, L"STATIC", kCalcNames[i], WS_CHILD,
                                       0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
        SendMessageW(calcLbls_[i], WM_SETFONT, (WPARAM)uiFont_, TRUE);
        calcEdits_[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | ES_AUTOHSCROLL,
                                        0, 0, 10, 10, extWnd_,
                                        (HMENU)(INT_PTR)(IDC_CALC_DEC + i), hInst_, nullptr);
        SendMessageW(calcEdits_[i], WM_SETFONT, (WPARAM)uiFont_, TRUE);
    }
    calcCarrLbl_ = CreateWindowExW(0, L"STATIC", L"C 数组", WS_CHILD,
                                   0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(calcCarrLbl_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    calcCarr_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_CALC_CARR, hInst_, nullptr);
    SendMessageW(calcCarr_, WM_SETFONT, (WPARAM)uiFont_, TRUE);

    // 页 2：ASCII 码表（0-127 全表，双击复制字符）
    asciiList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_ASCII_LIST, hInst_, nullptr);
    SendMessageW(asciiList_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    ListView_SetExtendedListViewStyle(asciiList_, LVS_EX_FULLROWSELECT);
    LVCOLUMNW lc{};
    lc.mask = LVCF_TEXT | LVCF_WIDTH;
    const wchar_t* cols[5] = { L"十进制", L"十六进制", L"二进制", L"字符", L"缩写/说明" };
    int cwds[5] = { 52, 52, 64, 40, 110 };
    for (int c = 0; c < 5; ++c) {
        lc.pszText = (LPWSTR)cols[c];
        lc.cx = ScalePx(cwds[c], dpi_);
        SendMessageW(asciiList_, LVM_INSERTCOLUMNW, c, (LPARAM)&lc);
    }
    FillAsciiList();

    // 页 3：查找（§75：由主窗顶部查找条迁入——控件为扩展窗子级，页签切换显隐；
    // 初始全隐藏，打开扩展/切到本页时由 ShowExtPage 统一同步）
    findEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_EDIT, hInst_, nullptr);
    SendMessageW(findEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"查找内容（文本或 HEX 字节）");
    findCase_ = CreateWindowExW(0, L"BUTTON", L"区分大小写", WS_CHILD | BS_AUTOCHECKBOX,
                                0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_CASE, hInst_, nullptr);
    findHex_ = CreateWindowExW(0, L"BUTTON", L"HEX", WS_CHILD | BS_AUTOCHECKBOX,
                               0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_HEX, hInst_, nullptr);
    findCount_ = CreateWindowExW(0, L"STATIC", L"0/0", WS_CHILD,
                                 0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_COUNT, hInst_, nullptr);
    findPrev_ = CreateWindowExW(0, L"BUTTON", L"上一个", WS_CHILD | BS_PUSHBUTTON,
                                0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_PREV, hInst_, nullptr);
    findNext_ = CreateWindowExW(0, L"BUTTON", L"下一个", WS_CHILD | BS_PUSHBUTTON,
                                0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_FIND_NEXT, hInst_, nullptr);

    // 页 4：协议发送（§129：XMODEM/YMODEM 发送端；§132：页面 5 项界面态全量持久化）
    cmb_proto_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                 0, 0, 10, 200, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_MODE, hInst_, nullptr);
    SendMessageW(cmb_proto_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SendMessageW(cmb_proto_, CB_ADDSTRING, 0, (LPARAM)L"XMODEM");
    SendMessageW(cmb_proto_, CB_ADDSTRING, 0, (LPARAM)L"YMODEM");
    protoKindIdx_ = (cfg_.proto_kind >= 0 && cfg_.proto_kind <= 1) ? cfg_.proto_kind : 0;   // §132
    SendMessageW(cmb_proto_, CB_SETCURSEL, protoKindIdx_, 0);   // 隐藏态选中会丢，ShowExtPage 重设
    DisableComboWheel(cmb_proto_);                 // §56：输入类下拉禁滚轮
    // XMODEM 校验方式（§129 续修3 用户拍板）：自动 / CRC / 校验和——YMODEM 恒 CRC（选择无效，
    // 续修5：控件保持可用、仅不参与）；包大小续修5 起 YMODEM 也生效（数据块可 128）
    lbl_proto_ = CreateWindowExW(0, L"STATIC", L"协议：", WS_CHILD,
                                 0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_proto_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    lbl_xcheck_ = CreateWindowExW(0, L"STATIC", L"XMODEM 校验：", WS_CHILD,
                                  0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_xcheck_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    lbl_xblk_ = CreateWindowExW(0, L"STATIC", L"包大小：", WS_CHILD,
                                0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_xblk_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    cmb_xcheck_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                  0, 0, 10, 200, extWnd_, (HMENU)(INT_PTR)IDC_XCHECK_MODE, hInst_, nullptr);
    SendMessageW(cmb_xcheck_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SendMessageW(cmb_xcheck_, CB_ADDSTRING, 0, (LPARAM)L"自动");
    SendMessageW(cmb_xcheck_, CB_ADDSTRING, 0, (LPARAM)L"CRC");
    SendMessageW(cmb_xcheck_, CB_ADDSTRING, 0, (LPARAM)L"校验和");
    protoXCheckIdx_ = (cfg_.proto_xcheck >= 0 && cfg_.proto_xcheck <= 2) ? cfg_.proto_xcheck : 0;   // §132
    SendMessageW(cmb_xcheck_, CB_SETCURSEL, protoXCheckIdx_, 0);
    DisableComboWheel(cmb_xcheck_);
    // XMODEM 块大小（§129 续修4 用户拍板）：自动 / 1024 / 128——YMODEM 恒 1K 不参与
    cmb_xblk_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                0, 0, 10, 200, extWnd_, (HMENU)(INT_PTR)IDC_XBLK_MODE, hInst_, nullptr);
    SendMessageW(cmb_xblk_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SendMessageW(cmb_xblk_, CB_ADDSTRING, 0, (LPARAM)L"自动");
    SendMessageW(cmb_xblk_, CB_ADDSTRING, 0, (LPARAM)L"1024");
    SendMessageW(cmb_xblk_, CB_ADDSTRING, 0, (LPARAM)L"128");
    protoXBlkIdx_ = (cfg_.proto_xblk >= 0 && cfg_.proto_xblk <= 2) ? cfg_.proto_xblk : 0;   // §132
    SendMessageW(cmb_xblk_, CB_SETCURSEL, protoXBlkIdx_, 0);
    DisableComboWheel(cmb_xblk_);
    // §131：应答超时（用户拍板：可设置、默认 1000ms、X/Y 均生效）——会话启动时快照入
    // protoTimeoutTicks_；持久化 cfg_.proto_timeout_ms（100~60000 钳制）
    lbl_proto_timeout_ = CreateWindowExW(0, L"STATIC", L"超时时间：", WS_CHILD,
                                         0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_proto_timeout_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    edit_proto_timeout_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP,
                                          0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_TIMEOUT,
                                          hInst_, nullptr);
    SendMessageW(edit_proto_timeout_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SetWindowTextW(edit_proto_timeout_, std::to_wstring(cfg_.proto_timeout_ms).c_str());
    lbl_proto_timeout_ms_ = CreateWindowExW(0, L"STATIC", L"ms", WS_CHILD,
                                            0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_proto_timeout_ms_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    // §131 续：超时次数（用户拍板：与超时时间配套）——重发上限，默认 10，X/Y 均生效；
    // 统一作用于数据块/块 0/EOT 三处超时重发；持久化 cfg_.proto_retries（1~100 钳制）
    lbl_proto_retries_ = CreateWindowExW(0, L"STATIC", L"超时次数：", WS_CHILD,
                                         0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_proto_retries_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    edit_proto_retries_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP,
                                          0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_RETRIES,
                                          hInst_, nullptr);
    SendMessageW(edit_proto_retries_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SetWindowTextW(edit_proto_retries_, std::to_wstring(cfg_.proto_retries).c_str());
    lbl_proto_retries_c_ = CreateWindowExW(0, L"STATIC", L"次", WS_CHILD,
                                           0, 0, 10, 10, extWnd_, nullptr, hInst_, nullptr);
    SendMessageW(lbl_proto_retries_c_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    // 续修5：勾选=会话期收发内容（对端控制流 + 发出的块）入显示窗口；不勾=原隔离口径
    chk_proto_show_ = CreateWindowExW(0, L"BUTTON", L"将收发内容显示到窗口",
                                      WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
                                      0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_SHOW, hInst_, nullptr);
    SendMessageW(chk_proto_show_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    protoShow_ = cfg_.proto_show;                  // §132：勾选态恢复（不自动重放任何内容）
    SendMessageW(chk_proto_show_, BM_SETCHECK, protoShow_ ? BST_CHECKED : BST_UNCHECKED, 0);
    protoFileEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                     0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_FILE, hInst_, nullptr);
    SendMessageW(protoFileEdit_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    SendMessageW(protoFileEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"协议发送文件路径");
    if (!cfg_.proto_file.empty())                  // §132：文件路径恢复（不存在则开始时提示，同手输）
        SetWindowTextW(protoFileEdit_, cfg_.proto_file.c_str());
    protoOpenBtn_ = CreateWindowExW(0, L"BUTTON", L"打开", WS_CHILD | BS_PUSHBUTTON,
                                    0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_OPEN, hInst_, nullptr);
    SendMessageW(protoOpenBtn_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    protoStartBtn_ = CreateWindowExW(0, L"BUTTON", L"开始发送", WS_CHILD | BS_PUSHBUTTON,
                                     0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_START, hInst_, nullptr);
    SendMessageW(protoStartBtn_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    protoCancelBtn_ = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | BS_PUSHBUTTON,
                                      0, 0, 10, 10, extWnd_, (HMENU)(INT_PTR)IDC_PROTO_CANCEL, hInst_, nullptr);
    SendMessageW(protoCancelBtn_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    protoStatus_ = CreateWindowExW(0, L"STATIC", L"选择文件后「开始发送」；传输由对端启动字符驱动",
                                   WS_CHILD, 0, 0, 10, 10, extWnd_,
                                   (HMENU)(INT_PTR)IDC_PROTO_STATUS, hInst_, nullptr);
    SendMessageW(protoStatus_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    EnableWindow(protoCancelBtn_, FALSE);          // 会话期才可取消（状态机驱动）

    ShowWindow(extTab_, SW_HIDE);                  // 初始隐藏（点「扩展」弹出）
    ShowWindow(extWnd_, SW_HIDE);
}

void MainWindow::FillAsciiList() {
    static const wchar_t* kAbbrs[33] = {
        L"NUL", L"SOH", L"STX", L"ETX", L"EOT", L"ENQ", L"ACK", L"BEL",
        L"BS", L"HT", L"LF", L"VT", L"FF", L"CR", L"SO", L"SI",
        L"DLE", L"DC1", L"DC2", L"DC3", L"DC4", L"NAK", L"SYN", L"ETB",
        L"CAN", L"EM", L"SUB", L"ESC", L"FS", L"GS", L"RS", L"US", L"SP" };
    static const wchar_t* kDescs[33] = {
        L"空字符", L"标题开始", L"正文开始", L"结束传输", L"传输结束", L"询问",
        L"确认", L"响铃", L"退格", L"水平制表符", L"换行", L"垂直制表符", L"换页",
        L"回车", L"移出", L"移入", L"数据链路转义", L"设备控制1", L"设备控制2",
        L"设备控制3", L"设备控制4", L"否定确认", L"同步空闲", L"传输块结束",
        L"取消", L"介质结束", L"替换", L"转义", L"文件分隔符", L"组分隔符",
        L"记录分隔符", L"单元分隔符", L"空格" };
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    for (int d = 0; d < 128; ++d) {
        wchar_t dec[8], hex[8], bin[12], ch[4], tail[64];
        swprintf(dec, 8, L"%d", d);
        swprintf(hex, 8, L"0x%02X", d);
        {   // 二进制无前导零（修复：原 %08lld 是十进制补零，非二进制）
            wchar_t tmp[16];
            int n = 0;
            unsigned v = (unsigned)d;
            if (v == 0) tmp[n++] = L'0';
            while (v) { tmp[n++] = (wchar_t)(L'0' + (v & 1)); v >>= 1; }
            int k = 0;
            while (n > 0) bin[k++] = tmp[--n];
            bin[k] = 0;
        }
        bool printable = d >= 32 && d <= 126;
        if (printable) swprintf(ch, 4, L"%lc", (wchar_t)d);
        else ch[0] = 0;
        const wchar_t* ab = d <= 32 ? kAbbrs[d] : (d == 127 ? L"DEL" : ch);
        const wchar_t* ds;
        if (d <= 32) ds = kDescs[d];
        else if (d == 127) ds = L"删除";
        else if (d >= 48 && d <= 57) ds = L"数字";
        else if (d >= 65 && d <= 90) ds = L"大写字母";
        else if (d >= 97 && d <= 122) ds = L"小写字母";
        else ds = L"标点符号";
        swprintf(tail, 64, L"%ls%ls%ls", ab, d <= 32 || d == 127 ? L"｜" : L"", ds);
        it.iItem = d;
        it.iSubItem = 0;                            // 修复：同 MsSyncList——上轮 SETITEM 残留 iSubItem=4
        it.pszText = dec;                           // 会让下一行 INSERT 被拒（表只余 1 行）
        SendMessageW(asciiList_, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.iSubItem = 1; it.pszText = hex;
        SendMessageW(asciiList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 2; it.pszText = bin;
        SendMessageW(asciiList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 3; it.pszText = ch;
        SendMessageW(asciiList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 4; it.pszText = tail;
        SendMessageW(asciiList_, LVM_SETITEMW, 0, (LPARAM)&it);
    }
}

// 扩展小窗页显隐：页签 + 当前页控件（原先只在页签切换消息里做过一次，且打开时
// 从不调用 -> 页签与三页控件自创建起一直 SW_HIDE，窗口打开后全空白；
// 菜单路径 post 的 WM_APP+0x111 也没有处理者。审查 P2-1）
// §90（用户报：查找页复选框/计数、进制转换标签“文字有阴影”）：
// 扩展窗页签显示区由页签控件绘制（浅色 249），而 STATIC/复选框控件默认背景是系统
// COLOR_BTNFACE（灰 240）——灰底矩形在浅色页面上就是用户看到的“阴影”。
// 修=实测一次页签显示区底色建刷，静态/按钮背景一律用页底色（两者同源即无灰差）。
void MainWindow::ShowExtPage(int sel) {
    if (!extWnd_ || !extTab_) return;
    if (sel < 0 || sel > 4) sel = 0;
    RECT cr{};
    GetClientRect(extWnd_, &cr);
    if (cr.right > 0 && cr.bottom > 0)
        SetWindowPos(extTab_, nullptr, 0, 0, cr.right, cr.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(extTab_, SW_SHOW);
    ShowWindow(msList_,   sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(msAddBtn_, sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(msDelBtn_, sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(msImpBtn_, sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(msLoopBtn_, sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(msEdit_, SW_HIDE);
    for (int i = 0; i < 10; ++i) {
        if (calcLbls_[i]) ShowWindow(calcLbls_[i], sel == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(calcEdits_[i], sel == 1 ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(calcCarr_, sel == 1 ? SW_SHOW : SW_HIDE);
    if (calcCarrLbl_) ShowWindow(calcCarrLbl_, sel == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(asciiList_, sel == 2 ? SW_SHOW : SW_HIDE);
    // §75：查找页（迁入本窗）——无条件同步显隐（F1 教训），并维护 findOpen_
    // （= 本页激活 且 扩展窗可见；FindRescan/FindJump 的守卫沿用）
    for (HWND fc : {findEdit_, findCase_, findHex_, findCount_, findPrev_, findNext_})
        if (fc) ShowWindow(fc, sel == 3 ? SW_SHOW : SW_HIDE);
    findOpen_ = (sel == 3) && extOpen_;
    if (!findOpen_) KillTimer(hwnd_, 8);
    // §129 协议发送页（第 5 页）：显隐同步（F1 教训=无条件同步）；显形时重设协议选中
    // （隐藏态 CB_SETCURSEL 会丢——实测创建期设置后闭合框无文本，运行期可见态可靠）
    for (HWND pc : {cmb_proto_, cmb_xcheck_, cmb_xblk_, protoFileEdit_, protoOpenBtn_,
                    protoStartBtn_, protoCancelBtn_, protoStatus_, chk_proto_show_,
                    edit_proto_timeout_, edit_proto_retries_})
        if (pc) ShowWindow(pc, sel == 4 ? SW_SHOW : SW_HIDE);
    for (HWND lc : {lbl_proto_, lbl_xcheck_, lbl_xblk_, lbl_proto_timeout_, lbl_proto_timeout_ms_,
                    lbl_proto_retries_, lbl_proto_retries_c_})
        if (lc) ShowWindow(lc, sel == 4 ? SW_SHOW : SW_HIDE);
    if (sel == 4 && cmb_proto_) {
        SendMessageW(cmb_proto_, CB_SETCURSEL, protoKindIdx_, 0);
        SendMessageW(cmb_xcheck_, CB_SETCURSEL, protoXCheckIdx_, 0);
        SendMessageW(cmb_xblk_, CB_SETCURSEL, protoXBlkIdx_, 0);
        // 续修6：YMODEM 恒 CRC → 校验框置灰（用户复报「没变灰」=要禁用态，非仅忽略）；
        // 包大小 YMODEM 也生效（续修5）→ 保持可用。会话期两框皆禁（会话开始时已禁）。
        EnableWindow(cmb_xcheck_, !protoActive_ && protoKindIdx_ == 0);
        EnableWindow(cmb_xblk_, !protoActive_);
        EnableWindow(edit_proto_timeout_, !protoActive_);   // §131：会话期禁改（快照已取）
        EnableWindow(edit_proto_retries_, !protoActive_);   // §131 续：同
        if (chk_proto_show_)
            SendMessageW(chk_proto_show_, BM_SETCHECK, protoShow_ ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    RECT rc{};
    GetClientRect(extTab_, &rc);
    SendMessageW(extTab_, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
    LayoutExtPage(rc.left + 4, rc.top + 4, rc.right - rc.left - 8, rc.bottom - rc.top - 8);
    // §94：页签切换路径在此之后无几何变化，本调用即最终几何步骤——对刚显形的列表
    // 补一次"列表+表头"整窗置脏（打开路径随后 ToggleExt 末尾还会再做一次，幂等）。
    if (IsWindowVisible(extWnd_)) {
        ForceListPaint(msList_);
        ForceListPaint(asciiList_);
        // §129 续修：协议组合框与列表同受"父窗擦除覆盖子窗"机制影响（用户报：首开页面
        // 选择框不见）——立即置脏 + timer14 晚一拍再置一次（§96 双保险同款）。
        if (sel == 4)                              // §129 续修2/4：组合框立即置脏（§131 超时/次数）
            for (HWND cb : {cmb_proto_, cmb_xcheck_, cmb_xblk_, edit_proto_timeout_,
                            edit_proto_retries_})
                if (cb) InvalidateRect(cb, nullptr, TRUE);
        SetTimer(hwnd_, 14, 60, nullptr);          // §96：显开/切页后晚一拍补脏
    }
}

void MainWindow::ForceListPaint(HWND list) {
    // §96（本轮修复）：只“置脏”，**绝不在这里立即刷屏**。
    //
    // 机制：列表与表头是 SNComExt 的子窗，而 SNComExt 的类样式是 CS_HREDRAW|CS_VREDRAW
    // 且创建样式**没有 WS_CLIPCHILDREN**（§95 试过加、用户实测效果更差已当日回滚，§95.6）。
    // 于是每次“显示/改尺寸”父窗都会把整个客户区（**包括子窗矩形**）重刷成背景色；
    // 随后只有“仍带着失效区”的子窗会被重画（子窗绘制发生在父窗擦除之后）。
    // 而 SysListView32/SysHeader32 的类样式不含 CS_HREDRAW（八轮实测 GCL_STYLE=0x4008）→ 尺寸变化只标脏
    // “新暴露条带”；多条表表头全程只有一处确定失效（MsStretchLastColumn 的末列改宽）
    // → 就只剩「注释」一列有内容；ASCII 表表头连一处都没有 → 整条空白。
    //
    // 为何前五轮（§87 整表 UPDATENOW / §91 窄域补画 / §92 尺寸往返 / §94 同步上屏）均无效：
    // 它们都在“父窗待处理的擦除”之前就把表头画了出去（UpdateWindow/UPDATENOW），子窗一旦被验证
    // 就不再有失效区；紧接着父窗擦除把它盖掉，于是“画了也白画”。正确做法：
    // **只置脏、不立即刷**，交给正常绘制周期（父窗先擦、子窗后画）完成整窗重画。
    if (!list || !IsWindowVisible(list)) return;
    InvalidateRect(list, nullptr, TRUE);                      // 列表本体整窗（含表头带下方背景）
    HWND hdr = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    if (hdr) InvalidateRect(hdr, nullptr, TRUE);              // ★ 表头子窗整窗
    // 注意：**不要**在此处 UpdateWindow/RedrawWindow(UPDATENOW)——那会把子窗提前
    // 画在父窗擦除之前，重踩 §87/§91/§92/§94 五轮失败的老路。
}

void MainWindow::ToggleExt(bool on) {
    if (extOpen_ == on) return;
    btn_expand_ && SendMessageW(btn_expand_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(btn_expand_, on ? L"收起" : L"扩展");   // ← PyCom：展开后按钮变「收起」
    extOpen_ = on;
    if (on) {
        RepositionExt();                           // 先几何后显示（← PyCom，防闪现旧位置）
        ShowWindow(extWnd_, SW_SHOWNA);
        ShowExtPage((int)SendMessageW(extTab_, TCM_GETCURSEL, 0, 0));   // 页签+当前页控件显隐（审查 P2-1）
        RepositionExt();                           // 显示后一拍收口（三十轮 F2）
        // §94（取代 §92 的 1px 尺寸往返）：八轮实测表头宽度 444→445→444 净变化为 0，
        // 按"无 CS_HREDRAW 只失效新暴露条带"的语义它对表头失效无贡献，却让每次开窗
        // 多跑两轮完整 WM_SIZE→LayoutExtPage 重排。显示序列的【最后一步】显式置脏：
        // 此时几何已最终确定，表头第一次有机会被要求整体重画（§87 整表 UPDATENOW、
        // §91 窄域补画、§92 尺寸往返三次失效的演进与机制见 ForceListPaint 注释）。
        ForceListPaint(msList_);
        ForceListPaint(asciiList_);
        // §96：晚一拍（WM_TIMER 低于 WM_PAINT 优先级）再补一次失效，
        // 保证不会因后续的父窗擦除而再次丢失。只置脏，不刷屏。
        SetTimer(hwnd_, 14, 60, nullptr);
    } else {
        ShowWindow(extWnd_, SW_HIDE);
    }
}

void MainWindow::RepositionExt() {
    // 高度语义（用户拍板 §52）：只在【打开的那一刻】取主窗当前全高作为快照；
    // 打开期间主窗改尺寸不跟随（本函数也只在 ToggleExt(true) 路径被调）。
    if (!extWnd_) return;
    RECT fr{};
    GetWindowRect(hwnd_, &fr);
    HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);                     // rcWork：排除任务栏（D5 验收 4/5）
    int w = ScalePx(480, dpi_), h = fr.bottom - fr.top;   // 480：默认宽度（用户反馈）
    int x = fr.right + 1, y = fr.top;              // 贴右缘、顶对齐（验收 1）
    if (x + w > mi.rcWork.right) x = fr.left - w - 1;   // 右侧放不下 → 改贴左缘
    if (x < mi.rcWork.left) x = mi.rcWork.left;         // 仍放不下 → 夹回屏内
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;   // y 双侧夹取（验收 5）
    SetWindowPos(extWnd_, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

static int CalcFieldIdToIdx(int id) { return id - IDC_CALC_DEC; }

void MainWindow::CalcRecompute(int src) {
    // ← PyCom CalculatorWidget 语义 + §97 升级：任意精度无符号整数（≥256 位；上限 65536 位
    // = kBigMaxBytes 8192 字节），小端字节序 BigBytes；float 字段=位模式（取低 4/8 字节解释，
    // 超宽时按截断语义，与原 64 位实现连续）；解析失败/负数/超限不联动。
    if (calcLock_) return;
    int len = GetWindowTextLengthW(calcEdits_[src]);
    std::wstring t((size_t)(len > 0 ? len : 0) + 1, L'\0');
    int got = GetWindowTextW(calcEdits_[src], t.data(), len + 1);
    t.resize(got > 0 ? (size_t)got : 0);
    BigBytes bytes;
    bool ok = false;
    switch (src) {
    case 0: ok = BigParseDec(t, bytes); break;               // DEC（纯十进制数字）
    case 1: {                                                // HEX（容忍空格；数值语义，奇数左补 0）
        std::wstring s;
        for (wchar_t c : t) if (c != L' ') s += c;
        ok = BigParseHexNum(s, bytes);
        break;
    }
    case 2: case 3: {                                        // HEX 小端/大端（字节流语义，§54 成对）
        std::vector<uint8_t> b;
        if (ParseHex(t, b) && !b.empty()) {
            if (src == 2) bytes.assign(b.begin(), b.end());   // LE：输入序=低字节在前
            else bytes.assign(b.rbegin(), b.rend());          // BE：反转成小端存储
            ok = true;
        }
        break;
    }
    case 4: {                                                // BIN（容忍空格）
        std::wstring s;
        for (wchar_t c : t) if (c != L' ') s += c;
        ok = BigParseBin(s, bytes);
        break;
    }
    case 5: ok = BigParseLatin1BE(t, bytes); break;          // ASCII（latin-1 大端）
    case 6: case 7: {                                        // float32 LE/BE 输入 → 位模式
        // 输入侧两字段同义（原实现如此）：位模式按小端内存序存 bytes
        float f = (float)wcstod(t.c_str(), nullptr);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        bytes.assign(4, 0);
        memcpy(bytes.data(), &bits, 4);
        ok = true;
        break;
    }
    case 8: case 9: {                                        // float64 LE/BE 输入 → 位模式
        double dv = wcstod(t.c_str(), nullptr);
        uint64_t bits;
        memcpy(&bits, &dv, 8);
        bytes.assign(8, 0);
        memcpy(bytes.data(), &bits, 8);
        ok = true;
        break;
    }
    }
    if (!ok) return;
    BigTrim(bytes);
    calcLock_ = true;
    for (int i = 0; i < 10; ++i) {
        if (i == src) continue;
        std::wstring s;
        wchar_t out[64];
        switch (i) {
        case 0: s = BigToDec(bytes); break;
        case 1: s = BigToHexUpper(bytes); break;
        case 2: s = BigToHexLE(bytes); break;
        case 3: s = BigToHexBE(bytes); break;
        case 4: s = BigToBin(bytes); break;
        case 5: s = BigToLatin1(bytes); break;
        case 6: { uint8_t b4[4]; BigLowBytes(bytes, 4, b4);
                  float f; memcpy(&f, b4, 4); swprintf(out, 64, L"%.9g", f); s = out; break; }
        case 7: { uint8_t b4[4]; BigLowBytes(bytes, 4, b4); std::reverse(b4, b4 + 4);
                  float f; memcpy(&f, b4, 4); swprintf(out, 64, L"%.9g", f); s = out; break; }
        case 8: { uint8_t b8[8]; BigLowBytes(bytes, 8, b8);
                  double dv; memcpy(&dv, b8, 8); swprintf(out, 64, L"%.17g", dv); s = out; break; }
        case 9: { uint8_t b8[8]; BigLowBytes(bytes, 8, b8); std::reverse(b8, b8 + 8);
                  double dv; memcpy(&dv, b8, 8); swprintf(out, 64, L"%.17g", dv); s = out; break; }
        }
        SetWindowTextW(calcEdits_[i], s.c_str());
    }
    SetWindowTextW(calcCarr_, BigToCArray(bytes).c_str());   // C 语言数组（小端内存序 ← PyCom）
    calcLock_ = false;
}

LRESULT CALLBACK MainWindow::ExtProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->HandleExt(h, msg, wp, lp);
}

LRESULT MainWindow::HandleExt(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        // §101/§103 定稿：底色=COLOR_BTNFACE（页面显示区即父窗 BTNFACE，同源无灰差）。
        // §103 真根因（scanline 实证）：static 先用返回刷填满控件（灰），再用 **DC 当前背景色**
        // 不透明地画字形单元——此前只返回刷、从不 SetBkColor，DC 默认背景=白 → 每个标签
        // 文字背后出现**紧贴字形的白块**（§90→§98h 四轮"标签白"的真身；主窗引脚框 §P2-2
        // 早就是"SetBkColor+返回刷"的正确姿势）。修=两色同设 240。
        SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_SIZE: {
        // 布局：页签全区；各页随页签显示区排布
        int w = LOWORD(lp), hh = HIWORD(lp);
        // Must actually resize the tab: WM_SIZE is a notification, sending it to a
        // child does not move it -> the tab stayed 10x10 and every page layout
        // collapsed to zero size (ext window looked blank).
        SetWindowPos(extTab_, nullptr, 0, 0, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc{};
        GetClientRect(extTab_, &rc);
        SendMessageW(extTab_, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
        int x = rc.left + 4, y = rc.top + 4;
        int cw = rc.right - rc.left - 8, ch = rc.bottom - rc.top - 8;
        LayoutExtPage(x, y, cw, ch);
        // §92/§93 注意：本函数在扩展窗显示序列里会被**连调多次**（ToggleExt 先后两次
        // RepositionExt 各触发一轮 WM_SIZE）——勿在其中加入昂贵逻辑或非幂等操作。
        return 0;
    }
    case WM_NOTIFY: {
        auto* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_EXT_TAB && nm->code == TCN_SELCHANGE)
            ShowExtPage((int)SendMessageW(extTab_, TCM_GETCURSEL, 0, 0));
        if ((nm->code == HDN_BEGINTRACKW || nm->code == HDN_BEGINTRACKA)) {
            // 多条表列宽：点击发送/HEX 两列固定（用户反馈——按钮与勾选框列不允许拖动）。
            // 注意：HDN_* 由 LISTVIEW 内置表头发出，经 LISTVIEW 转发到本窗时
            // idFrom = 表头控件 ID（0），而非 LISTVIEW 的 20——首版误用 20 致拦截失效。
            // 二版误伤：扩展窗 ASCII 表的表头 ID 同样是 0，按 idFrom 判把它的
            // 列也锁死了——最终按 hwndFrom 精确匹配多条表自己的表头才正确。
            auto* hd = (NMHEADERW*)nm;
            if (nm->hwndFrom == ListView_GetHeader(msList_)
                && (hd->iItem == 0 || hd->iItem == 1)) return TRUE;
        }
        if ((nm->code == HDN_ENDTRACKW || nm->code == HDN_ENDTRACKA)
            && nm->hwndFrom == ListView_GetHeader(msList_)) {
            MsStretchLastColumn();        // §78：用户拖完列宽后末列重新填满右缘
        }
        HandleExtNotify(nm);
        if (nm->idFrom == 20 && nm->code == NM_CUSTOMDRAW) {   // 多条表 HEX 列画勾选框
            auto* cd = (NMLVCUSTOMDRAW*)nm;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                return CDRF_NOTIFYSUBITEMDRAW;
            default:
                if ((cd->nmcd.dwDrawStage & CDDS_SUBITEM) && cd->iSubItem == 1) {
                    // HEX 列：标准勾选框（SSCOM 对齐）
                    bool hex = cd->nmcd.dwItemSpec < msRows_.size()
                               && msRows_[cd->nmcd.dwItemSpec].hex;
                    RECT rc = cd->nmcd.rc;
                    int box = ScalePx(13, dpi_);
                    RECT br { rc.left + (rc.right - rc.left - box) / 2,
                              rc.top + (rc.bottom - rc.top - box) / 2,
                              rc.left + (rc.right - rc.left + box) / 2,
                              rc.top + (rc.bottom - rc.top + box) / 2 };
                    DrawFrameControl(cd->nmcd.hdc, &br, DFC_BUTTON,
                                     DFCS_BUTTONCHECK | DFCS_FLAT | (hex ? DFCS_CHECKED : 0));
                    return CDRF_SKIPDEFAULT;
                }
                if ((cd->nmcd.dwDrawStage & CDDS_SUBITEM) && cd->iSubItem == 0) {
                    // 点击发送列：立体按钮外观（每行一个按钮控件），文字恒为「发 送」
                    //（§141：用户拍板——不再随注释列变化）；刚点击的行短暂画凹陷（按压视觉反馈）
                    bool pressed = (msPressRow_ == (int)cd->nmcd.dwItemSpec);
                    RECT rc = cd->nmcd.rc;
                    InflateRect(&rc, -1, -1);
                    DrawEdge(cd->nmcd.hdc, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED,
                             BF_RECT | BF_MIDDLE | BF_ADJUST);
                    SetBkMode(cd->nmcd.hdc, TRANSPARENT);
                    SetTextColor(cd->nmcd.hdc, GetSysColor(COLOR_BTNTEXT));
                    DrawTextW(cd->nmcd.hdc, L"发 送", -1, &rc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    return CDRF_SKIPDEFAULT;
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        HandleExtCommand(id, code, (HWND)lp);
        if (id >= IDC_CALC_DEC && id <= IDC_CALC_F64BE && code == EN_CHANGE && !calcLock_)
            CalcRecompute(id - IDC_CALC_DEC);
        return 0;
    }
    case WM_APP + 0x111:                       // 菜单「小工具」路径的切页显隐（原先无处理者）
        ShowExtPage((int)wp);
        return 0;

    case WM_GETMINMAXINFO: {                   // 太小则三页布局崩（页签/表格/字段无空间）
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = ScalePx(320, dpi_);
        mmi->ptMinTrackSize.y = ScalePx(240, dpi_);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        PostMessageW(hwnd_, WM_APP + 0x110, 0, 0);     // 主窗「扩展」按钮弹起同步（D5 验收 7）
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------- 阶段 E：多条字符串发送（← PyCom multisend.py） ----------------

void MainWindow::MsAddRow() {
    if ((int)msRows_.size() >= 100) { Notify(L"最多 100 条"); return; }
    msRows_.push_back(MsRow());
    MsSyncList();
    MsSyncToCfg();                                  // §132：增行即时入配置
}

void MainWindow::MsDelRow() {
    int sel = (int)SendMessageW(msList_, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)msRows_.size()) return;
    msRows_.erase(msRows_.begin() + sel);
    MsSyncList();
    MsSyncToCfg();                                  // §132：删行即时入配置
}

void MainWindow::MsLoadFromCfg() {
    // §132：启动时从 cfg_.multisend_rows 恢复表格（键名与 PyCom 多条行对齐，docs/00 §132）
    msRows_.clear();
    if (!cfg_.multisend_rows || !cfg_.multisend_rows->isArray()) return;
    for (const JsonPtr& it : cfg_.multisend_rows->arr) {
        if ((int)msRows_.size() >= 100) break;      // 与 MsAddRow 上限一致（防手改配置超限）
        if (!it || !it->isObject()) continue;
        MsRow m;
        m.note     = it->S(L"note", L"");
        m.text     = it->S(L"text", L"");
        m.hex      = it->B(L"hex", false);
        m.order    = max(0, min(9999, (int)it->I(L"order", 0)));
        m.interval = max(0, min(3600000, (int)it->I(L"interval", 1000)));
        msRows_.push_back(m);
    }
}

void MainWindow::MsSyncToCfg() {
    // §132：表格 → cfg_.multisend_rows（行增删改/HEX 切换后即时同步；落盘随保存参数/关窗）
    JsonPtr a = JsonValue::Arr();
    for (const MsRow& m : msRows_) {
        JsonPtr o = JsonValue::Obj();
        o->Set(L"note", JsonValue::Str(m.note));
        o->Set(L"text", JsonValue::Str(m.text));
        o->Set(L"hex", JsonValue::Bool(m.hex));
        o->Set(L"order", JsonValue::Num(m.order));
        o->Set(L"interval", JsonValue::Num(m.interval));
        a->Push(o);
    }
    cfg_.multisend_rows = a;
}

void MainWindow::MsSyncList() {
    SendMessageW(msList_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(msList_, LVM_DELETEALLITEMS, 0, 0);
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    for (size_t r = 0; r < msRows_.size(); ++r) {
        const MsRow& m = msRows_[r];
        std::wstring c0 = m.note.empty() ? std::wstring(L"发送") : m.note;
        if (c0.size() > 63) c0.resize(63);          // 注释来自就地编辑/ini，超 63 会撑爆原 wchar_t c0[64]（审查 P2-4）
        std::wstring c5 = m.note.empty() ? std::wstring(L"点击修改注释") : m.note;   // §54：空注释占位文案
        if (c5.size() > 63) c5.resize(63);
        wchar_t c3[8], c4[12];
        swprintf(c3, 8, L"%d", m.order);
        swprintf(c4, 12, L"%d", m.interval);
        it.iItem = (int)r;
        it.iSubItem = 0;                            // 修复：上一轮 SETITEM 残留 iSubItem=4 会让
                                                    // 下一行 LVM_INSERTITEMW 被拒绝（表格永远只有 1 行）
        it.pszText = (LPWSTR)c0.c_str();
        SendMessageW(msList_, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.iSubItem = 1; it.pszText = (LPWSTR)L"";   // HEX 列：勾选框由 NM_CUSTOMDRAW 绘制
        SendMessageW(msList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 2; it.pszText = (LPWSTR)m.text.c_str();
        SendMessageW(msList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 3; it.pszText = c3;
        SendMessageW(msList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 4; it.pszText = c4;
        SendMessageW(msList_, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 5; it.pszText = (LPWSTR)c5.c_str();          // 注释列（空=占位「点击修改注释」，编辑入口在此列）
        SendMessageW(msList_, LVM_SETITEMW, 0, (LPARAM)&it);
    }
    SendMessageW(msList_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(msList_, nullptr, TRUE);
}

void MainWindow::MsEditCell(int row, int col) {
    if (row < 0 || row >= (int)msRows_.size()) return;
    msEditRow_ = row;
    msEditCol_ = col;
    RECT rc{};
    ListView_GetSubItemRect(msList_, row, col, LVIR_BOUNDS, &rc);
    // rc 是相对 msList_ 客户区的坐标；msEdit_ 挂在 extWnd_ 下——必须映射（用户反馈：
    // 编辑框直用原坐标 → 偏差列表原点，落到按钮行被盖住）
    MapWindowPoints(msList_, extWnd_, (LPPOINT)&rc, 2);
    int W = rc.right - rc.left;
    SetWindowPos(msEdit_, nullptr, rc.left, rc.top, W, rc.bottom - rc.top, SWP_NOZORDER | SWP_SHOWWINDOW);
    std::wstring cur;
    const MsRow& m = msRows_[row];
    if (col == 5) cur = m.note;
    else if (col == 2) cur = m.text;
    else if (col == 3) cur = std::to_wstring(m.order);
    else if (col == 4) cur = std::to_wstring(m.interval);
    SetWindowTextW(msEdit_, cur.c_str());
    SetFocus(msEdit_);
    SendMessageW(msEdit_, EM_SETSEL, 0, -1);
}

void MainWindow::MsSendRow(int idx) {
    if (idx < 0 || idx >= (int)msRows_.size()) return;
    const MsRow& m = msRows_[idx];
    if (m.text.empty()) return;
    std::vector<uint8_t> b;
    if (m.hex) {
        if (!ParseHex(m.text, b)) { Notify((L"第 " + std::to_wstring(idx + 1) + L" 条 HEX 格式无效").c_str()); return; }
    } else {
        if (!TextWithEscapesToBytes(m.text, cfg_.encoding, b)) return;   // §77：转义同发送框（SSCOM 多条同款）
    }
    SendBytes(b);                                   // 经统一管线（显示+计数+队列语义）
}

void MainWindow::MsLoopNext() {
    // ← PyCom MultiSendLoop._send_next：每轮按顺序号升序发 order>0 条目，等各自延时
    std::vector<int> seq;
    for (int i = 0; i < (int)msRows_.size(); ++i)
        if (msRows_[i].order > 0) seq.push_back(i);
    std::sort(seq.begin(), seq.end(), [&](int a, int b) { return msRows_[a].order < msRows_[b].order; });
    if (seq.empty()) {                              // 无可发条目：自停（勾选同步取消）
        msLoopActive_ = false;
        KillTimer(hwnd_, 11);
        SendMessageW(msLoopBtn_, BM_SETCHECK, BST_UNCHECKED, 0);
        cfg_.multisend_loop = false;                // §132：自停同步入配置
        Notify(L"循环发送停止（无可发送条目）");
        return;
    }
    if (!LinkIsOpen()) {                            // §132：断开静默等待（不排队不刷提示）
        SetTimer(hwnd_, 11, 200, nullptr);          // 恢复连接后自动续跑；勾选保持
        return;
    }
    int row = seq[msIdx_ % (int)seq.size()];
    msIdx_ = (msIdx_ + 1) % (int)seq.size();
    MsSendRow(row);
    int delay = max(1, min(3600000, msRows_[row].interval));
    SetTimer(hwnd_, 11, (UINT)delay, nullptr);
}

void MainWindow::MsImportIni() {
    // ← PyCom _parse_ms_rows：N(100+i)=顺序,注释,延时；Ni=H/A,数据；前导逗号剥离
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = extWnd_ ? extWnd_ : hwnd_;
    ofn.lpstrFilter = L"INI 文件 (*.ini)\0*.ini\0所有文件\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    // GBK 读取（原版 ini 为 ANSI）
    HANDLE hf = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { Notify(L"读取失败: 无法打开文件"); return; }
    std::string raw;
    char rbuf[8192];
    DWORD got = 0;
    while (ReadFile(hf, rbuf, sizeof(rbuf), &got, nullptr) && got) raw.append(rbuf, got);
    CloseHandle(hf);
    int wlen = MultiByteToWideChar(936, 0, raw.data(), (int)raw.size(), nullptr, 0);
    std::wstring text((size_t)max(wlen, 0), 0);
    if (wlen > 0) MultiByteToWideChar(936, 0, raw.data(), (int)raw.size(), &text[0], wlen);

    std::vector<std::pair<std::wstring, std::wstring>> keys;
    ParseIniKeys(text, keys);
    std::vector<MsIniRow> rows;
    ParseMsIniRows(keys, rows);                    // 解析核（core/msini，与 PyCom _parse_ms_rows 对拍）

    msRows_.clear();
    for (const MsIniRow& r : rows) {
        MsRow m;
        m.order = r.order; m.note = r.note; m.hex = r.hex; m.text = r.text; m.interval = r.interval;
        msRows_.push_back(m);
    }
    MsSyncList();
    MsSyncToCfg();                                 // §132：导入结果即时入配置
    Notify(msRows_.empty() ? L"未发现多条字符串配置"
                           : (L"多条字符串 " + std::to_wstring(msRows_.size()) + L" 条").c_str());
}

void MainWindow::MsApplyPageLayout(int x, int y, int cw, int ch) {
    // 多条页：工具按钮一行 + 表格占余
    int bh = ScalePx(24, dpi_), g = ScalePx(4, dpi_);
    int bw = (cw - 3 * g) / 4;
    SetWindowPos(msAddBtn_, nullptr, x, y, bw, bh, SWP_NOZORDER);
    SetWindowPos(msDelBtn_, nullptr, x + bw + g, y, bw, bh, SWP_NOZORDER);
    SetWindowPos(msImpBtn_, nullptr, x + 2 * (bw + g), y, bw, bh, SWP_NOZORDER);
    SetWindowPos(msLoopBtn_, nullptr, x + 3 * (bw + g), y, bw, bh, SWP_NOZORDER);
    SetWindowPos(msList_, nullptr, x, y + bh + g, cw, ch - bh - g, SWP_NOZORDER);
    MsStretchLastColumn();                 // §78：末列拉伸填满（否则右缘留一片"空列"）
    // 就地编辑浮在单元格上（MsEditCell 定位）
}

// §78（用户报「最右边多了一列、完全没用」）：LVS_REPORT 的前 5 列定宽后，剩余宽度在
// 表头里表现为一截空白列。修法=末列（注释）吸收剩余宽度；用户拖动其他列后（HDN_ENDTRACK）
// 与扩展窗尺寸变化（本函数）都重算一次，保证右缘始终贴齐。
void MainWindow::MsStretchLastColumn() {
    if (!msList_) return;
    RECT rc{};
    GetClientRect(msList_, &rc);
    int used = 2;                                   // 表格边框
    for (int i = 0; i < 5; ++i) {
        // 取**当前**列宽（用户拖动后与设计值不同）；读失败才回设计值
        int w = (int)SendMessageW(msList_, LVM_GETCOLUMNWIDTH, i, 0);
        used += w > 0 ? w : ScalePx(kMsColW[i], dpi_);
    }
    int last = rc.right - used;
    int minW = ScalePx(96, dpi_);                   // 注释列下限（占位文本 6 汉字 ~96 设计 px）
    if (last < minW) last = minW;
    SendMessageW(msList_, LVM_SETCOLUMNWIDTH, 5, MAKELPARAM(last, 0));
}

void MainWindow::TxFileTick() {
    // ← PyCom txfile.FileSender._send_chunk：256B 流式；余量未清空前不读新块；
    // 部分接受留 txFilePend_ 下 tick 续发（G2-3，对齐 FileSender._pend——背压下不丢）。
    // PyCom 连续档 = QTimer(0)（每轮事件循环发一块）；Win32 WM_TIMER 最小分辨率
    // ~15.6ms，1ms 定时只跑出 ~16KB/s——连续档改每 tick 连发至多 64KB，延时档维持
    // 每 tick 一块（速度由延时档位决定）。
    if (!LinkIsOpen()) {
        KillTimer(hwnd_, 9);
        if (txFile_ != INVALID_HANDLE_VALUE) { CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE; }
        txFilePend_.clear();
        Notify(L"发送文件中止：连接已断开");
        SendMessageW(progress_, PBM_SETPOS, 0, 0);
        return;
    }
    int budget = (cfg_.file_delay_ms > 0) ? 256 : 64 * 1024;   // 本 tick 发送预算（字节）
    while (budget > 0) {
        if (txFilePend_.empty()) {                  // 余量清空后才读下一块（PyCom 同序）
            uint8_t chunk[256];
            DWORD got = 0;
            if (!ReadFile(txFile_, chunk, sizeof(chunk), &got, nullptr)) {
                KillTimer(hwnd_, 9); CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE;
                Notify(L"发送文件中止：读取失败");
                return;
            }
            if (got == 0) {                         // 完成
                KillTimer(hwnd_, 9);
                CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE;
                SendMessageW(progress_, PBM_SETPOS, (WPARAM)txFileSize_, 0);
                Notify(L"发送文件完成");
                return;
            }
            txFilePend_.assign(chunk, chunk + got);
        }
        int n = LinkWrite(txFilePend_.data(), txFilePend_.size());   // 直接写入（回显见下 §101）
        if (n <= 0) {
            // §100/§102：0=链路本轮拒收。串口恒为背压 → 暂停续发；网络侧「余量满」（非
            // UDP 错误窗口）同为背压 → 也暂停续发（§102 起两路口径统一）；仅「UDP 错误等
            // 真错误」窗口内维持中止提示——真错误重试无意义，须显式中止让用户知情。
            if (cfg_.mode == L"serial" || !net_.SendErrActive()) {
                if (!txFileStalled_) {
                    txFileStalled_ = true;
                    Notify(L"发送文件暂停：链路写入排队中（按线速继续）");
                }
                return;
            }
            KillTimer(hwnd_, 9); CloseHandle(txFile_); txFile_ = INVALID_HANDLE_VALUE;
            txFilePend_.clear();
            Notify(L"发送文件中止：写入失败（对端未接收）");
            SendMessageW(progress_, PBM_SETPOS, 0, 0);
            return;
        }
        txFileStalled_ = false;                     // 恢复写入即清（进度走动即恢复反馈）
        // §101（用户拍板）：文件发送内容**回显**到接收区——已接受部分按发送方向入显示管线
        //（与常规发送同语义：时间戳/方向标记仅包首行、HEX/折行按当前显示模式），
        // 否则「发了什么」无处可查；旧口径「不回显」即用户报的"发送的文件数据没有显示"
        newData_ = true;
        RequestFastDisplay();                      // R26-P3-1：长空闲后文件发送首屏回显即时（不入慢拍）
        builder_.feed(true, txFilePend_.data(), n, NowSeconds(),
                      buf_.end_abs() - (txFilePend_.size() - n));
        if (n < (int)txFilePend_.size())
            txFilePend_.erase(txFilePend_.begin(), txFilePend_.begin() + n);   // 余量留队首
        else
            txFilePend_.clear();
        txBytes_ += (unsigned)n;
        txFilePos_ += (unsigned long long)n;
        budget -= n;
        if (cfg_.file_delay_ms > 0) break;          // 延时档：一块即走
    }
    SendMessageW(progress_, PBM_SETPOS, (WPARAM)min(txFilePos_, txFileSize_), 0);
}


void MainWindow::SendRawNoEcho(const std::vector<uint8_t>& b) {
    if (b.empty()) return;
    if (LinkIsOpen()) {
        int n = LinkWrite(b.data(), b.size());
        if (n > 0) txBytes_ += (unsigned)n;
    }
}

void MainWindow::LayoutExtPage(int x, int y, int cw, int ch) {
    // 页 0 多条：工具按钮行 + 表格；页 1 转换：10 行 + C 数组；页 2 ASCII：占满
    MsApplyPageLayout(x, y, cw, ch);
    int rowH = ScalePx(24, dpi_);
    int lblW = ScalePx(96, dpi_);
    for (int i = 0; i < 10; ++i) {
        if (calcLbls_[i])
            SetWindowPos(calcLbls_[i], nullptr, x, y + i * rowH + 2, lblW, rowH - 4, SWP_NOZORDER);
        SetWindowPos(calcEdits_[i], nullptr, x + lblW + 4, y + i * rowH,
                     cw - lblW - 8, rowH - 2, SWP_NOZORDER);
    }
    if (calcCarrLbl_)
        SetWindowPos(calcCarrLbl_, nullptr, x, y + 10 * rowH + 2, lblW, rowH - 4, SWP_NOZORDER);
    SetWindowPos(calcCarr_, nullptr, x, y + 11 * rowH + 2,
                 cw, ch - 11 * rowH - 4, SWP_NOZORDER);
    SetWindowPos(asciiList_, nullptr, x, y, cw, ch, SWP_NOZORDER);
    // 页 3 查找（§75）：两行——[编辑 伸展 | 上一个 | 下一个] / [区分大小写 | HEX | 计数 | ×]
    {
        int bW = ScalePx(64, dpi_), bH = ScalePx(24, dpi_), g = ScalePx(4, dpi_);
        int eW = cw - 2 * bW - 2 * g;
        if (eW < ScalePx(80, dpi_)) eW = ScalePx(80, dpi_);
        SetWindowPos(findEdit_, nullptr, x, y, eW, bH, SWP_NOZORDER);
        SetWindowPos(findPrev_, nullptr, x + eW + g, y, bW, bH, SWP_NOZORDER);
        SetWindowPos(findNext_, nullptr, x + eW + bW + 2 * g, y, bW, bH, SWP_NOZORDER);
        int y3 = y + bH + g * 2;
        int x3 = x;
        int wCase = ChkW(L"区分大小写");
        SetWindowPos(findCase_, nullptr, x3, y3 + 2, wCase, bH, SWP_NOZORDER);
        x3 += wCase + g * 2;
        int wHex = ChkW(L"HEX");
        SetWindowPos(findHex_, nullptr, x3, y3 + 2, wHex, bH, SWP_NOZORDER);
        x3 += wHex + g * 2;
        int cwRest = x + cw - x3;                   // §89：× 移除后计数用满剩余宽度
        SetWindowPos(findCount_, nullptr, x3, y3 + 6, cwRest > 40 ? cwRest : 40, ScalePx(18, dpi_), SWP_NOZORDER);
    }
    // 页 4 协议发送（§129 续修4）：每个选择框独占一行+前置说明（用户拍板）——
    // [协议：combo 220] / [XMODEM 校验：combo 130] / [XMODEM 包大小：combo 130] /
    // [超时时间：edit+ms]（§131） / [路径 伸展 | 打开] / [开始 | 取消 | 状态 伸展]。
    // 组合框隐藏态下 CB_SETCURSEL 会丢（实测）→ 页显形时由 ShowExtPage 重设（运行期可见态
    // 设置可靠，verify_r129 A1 实证）。
    {
        int bW = ScalePx(64, dpi_), bH = ScalePx(24, dpi_), g = ScalePx(4, dpi_);
        int comboH = ScalePx(kComboDropH, dpi_);
        int lblW = ScalePx(120, dpi_);
        int cW = ScalePx(220, dpi_);
        int xW = ScalePx(130, dpi_);
        int rowH = bH + g * 2;
        SetWindowPos(lbl_proto_, nullptr, x, y + 3, lblW, bH, SWP_NOZORDER);
        SetWindowPos(cmb_proto_, nullptr, x + lblW + g, y, cW, comboH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        int y2 = y + rowH;
        SetWindowPos(lbl_xcheck_, nullptr, x, y2 + 3, lblW, bH, SWP_NOZORDER);
        SetWindowPos(cmb_xcheck_, nullptr, x + lblW + g, y2, xW, comboH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        int y3 = y2 + rowH;
        SetWindowPos(lbl_xblk_, nullptr, x, y3 + 3, lblW, bH, SWP_NOZORDER);
        SetWindowPos(cmb_xblk_, nullptr, x + lblW + g, y3, xW, comboH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        int y4 = y3 + rowH;
        int tW = ScalePx(72, dpi_);                // §131：超时时间行
        SetWindowPos(lbl_proto_timeout_, nullptr, x, y4 + 3, lblW, bH, SWP_NOZORDER);
        SetWindowPos(edit_proto_timeout_, nullptr, x + lblW + g, y4, tW, bH, SWP_NOZORDER);
        SetWindowPos(lbl_proto_timeout_ms_, nullptr, x + lblW + g + tW + g, y4 + 3,
                     ScalePx(28, dpi_), bH, SWP_NOZORDER);
        int y5 = y4 + rowH;                        // §131 续：超时次数行
        SetWindowPos(lbl_proto_retries_, nullptr, x, y5 + 3, lblW, bH, SWP_NOZORDER);
        SetWindowPos(edit_proto_retries_, nullptr, x + lblW + g, y5, tW, bH, SWP_NOZORDER);
        SetWindowPos(lbl_proto_retries_c_, nullptr, x + lblW + g + tW + g, y5 + 3,
                     ScalePx(28, dpi_), bH, SWP_NOZORDER);
        int y6 = y5 + rowH;
        int oW = bW;
        int eW = cw - oW - g;
        if (eW < ScalePx(80, dpi_)) eW = ScalePx(80, dpi_);
        SetWindowPos(protoFileEdit_, nullptr, x, y6, eW, bH, SWP_NOZORDER);
        SetWindowPos(protoOpenBtn_, nullptr, x + eW + g, y6, oW, bH, SWP_NOZORDER);
        int y7 = y6 + rowH;
        SetWindowPos(chk_proto_show_, nullptr, x, y7 + 2,
                     ChkW(L"将收发内容显示到窗口") + ScalePx(8, dpi_), bH, SWP_NOZORDER);
        int y8 = y7 + rowH;
        int sW = ScalePx(88, dpi_);
        SetWindowPos(protoStartBtn_, nullptr, x, y8, sW, bH, SWP_NOZORDER);
        SetWindowPos(protoCancelBtn_, nullptr, x + sW + g, y8, bW, bH, SWP_NOZORDER);
        int x8 = x + sW + bW + 2 * g;
        int w8 = x + cw - x8;
        SetWindowPos(protoStatus_, nullptr, x8, y8 + 3,
                     w8 > ScalePx(60, dpi_) ? w8 : ScalePx(60, dpi_), ScalePx(18, dpi_), SWP_NOZORDER);
    }
}

void MainWindow::HandleExtNotify(NMHDR* nm) {
    if (nm->idFrom == IDC_EXT_TAB && nm->code == TCN_SELCHANGE)
        ShowExtPage((int)SendMessageW(extTab_, TCM_GETCURSEL, 0, 0));   // 与 HandleExt 的 WM_NOTIFY 路径统一
    if (nm->idFrom == 20 && nm->code == LVN_GETINFOTIPW) {
        auto* gi = (NMLVGETINFOTIPW*)nm;                 // §77：多条表悬停显示「第 N 条」（SSCOM V5.12）
        swprintf(gi->pszText, gi->cchTextMax, L"第 %d 条", (int)gi->iItem + 1);
    }
    if (nm->idFrom == IDC_ASCII_LIST && nm->code == NM_DBLCLK) {   // 双击复制字符（ASCII 表）
        // §94 附带修复：原判 idFrom==2（=IDC_CALC_DEC）——IDC_EXT_TAB 插入枚举首位后
        // IDC_ASCII_LIST 平移到 13，此处漏改致双击复制从未生效（§67「无 ID 平移」教训同型）
        auto* di = (NMITEMACTIVATE*)nm;
        if (di->iItem >= 0) {
            int d = di->iItem;
            wchar_t ch[8] = L"";
            if (d >= 32 && d <= 126) swprintf(ch, 8, L"%lc", (wchar_t)d);
            else {
                static const wchar_t* kAbbrs[33] = {
                    L"NUL", L"SOH", L"STX", L"ETX", L"EOT", L"ENQ", L"ACK", L"BEL",
                    L"BS", L"HT", L"LF", L"VT", L"FF", L"CR", L"SO", L"SI",
                    L"DLE", L"DC1", L"DC2", L"DC3", L"DC4", L"NAK", L"SYN", L"ETB",
                    L"CAN", L"EM", L"SUB", L"ESC", L"FS", L"GS", L"RS", L"US", L"SP" };
                StringCchCopyW(ch, 8, d <= 32 ? kAbbrs[d] : L"DEL");
            }
            if (OpenClipboard(extWnd_)) {
                EmptyClipboard();
                HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (wcslen(ch) + 1) * 2);
                if (g) {
                    // §99（审查另记）：GlobalLock 可能失败——失败须 GlobalFree（数据尚未交系统）
                    if (wchar_t* p = (wchar_t*)GlobalLock(g)) {
                        StringCchCopyW(p, wcslen(ch) + 1, ch);
                        GlobalUnlock(g);
                        SetClipboardData(CF_UNICODETEXT, g);
                    } else {
                        GlobalFree(g);
                    }
                }
                CloseClipboard();
            }
            Notify((std::wstring(L"已复制: ") + ch).c_str());
        }
    }
    if (nm->idFrom == 20) {                            // 多条发送表
        if (nm->code == NM_CLICK) {                    // 单击：发送列=发送，HEX 列=切换勾选框
            auto* di = (NMITEMACTIVATE*)nm;
            if (di->iItem >= 0 && di->iSubItem >= 0) {
                // ptAction 已是 msList_ 客户区坐标（此前误再 ScreenToClient 一次 → 错位到列头，
                // HEX 勾选框点击无效——用户反馈）
                LVHITTESTINFO ht{};
                ht.pt = di->ptAction;
                ListView_SubItemHitTest(msList_, &ht);
                if (ht.iItem >= 0) {
                    if (ht.iSubItem == 0) {
                        MsSendRow(ht.iItem);
                        msPressRow_ = ht.iItem;        // 按压视觉：凹陷 130ms 后弹起
                        SetTimer(hwnd_, 12, 130, nullptr);
                        RECT rr{};
                        ListView_GetItemRect(msList_, ht.iItem, &rr, LVIR_BOUNDS);
                        InvalidateRect(msList_, &rr, TRUE);
                        // §83（审查 R1 实锤）：WM_PAINT 与 130ms 的 WM_TIMER 同为空闲优先级，
                        // 定时器常常先复位 msPressRow_ → 两次失效合并成一帧、凹陷帧根本没上屏
                        //（实测：点击后 0/28/96/125ms 采样与 250ms 后**完全相同**）。同步刷新一次
                        // 保证凹陷先进屏，再等定时器弹起。
                        UpdateWindow(msList_);
                    }
                    else if (ht.iSubItem == 1) {
                        msRows_[ht.iItem].hex = !msRows_[ht.iItem].hex;
                        MsSyncList();                  // 重灌后 custom draw 重画勾选框
                        MsSyncToCfg();                 // §132：HEX 切换即时入配置
                    }
                    else if (ht.iSubItem == 5) MsEditCell(ht.iItem, 5);   // 注释列单击编辑
                }
            }
        }
        if (nm->code == NM_DBLCLK) {                   // 双击：就地编辑（按钮列=发送、HEX=勾选，均不编辑；
            auto* di = (NMITEMACTIVATE*)nm;            //   注释列单击编辑，见 NM_CLICK）
            if (di->iItem >= 0 && di->iSubItem >= 1)
                MsEditCell(di->iItem, di->iSubItem);
        }
    }
}

// ---------------- §129 协议文件发送（XMODEM/YMODEM 发送端；docs/17 口径） ----------------
void MainWindow::ProtoStart() {
    if (protoActive_) return;
    if (!LinkIsOpen()) { Notify(L"协议发送：未连接"); return; }
    if (loopMode_) { Notify(L"协议发送：软件回环模式下无意义"); return; }
    if (cfg_.mode == L"tcp_server" && net_.ClientCount() != 1) {
        // 服务端 Write 是广播：两个收端同时 ACK/NAK 会毁掉停等应答（docs/17 §3.5 互斥矩阵）
        Notify(L"协议发送：服务端多客户端无法停等应答，请用单客户端或客户端模式");
        return;
    }
    if (txFile_ != INVALID_HANDLE_VALUE) { Notify(L"协议发送：「发送文件」正在占用发送通道"); return; }
    if (timedRunning_) { Notify(L"协议发送：「定时发送」正在占用发送通道"); return; }
    if (msLoopActive_) { Notify(L"协议发送：「循环发送」正在占用发送通道"); return; }
    wchar_t path[MAX_PATH]{};
    GetWindowTextW(protoFileEdit_, path, MAX_PATH);
    if (!path[0]) { Notify(L"协议发送：请先选择文件"); return; }
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { Notify(L"协议发送：读取失败"); return; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(hf, &sz);
    if (sz.QuadPart > 512ull * 1024 * 1024) {
        CloseHandle(hf);
        Notify(L"协议发送：文件超过 512MB 上限");
        return;
    }
    std::vector<uint8_t> data;
    try {
        data.resize((size_t)sz.QuadPart);          // R23-P3-5：分配失败提示而非 std::terminate
    } catch (...) {
        CloseHandle(hf);
        Notify(L"协议发送：内存不足，无法载入该文件");
        return;
    }
    bool ok = true;                                // R23-P3-5：循环读满（短读续读，风格同 msini 导入）
    size_t off = 0;
    while (ok && off < data.size()) {
        DWORD got = 0;
        ok = ReadFile(hf, data.data() + off, (DWORD)(data.size() - off), &got, nullptr) && got > 0;
        if (ok) off += got;
    }
    CloseHandle(hf);
    if (!ok) { Notify(L"协议发送：读取失败"); return; }
    const wchar_t* base = path;                    // YMODEM 块 0 用文件名（UTF-8 原样序列）
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    int ulen = WideCharToMultiByte(CP_UTF8, 0, base, -1, nullptr, 0, nullptr, nullptr);
    std::string nameU(ulen > 1 ? (size_t)ulen - 1 : 8, 0);
    if (ulen > 1) WideCharToMultiByte(CP_UTF8, 0, base, -1, nameU.data(), ulen, nullptr, nullptr);
    else nameU = "file.bin";
    int sel = (int)SendMessageW(cmb_proto_, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = protoKindIdx_;              // 隐藏态选中丢失的兜底（页显形已重设）
    ProtoKind k = (sel == 1) ? ProtoKind::Ymodem : ProtoKind::Xmodem;
    int xc = (int)SendMessageW(cmb_xcheck_, CB_GETCURSEL, 0, 0);
    if (xc < 0) xc = protoXCheckIdx_;
    XCheck check = (xc == 1) ? XCheck::Crc16 : (xc == 2) ? XCheck::Checksum : XCheck::Auto;
    int xb = (int)SendMessageW(cmb_xblk_, CB_GETCURSEL, 0, 0);
    if (xb < 0) xb = protoXBlkIdx_;
    XBlk xblk = (xb == 1) ? XBlk::Blk1k : (xb == 2) ? XBlk::Blk128 : XBlk::Auto;
    protoShow_ = SendMessageW(chk_proto_show_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (protoShow_) rxv_.ProtoFollow(true);        // 续修5：显示期视图跟随底部（自动滚屏关闭也实时可见）
    // §131：应答超时快照（默认 1000ms；XMODEM/YMODEM 均生效）——会话期禁改该框
    {
        wchar_t tb[16]{};
        GetWindowTextW(edit_proto_timeout_, tb, 16);
        int tms = _wtoi(tb);
        if (tms < 100) tms = 100;
        if (tms > 60000) tms = 60000;
        cfg_.proto_timeout_ms = tms;
        protoTimeoutTicks_ = (tms + 29) / 30;      // 30ms 拍向上取整
        if (protoTimeoutTicks_ < 1) protoTimeoutTicks_ = 1;
    }
    // §131 续：超时次数快照（重发上限，统一作用于数据块/块 0/EOT；1~100 钳制）
    int rtx = 10;
    {
        wchar_t rb[16]{};
        GetWindowTextW(edit_proto_retries_, rb, 16);
        rtx = _wtoi(rb);
        if (rtx < 1) rtx = 1;
        if (rtx > 100) rtx = 100;
        cfg_.proto_retries = rtx;
    }
    proto_.Start(k, std::move(data), nameU, check, xblk, (unsigned)rtx);
    protoActive_ = true;
    protoWaitTicks_ = 0;
    protoStatusText_.clear();
    EnableWindow(protoStartBtn_, FALSE);
    EnableWindow(protoCancelBtn_, TRUE);
    EnableWindow(cmb_proto_, FALSE);
    EnableWindow(cmb_xcheck_, FALSE);
    EnableWindow(cmb_xblk_, FALSE);
    EnableWindow(edit_proto_timeout_, FALSE);      // §131
    EnableWindow(edit_proto_retries_, FALSE);      // §131 续
    ProtoUpdateStatus();
    SetTimer(hwnd_, 16, 30, nullptr);              // §79：新 id 追加枚举末尾（id=16）
    ProtoPump();                                   // §131：立即排空一次（等踢前无字节，统一入口）
    Notify(L"协议发送：已启动（等待对端启动字符）");
}

void MainWindow::ProtoTick() {
    if (!protoActive_) { KillTimer(hwnd_, 16); return; }
    if (!LinkIsOpen()) { ProtoFinish(L"连接已断开", false); return; }
    ProtoPump();                                   // §131：排空待发（亦由 RX 事件即时驱动）
    // 2) ACK 超时（§131：用户设置，默认 1000ms；30ms 拍计数，×向上取整；WM_TIMER 合并
    //    最坏 ~4.5s 一拍，停等协议无硬时限；不引 GetTickCount64 避免新导入——R22 门禁口径）
    if (proto_.WaitingAck()) {
        if (++protoWaitTicks_ >= protoTimeoutTicks_) {
            protoWaitTicks_ = 0;
            proto_.OnTimeout();
        }
    } else {
        protoWaitTicks_ = 0;
    }
    // 3) 收尾（状态机终态 → 恢复互斥面 + 通知）
    if (proto_.state() == ProtoState::Done) { ProtoFinish(L"完成", true); return; }
    if (proto_.state() == ProtoState::Aborted) { ProtoFinish(proto_.error().c_str(), false); return; }
    if (proto_.state() == ProtoState::Error) { ProtoFinish(proto_.error().c_str(), false); return; }
    ProtoUpdateStatus();
}

void MainWindow::ProtoPump() {
    // §131：事件驱动排空——RX 分流（对端踢到达/重发触发）与 30ms 拍共用。首包（踢后第一块/
    // 块 0）在 Feed 的同一消息内写出并送入显示管线，不再等下一拍（用户报「首包要过一会才显示」
    // 的根因）。背压（Write 部分接受/拒收）留余量，timer 拍续发（§99 语义）。
    while (!proto_.tx().empty()) {
        int n = LinkWrite(proto_.tx().data(), proto_.tx().size());
        if (n <= 0) break;                         // 背压：留队下拍续发
        txBytes_ += (unsigned)n;
        if (protoShow_) {                          // 续修5：发出的块入显示窗口（txfile §101 同型）
            newData_ = true;
            RequestFastDisplay();                  // R26-P3-1：长空闲后协议回显首屏即时（不入慢拍）
            builder_.feed(true, proto_.tx().data(), (size_t)n, NowSeconds(),
                          buf_.end_abs() - (proto_.tx().size() - n));
        }
        proto_.Consume((size_t)n);
    }
}

void MainWindow::ProtoFinish(const wchar_t* why, bool ok) {
    protoActive_ = false;
    KillTimer(hwnd_, 16);
    rxv_.ProtoFollow(false);                       // 续修5：会话结束解除跟随（停留当前位置）
    EnableWindow(protoStartBtn_, TRUE);
    EnableWindow(protoCancelBtn_, FALSE);
    EnableWindow(cmb_proto_, TRUE);
    EnableWindow(cmb_xcheck_, protoKindIdx_ == 0);   // 续修6：YMODEM 恒 CRC → 置灰
    EnableWindow(cmb_xblk_, TRUE);                   // 包大小两协议均生效
    EnableWindow(edit_proto_timeout_, TRUE);         // §131：会话结束恢复可改
    EnableWindow(edit_proto_retries_, TRUE);         // §131 续：同
    const ProtoProgress& pg = proto_.progress();
    wchar_t b[288];
    swprintf(b, 288, L"协议发送%s：%ls（块 %u/%u，%llu/%llu 字节）",
             ok ? L"完成" : L"结束", why, pg.block, pg.blocks, pg.confirmed, pg.total);
    protoStatusText_ = b;
    if (protoStatus_) SetWindowTextW(protoStatus_, b);
    Notify(b);
}

void MainWindow::ProtoUpdateStatus() {
    if (!protoStatus_) return;
    const ProtoProgress& pg = proto_.progress();
    wchar_t b[288];
    if (!protoActive_) {
        swprintf(b, 288, L"空闲（协议 %ls）", protoKindIdx_ == 1 ? L"YMODEM" : L"XMODEM");
    } else {
        swprintf(b, 288, L"%ls  块 %u/%u  %llu/%llu B  重发 %u",
                 pg.stage ? pg.stage : L"", pg.block, pg.blocks, pg.confirmed, pg.total,
                 pg.retries);
    }
    if (protoStatusText_ != b) {
        protoStatusText_ = b;
        SetWindowTextW(protoStatus_, b);
    }
}

void MainWindow::HandleExtCommand(int id, int code, HWND ctl) {
    // §75：查找页（控件已迁入扩展窗，命令在此路由）
    if (id == IDC_FIND_EDIT && code == EN_CHANGE) { FindRescanSoon(); return; }
    if ((id == IDC_FIND_CASE || id == IDC_FIND_HEX) && code == BN_CLICKED) { FindRescan(); return; }
    if (id == IDC_FIND_PREV && code == BN_CLICKED) { FindJump(-1); return; }
    if (id == IDC_FIND_NEXT && code == BN_CLICKED) { FindJump(1); return; }
    // §129 协议发送页
    if (id == IDC_PROTO_OPEN && code == BN_CLICKED) {
        wchar_t file[MAX_PATH]{};
        GetWindowTextW(protoFileEdit_, file, MAX_PATH);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = extWnd_;
        ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) SetWindowTextW(protoFileEdit_, file);
        return;
    }
    if (id == IDC_PROTO_START && code == BN_CLICKED) { ProtoStart(); return; }
    if (id == IDC_PROTO_CANCEL && code == BN_CLICKED) {
        if (protoActive_) proto_.Cancel();     // CAN CAN BS BS 由 ProtoTick 排空后收尾
        return;
    }
    if (id == IDC_PROTO_MODE && code == CBN_SELCHANGE) {
        int s = (int)SendMessageW(cmb_proto_, CB_GETCURSEL, 0, 0);
        if (s >= 0) {
            protoKindIdx_ = s;
            cfg_.proto_kind = s;               // §132：选择持久化（保存参数/关窗落盘）
            EnableWindow(cmb_xcheck_, !protoActive_ && s == 0);   // 续修6：YMODEM 恒 CRC → 置灰
        }
        return;                                // 包大小续修5 起两协议均生效——无置灰联动
    }
    if (id == IDC_XCHECK_MODE && code == CBN_SELCHANGE) {
        int s = (int)SendMessageW(cmb_xcheck_, CB_GETCURSEL, 0, 0);
        if (s >= 0) { protoXCheckIdx_ = s; cfg_.proto_xcheck = s; }   // §132
        return;
    }
    if (id == IDC_XBLK_MODE && code == CBN_SELCHANGE) {
        int s = (int)SendMessageW(cmb_xblk_, CB_GETCURSEL, 0, 0);
        if (s >= 0) { protoXBlkIdx_ = s; cfg_.proto_xblk = s; }       // §132
        return;
    }
    if (id == IDC_PROTO_SHOW && code == BN_CLICKED) {
        protoShow_ = SendMessageW(chk_proto_show_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        cfg_.proto_show = protoShow_;          // §132：勾选持久化
        if (protoActive_) rxv_.ProtoFollow(protoShow_);   // 续修5：会话中切换即时生效
        return;
    }
    if (id == IDC_PROTO_FILE && code == EN_CHANGE) {
        wchar_t b[MAX_PATH]{};                 // §132：文件路径持久化（程序 SetWindowText 亦入，幂等）
        GetWindowTextW(protoFileEdit_, b, MAX_PATH);
        cfg_.proto_file = b;
        return;
    }
    if (id == IDC_PROTO_TIMEOUT && code == EN_CHANGE) {
        // §131：合法值即认入 cfg（持久化随「保存参数」/关窗）；越界不入（ProtoStart 再钳）
        wchar_t b[16]{};
        GetWindowTextW(edit_proto_timeout_, b, 16);
        int ms = _wtoi(b);
        if (ms >= 100 && ms <= 60000) cfg_.proto_timeout_ms = ms;
        return;
    }
    if (id == IDC_PROTO_RETRIES && code == EN_CHANGE) {
        // §131 续：超时次数（1~100，超界不入；ProtoStart 再钳）
        wchar_t b[16]{};
        GetWindowTextW(edit_proto_retries_, b, 16);
        int n = _wtoi(b);
        if (n >= 1 && n <= 100) cfg_.proto_retries = n;
        return;
    }
    // 多条发送页
    if (id == 22) MsAddRow();
    else if (id == 23) MsDelRow();
    else if (id == 24) MsImportIni();
    else if (id == 25) {                               // 循环发送 toggle
        if (SendMessageW(msLoopBtn_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            if (protoActive_) {                        // §129 互斥：通道被协议会话占用
                Notify(L"「协议发送」正在占用发送通道");
                SendMessageW(msLoopBtn_, BM_SETCHECK, BST_UNCHECKED, 0);
                return;
            }
            msLoopActive_ = true;
            msIdx_ = 0;
            cfg_.multisend_loop = true;                // §132：勾选持久化
            MsLoopNext();
        } else {
            msLoopActive_ = false;
            KillTimer(hwnd_, 11);
            cfg_.multisend_loop = false;               // §132
            Notify(L"循环发送已停止");
        }
    }
    // 就地编辑写回（失焦写回；回车由 msEdit_ 子类化转成失焦）
    if (id == 21 && code == EN_KILLFOCUS) {
        if (msEditRow_ >= 0 && msEditRow_ < (int)msRows_.size()) {
            wchar_t b[1024]{};
            GetWindowTextW(msEdit_, b, 1024);
            MsRow& m = msRows_[msEditRow_];
            if (msEditCol_ == 5) m.note = b;
            else if (msEditCol_ == 2) m.text = b;
            else if (msEditCol_ == 3) m.order = max(0, min(9999, _wtoi(b)));
            else if (msEditCol_ == 4) m.interval = max(0, min(3600000, _wtoi(b)));
            MsSyncList();
            MsSyncToCfg();                             // §132：单元格提交即时入配置
        }
        ShowWindow(msEdit_, SW_HIDE);
        msEditRow_ = -1;
    }
}

} // namespace sncom
