# -*- coding: utf-8 -*-
# verify_r41_s8c.py - is child painting also suppressed after the collapse leak?
# method: trigger a status-bar text change (Notify) while collapsed, then force a
# full repaint and compare. Same experiment repeated expanded as a control group.
import os, time, ctypes, subprocess
import win32gui, win32con, win32ui, win32process
from PIL import Image, ImageChops
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
user32 = ctypes.windll.user32
REDRAW = 0x0001 | 0x0004 | 0x0080 | 0x0100
ID_CLEAR, ID_COLLAPSE = 3000, 3009

def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

def shot(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    w, h = r - l, b - t
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (w, h), mfc, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img

def report(tag, a, b):
    d = ImageChops.difference(a, b).convert("L")
    bb = d.getbbox(); npx = sum(1 for p in d.getdata() if p > 8)
    print("%-34s identical=%s bbox=%s changed=%d" % (tag, bb is None, bb, npx), flush=True)
    if bb:
        # status bar band = bottom 30 px
        sb = d.crop((0, d.size[1] - 30, d.size[0], d.size[1]))
        n_sb = sum(1 for p in sb.getdata() if p > 8)
        print("    status-bar band changed pixels = %d" % n_sb, flush=True)
    return bb

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
clear = win32gui.GetDlgItem(main, ID_CLEAR)
collapse = win32gui.GetDlgItem(main, ID_COLLAPSE)

# --- control group: EXPANDED ---
win32gui.PostMessage(clear, 0x00F5, 0, 0)      # BM_CLICK -> Notify(...) in status bar
time.sleep(0.5)
E1 = shot(main)
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.5)
E2 = shot(main)
report("control(expanded) notify vs forced repaint", E1, E2)
E1.save(os.path.join(OUT, "r41_s8c_expanded_before.png")); E2.save(os.path.join(OUT, "r41_s8c_expanded_after.png"))

# --- test group: COLLAPSED ---
win32gui.PostMessage(collapse, 0x00F5, 0, 0)   # collapse -> leaks WM_SETREDRAW(FALSE)
time.sleep(1.0)
win32gui.PostMessage(clear, 0x00F5, 0, 0)      # notify while collapsed
time.sleep(0.6)
C1 = shot(main)
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.6)
C2 = shot(main)
report("test(collapsed) notify vs forced repaint", C1, C2)
C1.save(os.path.join(OUT, "r41_s8c_collapsed_before.png")); C2.save(os.path.join(OUT, "r41_s8c_collapsed_after.png"))
C1.crop((0, C1.size[1] - 40, C1.size[0], C1.size[1])).save(os.path.join(OUT, "r41_s8c_collapsed_before_statusbar.png"))
C2.crop((0, C2.size[1] - 40, C2.size[0], C2.size[1])).save(os.path.join(OUT, "r41_s8c_collapsed_after_statusbar.png"))
proc.terminate()
