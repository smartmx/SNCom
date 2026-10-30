# -*- coding: utf-8 -*-
# verify_r57.py - docs/00 §57: find-toggle gray-flash.
# Click 查找 asynchronously (PostMessage) and continuously capture the RxView screen
# region. A flash = the RxView area washed in COLOR_BTNFACE gray (the RDW_ERASE step).
# Metric: per-frame fraction of btnface-gray(240±6) pixels inside the RxView rect.
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))

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
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)               # IDC_RXVIEW
l, t, r, b = win32gui.GetWindowRect(rx)
W, H = r - l, b - t
print("rxview rect %dx%d" % (W, H), flush=True)

# 采样区加固（P3-6/r41 同款）：窗口置 TOPMOST 再抓屏——桌面任何灰窗（宿主窗/残留工具窗）
# 叠在 rxview 屏幕矩形上都会把灰占比抬高到 0.3+（实测 2026-10-27 三跑两 FAIL，置顶后 0.000）
user32.SetWindowPos(main, -1, 0, 0, 0, 0, 0x3)     # HWND_TOPMOST, SWP_NOMOVE|NOSIZE
time.sleep(0.3)

sdc = user32.GetDC(0)
mdc = win32ui.CreateDCFromHandle(sdc).CreateCompatibleDC()
bmp = win32ui.CreateBitmap()
bmp.CreateCompatibleBitmap(win32ui.CreateDCFromHandle(sdc), W, H)

def grab():
    old = mdc.SelectObject(bmp)
    gdi32.BitBlt(mdc.GetSafeHdc(), 0, 0, W, H, sdc, l, t, 0x00CC0020)
    mdc.SelectObject(old)
    bits = bmp.GetBitmapBits(True)
    return Image.frombuffer("RGBA", (W, H), bits, "raw", "BGRA", 0, 1).convert("RGB")

def gray_frac(img):
    px = img.getdata()
    n = 0
    tot = 0
    for p in px:
        tot += 1
        if abs(p[0] - 240) <= 6 and abs(p[1] - 240) <= 6 and abs(p[2] - 240) <= 6:
            n += 1
    return n / max(tot, 1)

worst = 0.0
frames_checked = 0
for cycle in range(6):                              # 开→关 3 个来回
    win32api.PostMessage(win32gui.GetDlgItem(main, 3001), 0x00F5, 0, 0)   # BM_CLICK 查找
    for _ in range(12):
        f = grab()
        worst = max(worst, gray_frac(f))
        frames_checked += 1
    time.sleep(0.4)
    win32api.PostMessage(win32gui.GetDlgItem(main, 3001), 0x00F5, 0, 0)   # 关闭
    for _ in range(12):
        f = grab()
        worst = max(worst, gray_frac(f))
        frames_checked += 1
    time.sleep(0.4)

print("frames=%d  worst gray-fraction in rxview = %.3f" % (frames_checked, worst), flush=True)
chk = worst < 0.15
print("RESULT %s (threshold 0.15)" % ("PASS" if chk else "FAIL"), flush=True)
user32.SetWindowPos(main, -2, 0, 0, 0, 0, 0x3)     # HWND_NOTOPMOST 还原
win32gui.DeleteObject(bmp.GetHandle()); mdc.DeleteDC(); user32.ReleaseDC(0, sdc)
proc.terminate()
