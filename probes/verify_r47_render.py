# -*- coding: utf-8 -*-
# verify_r47_render.py - characterise the residual screen-level diff after a forced repaint:
# is the forced-repaint rendering itself stable (B==C) while incremental paint (A) differs?
import os, time, subprocess
import win32gui, win32con, win32ui, win32process
from PIL import Image, ImageChops
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
REDRAW = 0x0001 | 0x0004 | 0x0080 | 0x0100
ID_COLLAPSE = 3009
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
def d(a, b, tag, save=None):
    diff = ImageChops.difference(a, b).convert("L")
    bb = diff.getbbox(); npx = sum(1 for p in diff.getdata() if p > 8)
    print("%-22s identical=%-5s bbox=%-22s changed=%d" % (tag, bb is None, str(bb), npx), flush=True)
    if bb and save:
        x0, y0, x1, y1 = bb
        c = a.crop((max(0, x0 - 6), max(0, y0 - 6), min(a.size[0], x1 + 6), min(a.size[1], y1 + 6)))
        c = c.resize((c.size[0] * 4, c.size[1] * 4), Image.NEAREST)
        c.save(os.path.join(OUT, save))
    return npx
proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
time.sleep(3.0)
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_COLLAPSE), 0x00F5, 0, 0)   # collapse
time.sleep(1.2)
rect = win32gui.GetWindowRect(main)
A = ss(rect)
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.6)
B = ss(rect)
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.6)
C = ss(rect)
d(A, B, "incremental vs full#1", "r47_render_AB.png")
d(B, C, "full#1 vs full#2", "r47_render_BC.png")
d(A, C, "incremental vs full#2", "r47_render_AC.png")
A.save(os.path.join(OUT, "r47_render_A.png")); C.save(os.path.join(OUT, "r47_render_C.png"))
proc.terminate()
