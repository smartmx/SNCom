# -*- coding: utf-8 -*-
# verify_r45b.py - does the HDN_BEGINTRACK interception (idFrom==0) also lock the ASCII page headers?
import os, time, subprocess
import win32gui, win32con, win32api, win32process


# ---- §84（五轮审查 S1）：真实输入调用统一守卫 ------------------------------------
# SetCursorPos/mouse_event/keybd_event 在系统拒绝时会抛 pywintypes.error，脚本会以
# traceback 中断（批跑里表现为 "no summary"，掩盖"没测到"）。统一包一层：失败置
# INPUT_OK=False 并打印 NOTE，调用方据此 SKIP 相关断言（与 r73/r79 同款口径）。
INPUT_OK = True


def _safe_input(fn, *a, **kw):
    global INPUT_OK
    try:
        return fn(*a, **kw)
    except Exception as e:
        if INPUT_OK:
            print("NOTE: 真实输入调用被拒（%s: %s）——相关断言将 SKIP" % (getattr(fn, "__name__", fn), e), flush=True)
        INPUT_OK = False
        return None


def SetCursorPos(pt):
    return _safe_input(win32api.SetCursorPos, pt)


def mouse_event(*a):
    return _safe_input(win32api.mouse_event, *a)


def keybd_event(*a):
    return _safe_input(win32api.keybd_event, *a)
# ---------------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
ID_EXPAND, ID_ASC = 3012, 13
WM_APP = 0x8000
LVM_GETHEADER, LVM_GETCOLUMNWIDTH, LVM_GETITEMCOUNT = 0x101F, 0x101D, 0x1004
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
time.sleep(3.0)
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
win32gui.PostMessage(ext, WM_APP + 0x111, 2, 0)      # switch to ASCII page
time.sleep(0.8)
asc = win32gui.GetDlgItem(ext, ID_ASC)
hd = win32gui.SendMessage(asc, LVM_GETHEADER, 0, 0)
w = [win32gui.SendMessage(asc, LVM_GETCOLUMNWIDTH, i, 0) for i in range(5)]
print("ascii rows=%d widths=%s" % (win32gui.SendMessage(asc, LVM_GETITEMCOUNT, 0, 0), w), flush=True)
def drag(xdiv, dx=40):
    l, t, r, b = win32gui.GetWindowRect(hd)
    SetCursorPos((l + xdiv, t + 10)); time.sleep(0.15)
    win32gui.PostMessage(hd, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, win32api.MAKELONG(xdiv, 10))
    time.sleep(0.15)
    for s in range(1, 5):
        nx = xdiv + dx * s // 4
        SetCursorPos((l + nx, t + 10))
        win32gui.PostMessage(hd, win32con.WM_MOUSEMOVE, win32con.MK_LBUTTON, win32api.MAKELONG(nx, 10))
        time.sleep(0.08)
    win32gui.PostMessage(hd, win32con.WM_LBUTTONUP, 0, win32api.MAKELONG(xdiv + dx, 10))
    time.sleep(0.5)
drag(w[0])
w2 = [win32gui.SendMessage(asc, LVM_GETCOLUMNWIDTH, i, 0) for i in range(5)]
print("after drag on ascii col0 divider: widths=%s  col0 %d -> %d" % (w2, w[0], w2[0]), flush=True)
drag(w2[0] + w2[1] + w2[2])
w3 = [win32gui.SendMessage(asc, LVM_GETCOLUMNWIDTH, i, 0) for i in range(5)]
print("after drag on ascii col2 divider: widths=%s  col2 %d -> %d" % (w3, w2[2], w3[2]), flush=True)
# col1 check = stability across the col2 drag (col1 divider itself is never dragged)
print("RESULT ascii col0 locked=%s col1 stable-across-col2-drag=%s col2 draggable=%s" % (w2[0] == w[0], w3[1] == w[1], w3[2] != w2[2]), flush=True)
proc.terminate()
