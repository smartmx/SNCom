# -*- coding: utf-8 -*-
# verify_r41_s8.py - isolate the WM_SETREDRAW leak on the collapsed path (docs 33)
import os, time, ctypes, subprocess, hashlib
import win32gui, win32con, win32api, win32ui
from PIL import Image, ImageChops
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
user32 = ctypes.windll.user32
REDRAW = 0x0001 | 0x0004 | 0x0080 | 0x0100
ID_COLLAPSE = 3009

def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain":
            import win32process
            if win32process.GetWindowThreadProcessId(h)[1] == pid:
                out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

def shot(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    w, h = r - l, b - t
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc)
    dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h)
    dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (w, h), mfc, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img

def diffbox(a, b, tag):
    d = ImageChops.difference(a, b).convert("L")
    bb = d.getbbox()
    npx = sum(1 for p in d.getdata() if p > 8)
    print("%-28s ident=%s bbox=%s pixels>8=%d" % (tag, bb is None, bb, npx), flush=True)
    d.point(lambda v: 255 if v > 8 else 0).save(os.path.join(OUT, "r41_s8_diff_%s.png" % tag))
    return bb, npx

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
assert main, "no main window"
time.sleep(3.0)
collapse = win32gui.GetDlgItem(main, ID_COLLAPSE)
win32gui.PostMessage(collapse, 0x00F5, 0, 0)     # BM_CLICK
time.sleep(1.6)
A1 = shot(main); A1.save(os.path.join(OUT, "r41_s8_collapsedA1.png"))
time.sleep(1.6)
A2 = shot(main); A2.save(os.path.join(OUT, "r41_s8_collapsedA2.png"))
diffbox(A1, A2, "drift_A1_A2")
# nothing but the leaked flag distinguishes A2 from a forced full repaint
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.8)
B = shot(main); B.save(os.path.join(OUT, "r41_s8_collapsedB.png"))
diffbox(A2, B, "repaint_A2_B")
# now expand again and confirm the app recovers (redraw flag restored on that path)
win32gui.PostMessage(collapse, 0x00F5, 0, 0)
time.sleep(0.8)
C = shot(main); C.save(os.path.join(OUT, "r41_s8_expandedC.png"))
print("recovered after expand:", C.size, flush=True)
proc.terminate()
