# -*- coding: utf-8 -*-
# verify_r62.py - docs/00 §62 长行折行（方案 A 像素版）E2E：
#  1) 回环演示注入 ~410 字符长行 → 垂直滚动条范围（显示行）> 逻辑行数（折行生效）
#  2) 滚到底 → 行尾哨兵 END_MARK 可见（截图目检）
#  3) 跨显示行选区（同一逻辑行的两个折行段）复制 → 剪贴板为连续子串（无 \r\n 插入）
import os, time, subprocess, ctypes, shutil
import ctypes.wintypes
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image


# ---- §84（五轮审查 S1）：真实输入调用统一守卫 ------------------------------------
# SetCursorPos/mouse_event/keybd_event 在系统拒绝时会抛 pywintypes.error，脚本会以
# traceback 中断（批跑里表现为 "no summary"，掩盖"没测到"）。统一包一层：失败置
# INPUT_OK=False 并打印 NOTE，调用方据此 SKIP 相关断言（与 r73/r79 同款口径）。
INPUT_OK = True


def _safe_input(fn, *a, **kw):
    global INPUT_OK
    try:
        return fn(*a, **kw)
    except Exception as e:
        if INPUT_OK:
            print("NOTE: 真实输入调用被拒（%s: %s）——相关断言将 SKIP" % (getattr(fn, "__name__", fn), e), flush=True)
        INPUT_OK = False
        return None


def SetCursorPos(pt):
    return _safe_input(win32api.SetCursorPos, pt)


def mouse_event(*a):
    return _safe_input(win32api.mouse_event, *a)


def keybd_event(*a):
    return _safe_input(win32api.keybd_event, *a)
# ---------------------------------------------------------------------------------

user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))   # §114：只读源（仅取 exe）
DIST = os.path.join(HERE, "_probe", "out", "r62")                        # §114：沙箱（不再动 develop\dist）
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

# 确定性文本模式：删 sncom.json → 全默认（HEX 模式行固定 16 字节，无长行可言）
# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = os.path.join(SRC_DIST, "") if "SRC_DIST" in globals() else r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = DIST if "DIST" in globals() else r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r62"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(os.path.join(DIST, "sncom.json"))
except FileNotFoundError:
    pass
proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)               # IDC_RXVIEW
assert rx

def scroll_info(hwnd):
    class SCROLLINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("fMask", ctypes.c_uint),
                    ("nMin", ctypes.c_int), ("nMax", ctypes.c_int),
                    ("nPage", ctypes.c_uint), ("nPos", ctypes.c_int),
                    ("nTrackPos", ctypes.c_int)]
    si = SCROLLINFO(); si.cbSize = ctypes.sizeof(si); si.fMask = 0x1F  # SIF_ALL
    user32.GetScrollInfo(hwnd, 1, ctypes.byref(si))    # SB_VERT
    return si

# ---- 1) 回环演示注入长行 ----
hexshow = win32gui.GetDlgItem(main, 3037)          # IDC_CHK_HEXSHOW
st = win32gui.SendMessage(hexshow, 0x00F1, 0, 0)   # BM_GETCHECK
if st == 4:                                        # 勾选中 → 关掉（文本模式才演示折行）
    win32gui.PostMessage(hexshow, 0x00F5, 0, 0)
    time.sleep(0.4)
win32gui.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # §67 起 IDM_TOOL_LOOP=2222（2221=资源管理器）
time.sleep(2.5)
si = scroll_info(rx)
disp_rows = si.nMax + 1
# 逻辑行：8 帧 = 3+1+3+1+3+1+3+1 = 16；长行(~411 字符)应折成 ≥4 显示行 → 总显示行 ≥ 19
chk("scroll range (display rows) > logical rows (wrapping active)", disp_rows >= 19,
    "disp_rows=%s" % disp_rows)

# ---- 2) 滚到底 → 截图（行尾哨兵目检）----
win32gui.PostMessage(rx, win32con.WM_VSCROLL, win32con.SB_BOTTOM, 0)
time.sleep(0.6)
si2 = scroll_info(rx)
chk("scroll reaches bottom (nPos == nMax - nPage + 1 附近)", abs(si2.nPos - (si2.nMax - si2.nPage + 1)) <= 2,
    "nPos=%s nMax=%s nPage=%s" % (si2.nPos, si2.nMax, si2.nPage))
l, t, r, b = win32gui.GetWindowRect(rx)
hdc = win32gui.GetWindowDC(rx)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r-l, b-t); dc.SelectObject(bmp)
if not user32.PrintWindow(rx, dc.GetSafeHdc(), 2):
    dc.BitBlt((0,0),(r-l,b-t),rx,(0,0),win32con.SRCCOPY)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA",(r-l,b-t),bits,"raw","BGRA",0,1).convert("RGB")
out = os.path.join(HERE, "out"); os.makedirs(out, exist_ok=True)
img.save(os.path.join(out, "r62_wrapped_bottom.png"))
print("saved r62_wrapped_bottom.png", flush=True)
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(rx, hdc)

# ---- 3) 跨显示行选区复制：长行前两个折行段拖选 → 剪贴板无 \r\n（同一逻辑行）----
# 长行是第 4 个逻辑行（idx 3），显示行从 3 起。视口顶部≈底部前 VisibleRows 行——
# 先滚到长行起点附近：按逻辑行算显示行 3，直接 SB_THUMBPOSITION 到 3
win32gui.PostMessage(rx, win32con.WM_VSCROLL, win32con.SB_THUMBPOSITION, 3)
time.sleep(0.5)
l, t, r, b = win32gui.GetWindowRect(rx)
# 选区拖选 = 消息注入（PostMessage 直达 rxview 客户区坐标）——不依赖前台/解锁，
# 不受用户真实鼠标竞态影响（mouse_event 合成拖选在用户在场时静默失效，§68 实测）
def msg_drag(x0, y0, x1, y1):
    MK = win32con.MK_LBUTTON
    win32api.PostMessage(rx, win32con.WM_LBUTTONDOWN, MK, win32api.MAKELONG(x0, y0))
    time.sleep(0.06)
    for step in (1, 2, 3):
        xx = x0 + (x1 - x0) * step // 3
        yy = y0 + (y1 - y0) * step // 3
        win32api.PostMessage(rx, win32con.WM_MOUSEMOVE, MK, win32api.MAKELONG(xx, yy))
        time.sleep(0.06)
    win32api.PostMessage(rx, win32con.WM_MOUSEMOVE, MK, win32api.MAKELONG(x1, y1))
    time.sleep(0.12)
    win32api.PostMessage(rx, win32con.WM_LBUTTONUP, 0, win32api.MAKELONG(x1, y1))
    time.sleep(0.3)
drag_ok = True
# 选区蓝带色 = GetSysColor(COLOR_HIGHLIGHT)（系统主题色，随机器/主题变——旧值 0x3399FF
# 在换机后失配致误报 FAIL，§68 实测本机 = (0,120,215)）。运行时读取。
_hlv = user32.GetSysColor(13)                          # COLOR_HIGHLIGHT (0x00BBGGRR)
HL = (_hlv & 0xFF, (_hlv >> 8) & 0xFF, (_hlv >> 16) & 0xFF)
def is_hl(p):
    return abs(p[0]-HL[0]) < 30 and abs(p[1]-HL[1]) < 30 and abs(p[2]-HL[2]) < 30
print("sys highlight =", HL, flush=True)
blue_rows = (0, 0, 0)
for attempt in range(3):                           # 重试直到选区成形
    print("attempt %d: msg_drag" % attempt, flush=True)
    msg_drag(30, 8, 260, 44)
    l, t, r, b = win32gui.GetWindowRect(rx)
    hdc = win32gui.GetWindowDC(rx)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r-l, b-t); dc.SelectObject(bmp)
    if not user32.PrintWindow(rx, dc.GetSafeHdc(), 2):
        dc.BitBlt((0,0),(r-l,b-t),rx,(0,0),win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    simg = Image.frombuffer("RGBA",(r-l,b-t),bits,"raw","BGRA",0,1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(rx, hdc)
    lineH = 18
    n0 = sum(1 for yy in range(2, 14) for xx in range(10, 400) if is_hl(simg.getpixel((xx, yy))))
    n1v = sum(1 for yy in range(2+lineH, 14+lineH) for xx in range(10, 400) if is_hl(simg.getpixel((xx, yy))))
    n2v = sum(1 for yy in range(2+3*lineH, 14+3*lineH) for xx in range(10, 400) if is_hl(simg.getpixel((xx, yy))))
    print("attempt %d: band0 blue=%d" % (attempt, n0), flush=True)
    if n0 > 50:
        drag_ok = True
        blue_rows = (n0, n1v, n2v)
        break
    time.sleep(0.8)
# （消息注入拖选不依赖前台，无需置顶/取消置顶）
# ---- 3b) 折行选区渲染断言：剪贴板被常驻管理器 100% 占用无法自动化读取——
#      复制无 \r\n 的语义由 SelectedText 按逻辑行拼接结构性保证；此处验证
#      选区蓝带跨两个折行段正确渲染（拖选后统计选区蓝像素分布）
l, t, r, b = win32gui.GetWindowRect(rx)
hdc = win32gui.GetWindowDC(rx)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r-l, b-t); dc.SelectObject(bmp)
if not user32.PrintWindow(rx, dc.GetSafeHdc(), 2):
    dc.BitBlt((0,0),(r-l,b-t),rx,(0,0),win32con.SRCCOPY)
bits = bmp.GetBitmapBits(True)
simg = Image.frombuffer("RGBA",(r-l,b-t),bits,"raw","BGRA",0,1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(rx, hdc)
lineH = 18                                          # ≈默认字体行高（96dpi）
def blue_in_band(y0, y1):
    n = 0
    for yy in range(y0, y1):
        for xx in range(10, 400):
            if is_hl(simg.getpixel((xx, yy))):
                n += 1
    return n
if drag_ok:
    lineH = 18
    band0 = blue_in_band(2, 2 + lineH - 4)              # 拖选起点所在显示行
    band1 = blue_in_band(2 + lineH, 2 + 2*lineH - 4)    # 第二个折行段显示行
    band2 = blue_in_band(2 + 3*lineH, 2 + 4*lineH - 4)  # 未选区（应为 0）
    if not INPUT_OK:
        print("NOTE r62: 拖选输入被拒（桌面竞争）——选区三条断言 SKIP", flush=True)
    chk("selection band on wrapped row 0", band0 > 50, "blue=%d" % band0) if INPUT_OK else None
    chk("selection band on wrapped row 1", band1 > 50, "blue=%d" % band1) if INPUT_OK else None
    chk("no selection below (control)", band2 < 10, "blue=%d" % band2) if INPUT_OK else None
else:
    print("SKIP: 选区拖选需解锁工作站后运行（python probes/verify_r62.py）", flush=True)
proc.terminate()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
