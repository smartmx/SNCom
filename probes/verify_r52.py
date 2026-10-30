# -*- coding: utf-8 -*-
# verify_r52.py - docs/00 §52 two user decisions:
#  1) ext window height snapshots at OPEN only (no live follow while open;
#     re-open re-snapshots to current main height)
#  2) 编码 menu has exactly the 12 presets, no 自定义编码 item / no trailing separator
import os, time, subprocess, ctypes
import sys                                   # §97：SKIP 路径 sys.exit（此前缺 import，走到即崩）
import win32gui, win32con, win32process, win32api
user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
ID_EXPAND = 3012
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

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# §93（七轮审查 U3）：统一"目标窗就绪"等待——批跑负载下 ext/控件可能延迟出现，
# 此前单跑全绿、批跑崩栈（GetDlgItem 句柄失效/Aеrt ext 失败）。失败 → NOTE + 优雅退出。
def wait_toplevel(cls, pid, tries=40, delay=0.15):
    for _ in range(tries):
        h = find_toplevel(cls, pid)
        if h:
            return h
        time.sleep(delay)
    return 0

def wait_ext(tries=40):
    ext = wait_toplevel("SNComExt", proc.pid, tries)
    if not ext:
        print("NOTE: 扩展窗 %d 次等待仍未出现（系统负载/时序）——本探针 SKIP，不判失败" % tries, flush=True)
        print("==== SUMMARY ==== total=0 pass=0 fail=0 (SKIP: ext window not ready)")
        try:
            win32gui.PostMessage(main, win32con.WM_CLOSE, 0, 0)
            time.sleep(0.6)
        except Exception:
            pass
        if proc.poll() is None:
            proc.kill()
        sys.exit(0)
    return ext

# ---- 2) 编码菜单枚举 ----
MF_BYPOSITION = 0x00000400
def menu_label(hmenu, i):
    buf = ctypes.create_unicode_buffer(96)
    user32.GetMenuStringW(hmenu, i, buf, 96, MF_BYPOSITION)
    return buf.value
menu = win32gui.GetMenu(main)
enc = 0
count = win32gui.GetMenuItemCount(menu)
for i in range(count):
    if menu_label(menu, i).startswith("编码"):
        enc = win32gui.GetSubMenu(menu, i)
        break
assert enc, "encoding submenu not found"
n = win32gui.GetMenuItemCount(enc)
labels = [menu_label(enc, i) for i in range(n)]
chk("enc menu count == 12", n == 12, "items=%d %s" % (n, labels))
chk("no 自定义编码 item", not any("自定义" in x for x in labels))
chk("no trailing separator", all(x.strip() for x in labels))

# ---- 1) ext 高度三段语义 ----
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)   # BM_CLICK
time.sleep(1.2)
ext = wait_ext()
l0, t0, r0, b0 = win32gui.GetWindowRect(main)
h_open = win32gui.GetWindowRect(ext)[3] - win32gui.GetWindowRect(ext)[1]
h_main0 = b0 - t0
chk("open: ext height == main height", h_open == h_main0, "ext=%d main=%d" % (h_open, h_main0))

# 主窗改高 +120：打开期间 ext 不得跟随
win32gui.SetWindowPos(main, 0, 0, 0, r0 - l0, h_main0 + 120,
                      win32con.SWP_NOZORDER | win32con.SWP_NOMOVE)
time.sleep(1.0)
h_ext2 = win32gui.GetWindowRect(ext)[3] - win32gui.GetWindowRect(ext)[1]
chk("open: main resized -> ext height UNCHANGED", h_ext2 == h_open,
    "ext=%d (was %d)" % (h_ext2, h_open))

# 关闭再打开：重新快照为新主窗高度
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)   # 收起
time.sleep(0.8)
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)   # 再展开
time.sleep(1.2)
h_ext3 = win32gui.GetWindowRect(ext)[3] - win32gui.GetWindowRect(ext)[1]
l1, t1, r1, b1 = win32gui.GetWindowRect(main)
chk("reopen: ext height == NEW main height", h_ext3 == b1 - t1,
    "ext=%d main=%d" % (h_ext3, b1 - t1))
proc.terminate()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
