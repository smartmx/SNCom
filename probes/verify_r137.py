# -*- coding: utf-8 -*-
# verify_r137.py - docs/00 §137（用户两条）：
#  1) 去掉未注册的进程 AUMID（main.cpp 不再 SetAppIdentity("SNCom.SerialDebugger")，
#     win_util 不再有 SetAppIdentity/SetCurrentProcessExplicitAppUserModelID）；
#  2) 建窗后主动调一次 ITaskbarList::AddTab（win_util.TaskbarAddTab + main.cpp 建窗后调用）。
# 判据：
#  A) 代码级：src 关键字断言（AUMID 全退场 / AddTab 调用链就位 / 白名单含 OLE32 两项）
#  B) 导入门：win7_import_audit 对 dist\sncom.exe 判 PASS（语义断言 dlls==12 ∧
#     approved==imported ∧ 非空——R29-P3-1 前此为硬编码「251」，随导入增减过期）
#  C) 行为级：启动 dist exe → UIA 轮询双任务栏出现「SNCom…」应用按钮 ≤12s（PowerShell
#     内联执行，结果走 stdout，无临时文件）。
#     判活门（R25 实证）：UIA 任务栏树在显示关闭/闲置期会冻结（时钟元素名停在旧时刻），
#     冻结期一切「按钮未出现」都是假阴性，时钟元素名不等于当前 HH:MM 时判环境性 SKIP，
#     不得红判产品（与 probes/README 规则 28 的「先判活再下判」同源）。
import os, re, subprocess, time, datetime

HERE = os.path.dirname(os.path.abspath(__file__))            # SNCom 的 probes 目录
SNCOM = os.path.dirname(HERE)                                # SNCom 根
DEV = os.path.join(SNCOM, "develop")
DIST = os.path.join(DEV, "dist")

res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

# ---------- A) 代码级 ----------
def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()

main_cpp = read(os.path.join(DEV, "src", "main.cpp"))
wu_cpp = read(os.path.join(DEV, "src", "platform", "win_util.cpp"))
wu_h = read(os.path.join(DEV, "src", "platform", "win_util.h"))
allow = read(os.path.join(DEV, "tools", "win7_import_allowlist.txt"))

chk("A1 main.cpp 无 AUMID/SetAppIdentity 残留",
    "SetAppIdentity" not in main_cpp and "SerialDebugger" not in main_cpp)
chk("A2 main.cpp 建窗后调用 TaskbarAddTab",
    "sncom::TaskbarAddTab(hwnd)" in main_cpp
    and main_cpp.index("MainWindow::Create(") < main_cpp.index("sncom::TaskbarAddTab(hwnd)"))
chk("A3 win_util 无 SetAppIdentity/SetCurrentProcessExplicitAppUserModelID",
    "SetAppIdentity" not in wu_cpp and "SetCurrentProcessExplicitAppUserModelID" not in wu_cpp
    and "SetAppIdentity" not in wu_h)
chk("A4 win_util TaskbarAddTab=ITaskbarList::HrInit+AddTab",
    "TaskbarAddTab" in wu_cpp and "ITaskbarList" in wu_cpp
    and "HrInit" in wu_cpp and "->AddTab(hwnd)" in wu_cpp)
chk("A5 导入白名单含 OLE32 两项",
    "OLE32.DLL!CoCreateInstance" in allow and "OLE32.DLL!CoInitializeEx" in allow)

# ---------- B) 导入门 ----------
r = subprocess.run(["python", "-X", "utf8", os.path.join(DEV, "tools", "win7_import_audit.py"),
                    os.path.join(DIST, "sncom.exe")], capture_output=True, text=True,
                   cwd=DEV, errors="replace")
audit = (r.stdout or "") + (r.stderr or "")
m = re.search(r"imports: (\d+) dlls / (\d+) functions \(approved set: (\d+)\)", audit)
chk("B1 win7_import_audit PASS", "RESULT PASS" in audit,
    (m.group(0) if m else audit.strip().splitlines()[-1] if audit.strip() else "no output"))
if m:
    # §142（R28-P3-1）改语义断言：导入增减（如 §141a 删 lstrcpyW 251→250）不再改探针——
    # 只锁「DLL 数=12 ∧ 审批集=实际导入 ∧ 非空」三元组（精确基线惯例由审计的 STALE 检查兜底）
    dlls, imp, appr = m.group(1), int(m.group(2)), int(m.group(3))
    chk("B2 导入语义断言：12 dlls ∧ 审批集==实际导入 ∧ 非空",
        dlls == "12" and imp == appr and imp > 0, m.group(0))

# ---------- C) 行为级（UIA 轮询 + 判活门） ----------
subprocess.run(["taskkill", "/F", "/IM", "sncom.exe"], capture_output=True)
time.sleep(1.5)

PS = r'''
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$root = $AE::RootElement
function Scan {
    $out = @()
    foreach ($cls in @('Shell_TrayWnd','Shell_SecondaryTrayWnd')) {
        $c = New-Object System.Windows.Automation.PropertyCondition($AE::ClassNameProperty, $cls)
        $bars = $root.FindAll($TS::Children, $c)
        foreach ($bar in $bars) {
            $all = $bar.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
            foreach ($e in $all) {
                if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.Button') {
                    $out += ("{0}|{1}" -f $cls, $e.Current.Name)
                }
            }
        }
    }
    return ,$out
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$clock = '?'
$found = $null
while ($sw.Elapsed.TotalSeconds -lt 12) {
    $btns = Scan
    foreach ($b in $btns) {
        if ($b -match '\|SNCom' -and $b -notmatch 'SNCom Ext') {
            $found = @{t=$sw.Elapsed.TotalSeconds; s=$b}
            break
        }
    }
    if ($found) { break }
    foreach ($b in $btns) { if ($b -match '^Shell_TrayWnd\|时钟') { $clock = $b.Split('|')[1] } }
    Start-Sleep -Milliseconds 300
}
$btns = Scan
foreach ($b in $btns) { if ($b -match '^Shell_TrayWnd\|时钟') { $clock = $b.Split('|')[1] } }
Write-Output "CLOCK=$clock"
if ($found) { Write-Output ("FOUND {0:N1}s {1}" -f $found.t, $found.s) } else { Write-Output "NOTFOUND" }
'''
p = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", PS],
                   capture_output=True, text=True, errors="replace")
out = r.stdout or ""
clock = "?"
m = re.search(r"CLOCK=(.+)", out)
if m:
    clock = m.group(1).strip()
nowhm = datetime.datetime.now().strftime("%H:%M")
def _hm(s):
    m2 = re.search(r"(\d{1,2}):(\d{2})", s)
    return (int(m2.group(1)) * 60 + int(m2.group(2))) if m2 else None
# 任务栏时钟元素无前导零（实测 '5:59'）——数值分钟比较，±1 分钟容差（跨分采样）
_ck, _nw = _hm(clock), _hm(nowhm)
alive = _ck is not None and _nw is not None and abs(_ck - _nw) <= 1
chk("C1 UIA 任务栏树判活（时钟元素=当前 HH:MM）", alive,
    "clock=%r now=%r %s" % (clock, nowhm, "（冻结期按环境 SKIP 处理）" if not alive else ""))
mm = re.search(r"FOUND\s+([0-9.]+)s\s+(.+)", out)
if mm:
    chk("C2 建窗后任务栏出现 SNCom 按钮（AddTab 生效）", True,
        "+%ss %s" % (mm.group(1), mm.group(2)))
    print("INFO 按钮全名（组名应取 exe FileDescription 回退口径）: " + mm.group(2), flush=True)
elif not alive:
    print("SKIP r137 C2（UIA 树冻结期，按钮判据无效——环境性非产品）", flush=True)
else:
    chk("C2 建窗后任务栏出现 SNCom 按钮（AddTab 生效）", False, "12s 未出现（UIA 树存活）")
p.terminate()
try:
    p.wait(timeout=3)
except Exception:
    p.kill()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
