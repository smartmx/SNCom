# -*- coding: utf-8 -*-
# verify_r41_s8d.py - 2x2 matrix: {expanded, collapsed} x {drift, intervention}
# separates real repaint suppression from frame-to-frame animation noise (docs/00 42 claim)
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
    print("%-38s identical=%-5s bbox=%-24s changed=%d" % (tag, bb is None, str(bb), npx), flush=True)
    return npx

def state(main, tag):
    # drift: two frames, no intervention, 250 ms apart
    a = shot(main); time.sleep(0.25); b = shot(main)
    n_drift = report(tag + "  drift(no input)", a, b)
    # intervention: notify then forced repaint
    win32gui.PostMessage(win32gui.GetDlgItem(main, ID_CLEAR), 0x00F5, 0, 0)
    time.sleep(0.5)
    c = shot(main)
    win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
    win32gui.RedrawWindow(main, None, None, REDRAW)
    time.sleep(0.5)
    d = shot(main)
    n_int = report(tag + "  notify vs forced repaint", c, d)
    c.save(os.path.join(OUT, "r41_s8d_%s_before.png" % tag)); d.save(os.path.join(OUT, "r41_s8d_%s_after.png" % tag))
    return n_drift, n_int

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
time.sleep(0.6)
print("EXPANDED:"); de, ie = state(main, "expanded")
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_COLLAPSE), 0x00F5, 0, 0)   # collapse
time.sleep(1.0)
print("COLLAPSED:"); dc_, ic = state(main, "collapsed")
print("RESULT: expanded drift=%d intervention=%d | collapsed drift=%d intervention=%d" % (de, ie, dc_, ic), flush=True)
proc.terminate()
