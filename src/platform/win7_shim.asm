; sncom/src/platform/win7_shim.asm —— §105：Win7 加载期兼容垫片（x86 数据槽）
;
; 为 libcpmt.lib(xtime.obj / winapisupp.obj) 的 UNDEF 符号
;   __imp__GetSystemTimePreciseAsFileTime@4      （x86 stdcall 间接调用槽）
; 提供一个本地定义（内容 = C 侧 thunk 的地址）。链接器据此不再从 kernel32.lib
; 抽取该函数的导入成员 → exe 导入表不含 Win8+ API → Win7 SP1 可加载。
; 详见 win7_shim.cpp 头部（含根因、被否替代方案与审计门禁）。
;
; 命名说明（实测）：.model flat（不带 stdcall 修饰）下 public 数据符号**原样**输出，
; 故这里写全名；若加 stdcall 修饰 MASM 会再加前导下划线（曾致 ___imp__… 三下划线）。
; 仅 x86。若将来做 x64（本工程为 vcvarsamd64_x86 交叉编译 x86），x64 的槽名无 @ 后缀
; 且无下划线：__imp_GetSystemTimePreciseAsFileTime，需要另写一份并同样过审计。
.386
.model flat
option casemap:none

extern _sncom_gsptaft_shim@4:near            ; C 侧 thunk（win7_shim.cpp）

.data
public __imp__GetSystemTimePreciseAsFileTime@4
__imp__GetSystemTimePreciseAsFileTime@4 dd _sncom_gsptaft_shim@4

end
