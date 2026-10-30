// sncom/src/platform/win7_shim.cpp —— §105：Win7 加载期兼容垫片（C 侧 thunk）
//
// 根因（§105 实证）：MSVC 14.51 的静态 STL libcpmt.lib 中 xtime.obj / winapisupp.obj
// 对 Win8+ 的 KERNEL32!GetSystemTimePreciseAsFileTime 是**静态导入**
// （UNDEF __imp__GetSystemTimePreciseAsFileTime@4）。本程序包含 <thread>（serial.h）即会
// 抽入 cthread.obj，其引用 _Xtime_diff_to_millis2 → 连带抽入 xtime.obj → 生成的 exe
// 导入表带上该函数 → 在 Win7 SP1 上**加载期**即弹「无法定位程序输入点
// GetSystemTimePreciseAsFileTime 于动态链接库 KERNEL32.dll 上」，程序无法启动（用户实拍）。
// UCRT 侧（ftime.obj/time.obj）走 winapi_thunks.obj 的运行时探测（GetProcAddress +
// 回退 GetSystemTimeAsFileTime），本身兼容 Win7——只有 STL 这两个成员是硬导入。
//
// 对策：本 TU 提供 thunk（退化实现=Win7 即有的 GetSystemTimeAsFileTime），
// 由同目录 win7_shim.asm 用**精确装饰名**定义导入数据槽
// __imp__GetSystemTimePreciseAsFileTime@4 指向本 thunk。链接器发现该符号已有定义，
// 就不再从 kernel32.lib 抽取其导入成员 → 导入表不含该函数 → Win7 可加载。
// ABI 关键代码（参数传递/栈清理）留在 C，交给编译器生成——手写汇编 thunk 曾因
// stdcall 实参位置取错（[esp+8] 应为 [esp+4]）在运行期段错误（实验 tiny3 实证）。
//
// 已验证的失败替代（勿再走）：①/alternatename（kernel32.lib 有定义，别名不生效）；
// ②/export:__imp_..@4=...（链接器对 __imp_ 前缀有特殊语义，无法解析内部名）；
// ③手写汇编 thunk（ABI 易错，见上）。
//
// 守卫：`tools/win7_import_audit.py` 解析 PE 导入表，断言无 Win8+ 静态导入；
// build.bat 链接后自动运行，非零即构建失败。换工具链/换 STL/引入新 STL 用法时，
// 该审计会立刻报红（§105 红绿对照：无垫片 73 导入含 Precise=FAIL；带垫片 72 无= PASS）。
#include <windows.h>

extern "C" void WINAPI sncom_gsptaft_shim(LPFILETIME ft)
{
    ::GetSystemTimeAsFileTime(ft);           // Win8+ 的 Precise 仅在精度上更强，语义等价
}
