# -*- coding: utf-8 -*-
# verify_r89.py - docs/00 §89（用户报）：查找页不需要独立 × 关闭按钮（扩展窗自身有关闭）。
#   断言：①查找页无 × 按钮（id 3080 无控件）；②查找功能不受影响（输入词→计数更新）；
#   ③第二行「计数」标签用满 × 移除后的剩余宽度（右缘贴列表右缘 ±8）。
import os, time, subprocess, ctypes, win32gui, win32process, win32con, win32api, shutil
user32 = ctypes.windll.user32
res = []


def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)


def find_toplevel(cls, pid):
    out = []

    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


def wtext(h, cap=256):
    b = ctypes.create_unicode_buffer(cap)
    user32.SendMessageW(h, 0x000D, cap, b)
    return b.value


def wset(h, s):
    user32.SendMessageW(h, 0x000C, 0, ctypes.c_wchar_p(s))


# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r89"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r89\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r89\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r89")
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)

# 喂数据（供查找命中）
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)      # 回环演示
time.sleep(1.5)
bm = user32.GetDlgItem(main, 3012)
win32api.PostMessage(bm, 0x00F5, 0, 0)                        # 扩展
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
win32api.PostMessage(ext, win32con.WM_COMMAND, (3 << 0) | 0, 0) if False else None
# 切到查找页（TCM_SETCURSEL + 页显隐经 WM_APP+0x111）
tab = user32.GetDlgItem(ext, 1)
user32.SendMessageW(tab, 0x130C, 3, 0)                        # TCM_SETCURSEL = 3
win32api.PostMessage(ext, win32con.WM_APP + 0x111, 3, 0)
time.sleep(0.6)

chk("§89 查找页无 × 关闭按钮（id 3080 无控件）", user32.GetDlgItem(ext, 3080) == 0, "")
fe = user32.GetDlgItem(ext, 3074)                             # IDC_FIND_EDIT
fc = user32.GetDlgItem(ext, 3077)                             # IDC_FIND_COUNT（3078=上一个）
lst = user32.GetDlgItem(ext, 20)
chk("§89 查找页其余控件在位（编辑/计数）", bool(fe and fc and user32.IsWindowVisible(fe)),
    "edit=%s count=%s" % (fe, fc))

# 查找功能不回归：输入词 → 计数更新
wset(fe, "回环")
time.sleep(0.8)
cnt = wtext(fc)
chk("§89 查找功能不回归（计数非 0/0）", cnt not in ("", "0/0"), "count=%r" % cnt)

# 计数标签右缘：应贴列表右缘（× 移除后用满剩余宽度）
class RC(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
rl, rt, rr, rb = win32gui.GetWindowRect(lst)
cl, ct, cr, cb = win32gui.GetWindowRect(fc)
gap = rr - cr
chk("§89 计数标签右缘贴列表右缘（gap ≤ 12）", 0 <= gap <= 12, "gap=%d" % gap)

win32gui.PostMessage(main, win32con.WM_CLOSE, 0, 0)
time.sleep(0.7)
if proc.poll() is None:
    proc.kill()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
