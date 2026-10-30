# -*- coding: utf-8 -*-
# verify_r138.py - docs/00 §138（用户拍板「现在就压产品占用」）：空闲 CPU 降唤醒改造验收。
#  改造内容：显示 tick（id4）空闲降档 33→100→250→500ms；守卫 tick（id15）安静且无待发
#  60→240ms（§120 只降采样密度、唤醒次数没降）；数据到达/终端回显/待发入队/HEX·分包勾选
#  由 RequestFastDisplay 拉回快拍（周期未变时零成本）。
# 判据：
#  A) 代码级：自适应结构、恢复点覆盖（RX×4/回显/终端×2/待发续发/入队/勾选/显示 tick 自身）
#  B) 行为级：静置 8s（tick 已入慢档）后触发回环演示 → 转存日志 1.5s 内出现新行
#     =「慢拍状态下数据到达即时恢复」的产品级判据（转存 poll 100ms 不受降档影响，
#       演示帧 1-6 自带 \r\n 为收尾行，规则 29 口径）。
# 空闲 CPU 数值判据归 verify_r117-C（§138 后实测 0.281/0.305/0.296%，余量恢复）。
import os, re, glob, time, shutil, subprocess, ctypes
import win32gui, win32con, win32api, win32process

HERE = os.path.dirname(os.path.abspath(__file__))
SNCOM = os.path.dirname(HERE)
DEV = os.path.join(SNCOM, "develop")
DIST = os.path.join(DEV, "dist")
BIN = os.path.join(HERE, "_bin")
os.makedirs(BIN, exist_ok=True)

res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()

mw_h = read(os.path.join(DEV, "src", "ui", "mainwindow.h"))
mw_cpp = read(os.path.join(DEV, "src", "ui", "mainwindow.cpp"))

# ---------- A) 代码级 ----------
chk("A1 头文件：降档成员与恢复入口声明",
    "dispTickMs_" in mw_h and "guardTickMs_" in mw_h and "void RequestFastDisplay();" in mw_h)
chk("A2 显示 tick 空闲降档阶梯（100/250/500）",
    "(dispIdle_ >= 30) ? 500" in mw_cpp and "(dispIdle_ >= 12) ? 250" in mw_cpp
    and "(dispIdle_ >= 3) ? 100" in mw_cpp)
chk("A3 守卫 tick 安静且无待发降 240ms",
    "guardTickMs_ != 240" in mw_cpp and "grayQuiet_ >= 16 && txPend_.empty()" in mw_cpp)
n_calls = mw_cpp.count("RequestFastDisplay()")
chk("A4 恢复点覆盖（R26-P3-1 补齐后：调用 12 + 定义 1 = 13）",
    n_calls == 13, "RequestFastDisplay 出现 %d 次（含 1 处定义）" % n_calls)
chk("A5 EnqueueTx/勾选/文件发送/协议回显四类恢复就位",
    re.search(r"void MainWindow::EnqueueTx[^{]*\{\s*\n\s*RequestFastDisplay\(\);", mw_cpp) is not None
    and "if (id == IDC_CHK_HEXSHOW || id == IDC_CHK_SPLIT) RequestFastDisplay();" in mw_cpp
    and mw_cpp.count("// R26-P3-1") == 2)

# ---------- B) 行为级：静置降档后数据到达即时恢复 ----------
subprocess.run(["taskkill", "/F", "/IM", "sncom.exe"], capture_output=True)   # 防运行中实例锁 dist exe
time.sleep(1.2)
shutil.copy2(os.path.join(DIST, "sncom.exe"), os.path.join(BIN, "sncom.exe"))
for old in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")):
    os.remove(old)
before = set(glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")))
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)
user32 = ctypes.windll.user32
rxfile = user32.GetDlgItem(main, 3039)                 # 接收转存勾选
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) != 1:     # BM_GETCHECK：未勾选则勾上
    win32api.PostMessage(rxfile, 0x00F5, 0, 0)         # BM_CLICK（勾选即生效、无文件对话框）
    time.sleep(0.6)
logs0 = [q for q in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")) if q not in before]
s0 = max((os.path.getsize(q) for q in logs0), default=0)
print("B 前置：转存已开（初始文件=%s）" % ("有" if logs0 else "无"), flush=True)
time.sleep(8.0)                                        # 静置：id4 降到 500ms 档、id15 降到 240ms 档
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # 回环演示（8 帧测试数据）
t0 = time.time()
grew = False
while time.time() - t0 < 1.5:
    time.sleep(0.1)
    ns = [q for q in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")) if q not in before]
    if ns and max(os.path.getsize(q) for q in ns) > s0:
        grew = True
        break
chk("B1 静置降档后数据到达 1.5s 内进转存（慢拍恢复即时）", grew,
    "静置 8s → 回环演示 → %.1fs 内日志增长" % (time.time() - t0))
# 收尾读内容（转存文件被应用持有，须先取消勾选）
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) == 1:
    win32api.PostMessage(rxfile, 0x00F5, 0, 0)
    time.sleep(0.6)
ns = sorted(q for q in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")) if q not in before)
data = b""
if ns:
    with open(ns[-1], "rb") as f:
        data = f.read()
chk("B2 演示帧确实进显示管线（日志含回环演示文本）", "回环演示".encode("utf-8") in data or
    "pqrstuvwxyz".encode() in data, "len=%d" % len(data))
proc.terminate()
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
