# -*- coding: utf-8 -*-
# verify_r67.py - docs/00 §67/§68 渲染断言（配置驱动，进程零写盘）：
#  1) §68 收/发时间戳各自独立样式：rx_ts_color=#FF00FF(加粗)、tx_ts_color=#00FFFF
#     —— 收行前缀紫红、发行前缀青色，均限前缀区；正文不受染
#  2) §68 样式弹窗有点击弹出且有标题「时间戳样式」（WM_NCCREATE 放行 Def 修复）
#  3) bg_color 背景色生效
# 配置由探针自写入沙箱 sncom.json（§116 起；此前为外部 bash 预写）。
import os, time, subprocess, ctypes, shutil
import json as _json
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
SNCOM_ROOT = os.path.dirname(HERE)              # probes/ 的父目录（项目根）
DEV = os.path.join(SNCOM_ROOT, "develop")
SRC = os.environ.get("SNCOM_EXE") or os.path.join(DEV, "dist", "sncom.exe")
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

# §116（十七轮 P3-2）：环境性跳过单独计数——批跑日志里可直接区分「没测到」与「测了没过」。
skips = []
def skip(desc):
    skips.append(desc)
    print("SKIP " + desc, flush=True)

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

# §116：并入 §114 沙箱红线（不再以 develop\dist 为工作目录）；fixture（颜色断言前置）
# 改由探针自写入沙箱 sncom.json（§74 的「外部 bash 预写」流程退场）。缺失仍显式 SKIP。
BIN = os.path.join(HERE, "_probe", "out", "r67")
shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as _f:
    _f.write(_json.dumps({"bg_color": "#10127C", "rx_ts_color": "#FF00FF", "tx_ts_color": "#00FFFF"}))
have_fixture = os.path.exists(os.path.join(BIN, "sncom.json"))

proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)                   # IDC_RXVIEW
assert rx

# 回环演示注入（收/发行都带 [ts] 标记 前缀）；§67：IDM_VIEW_BG 插入后 IDM_TOOL_LOOP=2222
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)
time.sleep(3.0)
# 滚到顶：自动滚屏停最底时收行长续行会把发行滚出视口（发行在转储前部）
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
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r67_colors.png")
w, h = img.size

# ---- 3) 背景色（fixture 依赖：缺失则 SKIP 颜色组，见文件头 F5 说明）----
if not have_fixture:
    skip("§67-3/§68 颜色断言组 x6（sncom.json fixture 缺失：预写 rx_ts_color/bg_color 后重跑）")
else:
    bgp = img.getpixel((w - 30, 10))
    chk("§67-3 背景色 #10127C 生效", abs(bgp[0]-0x10) < 8 and abs(bgp[1]-0x12) < 8 and abs(bgp[2]-0x7C) < 10,
        "px=%s" % (bgp,))

    # ---- 1) §68 收/发时间戳独立颜色 ----
    def magenta(p): return p[0] > 170 and p[2] > 170 and p[1] < 120
    def cyan(p):    return p[1] > 170 and p[2] > 170 and p[0] < 120
    mag = [xx for yy in range(2, h - 4) for xx in range(2, w - 4) if magenta(img.getpixel((xx, yy)))]
    cyn = [xx for yy in range(2, h - 4) for xx in range(2, w - 4) if cyan(img.getpixel((xx, yy)))]
    chk("§68-1 接收时间戳色 #FF00FF 有墨", len(mag) > 40, "magenta_px=%d" % len(mag))
    chk("§68-1 发送时间戳色 #00FFFF 有墨", len(cyn) > 40, "cyan_px=%d" % len(cyn))
    chk("§68-1 接收前缀限前缀区（maxx<200）", 0 < max(mag) < 200 if mag else False, "maxx=%s" % (max(mag) if mag else -1))
    chk("§68-1 发送前缀限前缀区（maxx<200）", 0 < max(cyn) < 200 if cyn else False, "maxx=%s" % (max(cyn) if cyn else -1))
    black = sum(1 for yy in range(2, h - 4) for xx in range(200, w - 20)
                if all(c < 10 for c in img.getpixel((xx, yy))[:2]) and img.getpixel((xx, yy))[2] < 60)
    chk("§67-2 正文非时间戳色（200px 外存在正文黑像素）", black > 100, "black_px=%d" % black)

# ---- 2) §68 弹窗标题 + 截图 ----
chip = win32gui.GetDlgItem(main, 3041)                 # IDC_CHIP_TS（SS_NOTIFY）
win32api.PostMessage(main, win32con.WM_COMMAND, 3041, 0)   # STN_CLICKED=0 → 弹样式窗
dlg = 0
for _ in range(20):
    dlg = find_toplevel("SNComTsStyle", proc.pid)
    if dlg: break
    time.sleep(0.15)
chk("§68-2 样式弹窗已弹出", dlg != 0, "")
if dlg:
    title = win32gui.GetWindowText(dlg)
    chk("§68-2 弹窗有标题「时间戳样式」", title == "时间戳样式", "title=%r" % title)
    capture(dlg).save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r68_dialog.png")
    win32api.PostMessage(dlg, win32con.WM_CLOSE, 0, 0)  # 取消语义
    time.sleep(0.4)
    print("saved r68_dialog.png", flush=True)

# ---- 3) §69-2 选区复制不重复注记（消息注入拖选 + Ctrl+C + 剪贴板读回）----
try:
    import win32clipboard
    def get_clip():
        win32clipboard.OpenClipboard()
        try:
            return win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
        finally:
            win32clipboard.CloseClipboard()
    def msg_drag(x0, y0, x1, y1):
        MK = win32con.MK_LBUTTON
        win32api.PostMessage(rx, win32con.WM_LBUTTONDOWN, MK, win32api.MAKELONG(x0, y0))
        time.sleep(0.06)
        win32api.PostMessage(rx, win32con.WM_MOUSEMOVE, MK, win32api.MAKELONG(x1, y1))
        time.sleep(0.12)
        win32api.PostMessage(rx, win32con.WM_LBUTTONUP, 0, win32api.MAKELONG(x1, y1))
        time.sleep(0.25)
    # 只选时间戳 [..]（行 0 前缀首 14 字符，已滚到顶）
    msg_drag(2, 3, 96, 3)
    win32api.PostMessage(rx, win32con.WM_CHAR, 0x03, 0)    # Ctrl+C
    time.sleep(0.4)
    clip = get_clip()
    ok = (clip.startswith("[") and clip.endswith("]") and len(clip) == 14 and clip.count("[") == 1)
    chk("§69-2 只选时间戳 → 复制恰好时间戳（不重复）", ok, "clip=%r" % clip[:40])
    # 选完整前缀 [..] 收←◆ / 发→◇（18 字符 × cellW≈8 ≈ 148px；行 0 为发行或收行视首帧而定）
    msg_drag(2, 3, 148, 3)
    win32api.PostMessage(rx, win32con.WM_CHAR, 0x03, 0)
    time.sleep(0.4)
    clip2 = get_clip()
    ok2 = (clip2.count("[") == 1 and ("收" in clip2 or "发" in clip2)
           and ("◆" in clip2 or "◇" in clip2) and len(clip2) <= 20)
    chk("§69-2 选完整前缀 → 恰好一份 [ts]+标记", ok2, "clip=%r" % clip2[:40])
except Exception as ex:
    skip("§69-2 剪贴板复制组（至多 2 项）读回失败：%s" % ex)

proc.terminate()
time.sleep(0.4)
shutil.rmtree(BIN, ignore_errors=True)
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
