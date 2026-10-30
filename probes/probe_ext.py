# -*- coding: utf-8 -*-
# probe_ext.py - ext window: resize + 3-page function checks
import time
import win32gui
import win32con
import subprocess
import os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "develop", "dist"))
p = subprocess.Popen([r"sncom.exe"], cwd=".")

def find_main():
    return win32gui.FindWindow("SNComMain", None)

h = None
for _ in range(30):
    h = find_main()
    if h:
        break
    time.sleep(0.3)
assert h, "no main window"

def click(hw):
    import win32api
    r = win32gui.GetWindowRect(hw)
    x, y = (r[0] + r[2]) // 2, (r[1] + r[3]) // 2
    win32api.SetCursorPos((x, y))
    win32api.mouse_event(2, 0, 0, 0, 0)
    win32api.mouse_event(4, 0, 0, 0, 0)

def tab_click(tab, idx, pages):
    import win32api
    r = win32gui.GetWindowRect(tab)
    x = r[0] + 35 + idx * 70          # 页签按文字宽排列（非均分），每页约 70px
    y = r[1] + 12
    win32api.SetCursorPos((x, y))
    win32api.mouse_event(2, 0, 0, 0, 0)
    win32api.mouse_event(4, 0, 0, 0, 0)

def dlg_text(h, id_):
    hw = win32gui.GetDlgItem(h, id_)
    n = win32gui.SendMessage(hw, win32con.WM_GETTEXTLENGTH, 0, 0) + 4
    buf = win32gui.PyGetMemory(n * 2 + 4)
    win32gui.SendMessage(hw, win32con.WM_GETTEXT, n, buf.address)
    return bytes(buf[:n * 2]).decode("utf-16-le", "replace").rstrip("\x00")

def lv_count(lv):
    return win32gui.SendMessage(lv, 0x1004, 0, 0)  # LVM_GETITEMCOUNT

# open ext via expand button
btn_expand = win32gui.GetDlgItem(h, 3012)
win32gui.SendMessage(btn_expand, 0x00F5, 0, 0)
time.sleep(0.8)

def find_ext():
    out = []
    def cb(hw, _):
        if win32gui.GetClassName(hw) == "SNComExt" and win32gui.IsWindowVisible(hw):
            out.append(hw)
    win32gui.EnumWindows(cb, None)
    return out[0] if out else None

ext = None
for _ in range(10):
    ext = find_ext()
    if ext:
        break
    time.sleep(0.3)
if not ext:
    print("EXT WINDOW NOT FOUND")
    p.kill()
    raise SystemExit(1)
print("ext hwnd:", ext, "visible:", bool(win32gui.IsWindowVisible(ext)))

tab = win32gui.GetDlgItem(ext, 1)
ms_list = win32gui.GetDlgItem(ext, 20)

# ---- page 0: multi-send ----
n0 = lv_count(ms_list)
win32gui.SendMessage(win32gui.GetDlgItem(ext, 22), 0x00F5, 0, 0)   # add row
time.sleep(0.3)
n1 = lv_count(ms_list)
print("P0 rows: %d -> %d  %s" % (n0, n1, "PASS" if n1 == n0 + 1 else "FAIL"))

# ---- page 1: calc linkage ----
tab_click(tab, 1, 3)
time.sleep(0.5)
dec = win32gui.GetDlgItem(ext, 2)
print("P1 DEC visible:", bool(win32gui.IsWindowVisible(dec)))
win32gui.SendMessage(dec, win32con.WM_SETTEXT, 0, "255")
time.sleep(0.5)
hexv = dlg_text(ext, 3)
ok1 = "FF" in hexv.upper()
print("P1 DEC=255 -> HEX [%s]  %s" % (hexv, "PASS" if ok1 else "FAIL"))

# ---- page 2: ascii ----
tab_click(tab, 2, 3)
time.sleep(0.5)
ascii_h = win32gui.GetDlgItem(ext, 13)
na = lv_count(ascii_h)
print("P2 ascii rows: %d  %s" % (na, "PASS" if na == 128 else "FAIL"))

# ---- resize follow ----
r0 = win32gui.GetWindowRect(tab)
w0 = r0[2] - r0[0]
win32gui.SetWindowPos(ext, 0, 0, 0, 620, 700, win32con.SWP_NOZORDER)
time.sleep(0.6)
r1 = win32gui.GetWindowRect(tab)
w1 = r1[2] - r1[0]
print("RESIZE tab width: %d -> %d  %s" % (w0, w1, "PASS" if w1 > w0 + 100 else "FAIL"))

# page 0 still fine
tab_click(tab, 0, 3)
time.sleep(0.5)
print("P0 rows after cycle:", lv_count(ms_list))

win32gui.PostMessage(h, win32con.WM_CLOSE, 0, 0)
time.sleep(0.5)
try:
    p.kill()
except Exception:
    pass
