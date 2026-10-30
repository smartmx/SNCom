# -*- coding: utf-8 -*-
# probe_selftest.py - run --spike-selftest and screenshot the status bar (pointer-free evidence)
import os, time, ctypes, subprocess
import win32gui, win32con, win32ui, win32process
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
user32 = ctypes.windll.user32
def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
def shot(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd); w, h = r - l, b - t
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (w, h), mfc, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img
p = subprocess.Popen([os.path.join(DIST, "sncom.exe"), "--spike-selftest"], cwd=DIST)
m = 0
for _ in range(40):
    m = find_main(p.pid)
    if m: break
    time.sleep(0.25)
assert m, "no main window"
time.sleep(6.0)
img = shot(m)
img.save(os.path.join(OUT, "selftest_full.png"))
sb = img.crop((0, img.size[1] - 26, img.size[0], img.size[1]))
sb = sb.resize((sb.size[0] * 2, sb.size[1] * 2), Image.LANCZOS)
sb.save(os.path.join(OUT, "selftest_statusbar.png"))
print("selftest window size:", img.size, "-> saved selftest_full.png / selftest_statusbar.png", flush=True)
p.terminate()
