# -*- coding: utf-8 -*-
# verify_r76.py - docs/00 §76（用户两项）：
#  1) BRK 单独状态框移除，BREAK 复选框并入状态显示（ownerdraw）：
#     - IDC_PIN_BREAK(3081) 不存在；引脚行仍 5 框（DCD..ERR）
#     - chk_break_ 为 BS_OWNERDRAW：BM_CLICK 手动翻转勾选态（ownerdraw 不自动翻）
#  2) 工具行整行下移 3 设计 px：按钮底缘→面板首行控件顶距 ≈ panelPadY(6)（原 rowGap+padY=9）
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

# ---- 1) BRK 框移除 + BREAK ownerdraw 翻转 ----
chk("§76-1 BRK 状态框已移除（id 3081 无控件）", user32.GetDlgItem(main, 3081) == 0, "")
pins = [i for i in range(3031, 3036) if user32.GetDlgItem(main, i)]   # DCD..ERR
chk("§76-1 引脚行仍 5 框（DCD/DSR/CTS/RI/ERR）", len(pins) == 5, "pins=%s" % pins)
brk = win32gui.GetDlgItem(main, 3036)                 # IDC_CHK_BREAK
style = win32gui.GetWindowLong(brk, win32con.GWL_STYLE)
is_ownerdraw = (style & 0x0B) == 0x0B                 # BS_OWNERDRAW = 0x0B
chk("§76-1 BREAK 为 BS_OWNERDRAW", is_ownerdraw, "style=0x%X" % style)

def brk_pix():
    """chk_break_ 区域像素（ownerdraw 无 BM_*CHECK 可读——以渲染为准验证翻转）"""
    l, t, r, b = win32gui.GetWindowRect(main)
    hdc = win32gui.GetWindowDC(main)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(main, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
    bl, bt, brr, bb = win32gui.GetWindowRect(brk)
    return img.crop((bl - l, bt - t, brr - l, bb - t)).tobytes()

p0 = brk_pix()
win32api.PostMessage(brk, 0x00F5, 0, 0)               # BM_CLICK → 自持态翻转（画勾）
time.sleep(0.4)
p1 = brk_pix()
win32api.PostMessage(brk, 0x00F5, 0, 0)               # 再点击 → 还原
time.sleep(0.4)
p2 = brk_pix()
chk("§76-1 点击翻转渲染（勾下/勾上）", p0 != p1 and p0 == p2, "pix same=%s" % (p0 == p1))

# ---- 2) 工具行下移：按钮底缘→面板首行（组合框顶）≈ 6 设计 px（原 rowGap+padY=9）----
bc = win32gui.GetWindowRect(win32gui.GetDlgItem(main, 3000))   # 清除窗口
pc = win32gui.GetWindowRect(win32gui.GetDlgItem(main, 3014))   # 端口号组合框（面板首行）
gap = pc[1] - bc[3]
chk("§76-2 工具行下移 3px（按钮底→面板首行 ≈6px）", 3 <= gap <= 8, "gap=%d（原≈9）" % gap)

# 截图
l, t, r, b = win32gui.GetWindowRect(main)
hdc = win32gui.GetWindowDC(main)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
user32.PrintWindow(main, dc.GetSafeHdc(), 2)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
img.crop((0, t and 0 or 0, r - l, b - t)).save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r76_main.png")
print("saved r76_main.png", flush=True)

proc.terminate()
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
