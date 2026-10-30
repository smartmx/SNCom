# -*- coding: utf-8 -*-
# toggle_hex.py —— 点行 0 的 HEX 勾选框并截图（独立脚本）
import time
import os
import subprocess
import sys
import win32gui
import win32con
import win32api

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(BASE, "dist"))
p = subprocess.Popen([r"sncom.exe"], cwd=".")
h = None
for _ in range(30):
    h = win32gui.FindWindow("SNComMain", None)
    if h:
        break
    time.sleep(0.3)
time.sleep(0.5)
win32gui.SendMessage(win32gui.GetDlgItem(h, 3012), 0x00F5, 0, 0)   # 扩展弹窗
time.sleep(0.8)

exts = []
def cb(hw, _):
    if win32gui.GetClassName(hw) == "SNComExt" and win32gui.IsWindowVisible(hw):
        exts.append(hw)
win32gui.EnumWindows(cb, None)
ext = exts[0]
ms = win32gui.GetDlgItem(ext, 20)
col0_w = win32gui.SendMessage(ms, 0x101D, 0, 0)
col_w = win32gui.SendMessage(ms, 0x101D, 1, 0)
r = win32gui.GetWindowRect(ms)
x, y = r[0] + col0_w + col_w // 2, r[1] + 40
win32api.SetCursorPos((x, y))
win32api.mouse_event(2, 0, 0, 0, 0)
win32api.mouse_event(4, 0, 0, 0, 0)
time.sleep(0.5)
print("toggled HEX checkbox at", (x, y))

# 截图（PrintWindow，与 shot.ps1 同款）
import win32ui
import win32con
r = win32gui.GetWindowRect(ext)
dchw = win32gui.GetWindowDC(ext)
dc = win32ui.CreateDCFromHandle(dchw)
bmp = win32ui.CreateBitmap()
bmp.CreateCompatibleBitmap(dc, r[2] - r[0], r[3] - r[1])
odc = dc.CreateCompatibleDC()
odc.SelectObject(bmp)
odc.BitBlt((0, 0), (r[2] - r[0], r[3] - r[1]), dc, (0, 0), win32con.SRCCOPY)
out = os.path.join(BASE, "_probe", "ms_checkbox_toggled.png")
bmp.SaveBitmapFile(odc, out)
print("saved:", out)

win32gui.PostMessage(h, win32con.WM_CLOSE, 0, 0)
time.sleep(0.5)
try:
    p.kill()
except Exception:
    pass
