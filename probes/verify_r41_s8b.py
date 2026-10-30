# -*- coding: utf-8 -*-
# verify_r41_s8b.py - screen-level confirmation of the collapsed-path redraw leak
import os, time, ctypes, subprocess
import win32gui, win32con, win32api, win32ui
from PIL import Image, ImageChops
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
REDRAW = 0x0001 | 0x0004 | 0x0080 | 0x0100
ID_COLLAPSE = 3009

def find_main(pid):
    out = []
    import win32process
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

def screen_shot(rect):
    l, t, r, b = rect
    w, h = r - l, b - t
    hdesk = win32gui.GetWindowDC(0)
    mfc = win32ui.CreateDCFromHandle(hdesk)
    dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h)
    dc.SelectObject(bmp)
    dc.BitBlt((0, 0), (w, h), mfc, (l, t), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(0, hdesk)
    return img

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
try:
    win32gui.SetForegroundWindow(main)
except Exception as e:
    print("SetForegroundWindow skipped:", e)
time.sleep(0.5)
rect = win32gui.GetWindowRect(main)
collapse = win32gui.GetDlgItem(main, ID_COLLAPSE)
win32gui.PostMessage(collapse, 0x00F5, 0, 0)
time.sleep(2.0)
S1 = screen_shot(rect); S1.save(os.path.join(OUT, "r41_s8_screen_before.png"))
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.8)
S2 = screen_shot(rect); S2.save(os.path.join(OUT, "r41_s8_screen_after.png"))
d = ImageChops.difference(S1, S2).convert("L")
bb = d.getbbox(); npx = sum(1 for p in d.getdata() if p > 8)
print("SCREEN-LEVEL collapsed main: identical=%s bbox=%s changed pixels>8=%d" % (bb is None, bb, npx), flush=True)
d.point(lambda v: 255 if v > 8 else 0).save(os.path.join(OUT, "r41_s8_screen_diff.png"))
proc.terminate()
