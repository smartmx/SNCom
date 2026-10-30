# -*- coding: utf-8 -*-
# verify_r47_blink.py - is the 16-px screen-level diff in the collapsed state a blink (animation)
# rather than stale content? 5 consecutive screen frames, no intervention, then a forced repaint.
import os, time, subprocess
import win32gui, win32con, win32ui, win32process
from PIL import Image, ImageChops
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT = os.path.join(HERE, "out")
REDRAW = 0x0001 | 0x0004 | 0x0080 | 0x0100
def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
def screen_shot(rect):
    l, t, r, b = rect; w, h = r - l, b - t
    hdesk = win32gui.GetWindowDC(0)
    mfc = win32ui.CreateDCFromHandle(hdesk); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h); dc.SelectObject(bmp)
    dc.BitBlt((0, 0), (w, h), mfc, (l, t), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(0, hdesk)
    return img
def d(a, b, tag):
    diff = ImageChops.difference(a, b).convert("L")
    bb = diff.getbbox(); npx = sum(1 for p in diff.getdata() if p > 8)
    print("%-26s identical=%-5s bbox=%-22s changed=%d" % (tag, bb is None, str(bb), npx), flush=True)
    return npx
proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
time.sleep(3.0)
rect = win32gui.GetWindowRect(main)
frames = []
for i in range(5):
    frames.append(screen_shot(rect)); time.sleep(0.2)
for i in range(1, 5):
    d(frames[0], frames[i], "t0 vs t%d" % i)
before = frames[-1]
win32gui.SendMessage(main, win32con.WM_SETREDRAW, 1, 0)
win32gui.RedrawWindow(main, None, None, REDRAW)
time.sleep(0.6)
after = screen_shot(rect)
d(before, after, "forced repaint")
frames[0].crop((0, 40, 200, 90)).resize((400, 100), Image.LANCZOS).save(os.path.join(OUT, "r47_blink_t0.png"))
frames[2].crop((0, 40, 200, 90)).resize((400, 100), Image.LANCZOS).save(os.path.join(OUT, "r47_blink_t2.png"))
proc.terminate()
