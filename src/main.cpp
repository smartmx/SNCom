// sncom/src/main.cpp —— 入口
#include <windows.h>
#include <commctrl.h>
#include <cwchar>
#include "ui/mainwindow.h"
#include "platform/win_util.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmd, int nCmdShow) {
    sncom::EnableDpiAwareness();                      // PerMonitorV2 → Win7 回退

    WSADATA wsa{};                                    // 网卡枚举/后续网络功能共用
    WSAStartup(MAKEWORD(2, 2), &wsa);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    if (!sncom::MainWindow::RegisterClasses(hInst)) {
        MessageBoxW(nullptr, L"窗口类注册失败", L"SNCom", MB_OK | MB_ICONERROR);
        return 1;
    }
    // --spike-selftest：M0.5 自检（200 行渲染 + 软件回环收发），供截图/无人值守验证
    bool selftest = cmd && wcsstr(cmd, L"--spike-selftest") != nullptr;
    HWND hwnd = sncom::MainWindow::Create(hInst, nCmdShow, selftest);
    if (!hwnd) {
        MessageBoxW(nullptr, L"主窗口创建失败", L"SNCom", MB_OK | MB_ICONERROR);
        return 1;
    }
    sncom::TaskbarAddTab(hwnd);     // §137：建窗后主动上任务栏（shell 异步加按钮偶发迟滞）
    MSG msg{};
    ACCEL acc = { FVIRTKEY | FCONTROL, 'F', 2208 };    // Ctrl+F → IDM_VIEW_FIND（显示→查找）
    HACCEL hAccel = CreateAcceleratorTableW(&acc, 1);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 滚轮悬停路由（C2a）：滚轮消息本发给焦点窗口；鼠标悬停在自绘接收区时
        // 转发给它（PyCom 口径——焦点在发送框等控件时滚轮仍可滚接收区）
        if (msg.message == WM_MOUSEWHEEL) {
            HWND hover = sncom::MainWindow::WheelTarget(msg);
            if (hover && hover != msg.hwnd) msg.hwnd = hover;
        }
        if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyAcceleratorTable(hAccel);
    WSACleanup();
    return (int)msg.wParam;
}
