# -*- coding: utf-8 -*-
# verify_r47_corner.py - what is the coloured speck at the receive view's origin?
import os, time, subprocess
import win32gui, win32con, win32ui, win32process
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
def ss(rect):
    l, t, r, b = rect; w, h = r - l, b - t
    hdesk = win32gui.GetWindowDC(0)
    mfc = win32ui.CreateDCFromHandle(hdesk); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h); dc.SelectObject(bmp)
    dc.BitBlt((0, 0), (w, h), mfc, (l, t), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(0, hdesk)
    return img
proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
m = 0
for _ in range(40):
    m = find_main(proc.pid)
    if m: break
    time.sleep(0.25)
time.sleep(3.0)
win32gui.SetWindowPos(m, win32con.HWND_TOPMOST, 0, 0, 0, 0, win32con.SWP_NOMOVE | win32con.SWP_NOSIZE | win32con.SWP_NOACTIVATE)
time.sleep(1.0)
img = ss(win32gui.GetWindowRect(m)); img.save(os.path.join(OUT, "r47_corner_expanded_full.png"))
c = img.crop((0, 40, 260, 110)).resize((260 * 3, 70 * 3), Image.LANCZOS)
c.save(os.path.join(OUT, "r47_corner_expanded.png"))
print("expanded idle corner saved", flush=True)
proc.terminate()
