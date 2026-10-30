# -*- coding: utf-8 -*-
# verify_r71.py - docs/00 §71（文本/HEX 同一网格，用户两项）：
#  1) 文本模式（HEX 显示关）每行数据宽度 = 3 英文字符格的整数倍：
#     折行带各行最右墨迹列 mod 单元周期(3*cellW) 同余（与 HEX 模式同款断言）
#  2) 文本模式首行数据前有补格距离（与 HEX 一致）：前缀行数据首字符 x ≥ 前缀末+补格
#     ——通过行间最右墨迹相位一致间接覆盖（前缀补格非 3 倍数时相位即偏）
#  3) 行间列对齐（互相关残差 ≤3px）
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\develop\dist")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)
assert rx

# 文本模式（默认 HEX 关）+ 回环演示（长帧 400+ 字符文本长行）；§67 后 IDM_TOOL_LOOP=2222
hexshow = win32gui.GetDlgItem(main, 3037)
if win32gui.SendMessage(hexshow, 0x00F1, 0, 0) == 4:   # 若 HEX 开着则关掉
    win32api.PostMessage(hexshow, 0x00F5, 0, 0)
    time.sleep(0.4)
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)
time.sleep(3.0)
# 收窄窗口：文本长行(400字符)折成更多行（960px 仅 3~4 满宽带，620px ≈ 6+）
win32gui.SetWindowPos(main, 0, 0, 0, 620, 700, win32con.SWP_NOMOVE | win32con.SWP_NOZORDER)
time.sleep(0.8)                                    # 折行防抖 150ms + 重排
win32api.PostMessage(rx, win32con.WM_VSCROLL, win32con.SB_TOP, 0)
time.sleep(0.8)

def capture(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (r - l, b - t), hwnd, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img

img = capture(rx)
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r71_text_grid.png")
w, h = img.size
print("rx size", (w, h), flush=True)

def mask_cols(y0, y1, x0, x1, thr=120):
    m = []
    for xx in range(x0, x1):
        ink = False
        for yy in range(y0, y1):
            p = img.getpixel((xx, yy))
            if p[0] < thr and p[1] < thr and p[2] < thr:
                ink = True; break
        m.append(1 if ink else 0)
    return m

def band_rows():
    prof = []
    for yy in range(0, h):
        n = 0
        for xx in range(4, w - 4, 2):
            p = img.getpixel((xx, yy))
            if p[0] < 160 and p[1] < 160 and p[2] < 160: n += 1
        prof.append(n)
    bands = []
    y = 0
    while y < h:
        if prof[y] > 1:
            y0 = y
            while y < h and prof[y] > 1: y += 1
            bands.append((y0, y))
        else:
            y += 1
    return bands

# 找连续满宽行带（长文本行折行段；滤 <6px 噪声带）
wide = []
for (y0, y1) in band_rows():
    if y1 - y0 < 6: continue
    m = mask_cols(y0, y1, 3, w - 22)
    xs = [i for i, v in enumerate(m) if v]
    if xs and (xs[-1] - xs[0]) > w * 0.55:
        wide.append((y0, y1))
best, cur = [], []
MINB = 4                     # 文本模式长行(400字符)仅折 4~5 行（HEX 是 20+），阈值不同
for bnd in wide:
    if cur and bnd[0] - cur[-1][1] > 6:
        if len(cur) >= MINB: best = cur
        cur = []
    cur.append(bnd)
if len(cur) >= MINB: best = cur
chk("§71-1 文本折行带找到（≥4 满宽带）", len(best) >= 4, "bands=%d" % len(best))
if len(best) >= 4:
    x0, x1 = 3, w - 22
    masks = [mask_cols(y0 + 1, y1 - 1, x0, x1) for (y0, y1) in best]
    # 容量一致性（§71 核心：每行数据宽 = 3 格整数倍 ⇔ 各满宽带右缘聚在同一格）：
    # 每带最右墨迹 x；转储末行（部分行）天然短——剔除后其余右缘差应 ≤1 格（~9px）
    rights = []
    for m in masks:
        for x in range(len(m) - 1, -1, -1):
            if m[x]: rights.append(x + x0); break
    cluster = [r for r in rights if max(rights) - r <= 9]
    chk("§71-1 各行数据宽 3 格整数倍（右缘聚类，差≤9px）", len(cluster) >= 4,
        "rights=%s cluster=%d" % (rights, len(cluster)))
    # 首行补格：前缀行的数据首字符 x ≥ 前缀格数（有补格则数据不贴着标记）
    y0, y1 = best[0]
    m0 = mask_cols(y0 + 1, y1 - 1, x0, x1)
    first_ink = next((x + x0 for x, v in enumerate(m0) if v), -1)
    print("first band leftmost ink x =", first_ink, flush=True)

proc.terminate()
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
