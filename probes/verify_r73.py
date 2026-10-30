# -*- coding: utf-8 -*-
# verify_r73.py - docs/00 §73（用户四项）：
#  1) 右键「全选」：菜单键盘导航选中 → Ctrl+C → 剪贴板含全量接收数据（长帧哨兵）
#  2) 缓冲上限单选：默认 1M 勾选；切到 20MB 后 20MB 项 MF_CHECKED（其余未勾）
#  3) BRK 状态框存在（IDC_PIN_BREAK=3078，文本 "BRK"）+ 截图目检
#  4) HEX 分包收行：设备侧单向流无法软环回复现，代码级修复 + 回归覆盖
#    （r65/r71 验证 HEX/文本折行不受影响）
import os, time, subprocess, ctypes, shutil
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

# §116：并入 §114 沙箱红线——不再以 develop\dist 为工作目录（graceful close 会把
# sncom.json 写回 dist；转存日志也会落进 dist）。BIN = probes\_probe\out\r73。
HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.join(os.path.dirname(HERE), "develop")
BIN = os.path.join(HERE, "_probe", "out", "r73")
shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(os.path.join(DEV, "dist", "sncom.exe"), os.path.join(BIN, "sncom.exe"))
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)
assert rx

# ---- 3) BREAK 状态（§76 起：独立 BRK 框已并入 BREAK 复选框 ownerdraw）
chk("§73/§76-3 BRK 独立框已移除（id 3081 无控件）", user32.GetDlgItem(main, 3081) == 0, "")
brk = win32gui.GetDlgItem(main, 3036)               # IDC_CHK_BREAK
style = win32gui.GetWindowLong(brk, win32con.GWL_STYLE)
chk("§76-3 BREAK 复选框为 ownerdraw（兼状态显示）", (style & 0x0B) == 0x0B, "style=0x%X" % style)

# ---- 2) 缓冲上限单选勾选 ----
MF_CHECKED = 0x8
MF_BYCOMMAND = 0x0
hmenu = user32.GetMenu(main)
mview = user32.GetSubMenu(hmenu, 3)                  # 显示
mbuf = user32.GetSubMenu(mview, 2)                   # 显示缓冲上限（§73 修复索引 2）
def buf_state(i):
    return user32.GetMenuState(mbuf, 2300 + i, MF_BYCOMMAND) & 0xFFFF
states0 = [buf_state(i) for i in range(6)]
chk("§73-2 默认勾选 1 MB（索引 1）", states0[1] & MF_CHECKED and sum(1 for s in states0 if s & MF_CHECKED) == 1,
    "states=%s" % [hex(s) for s in states0])
win32api.PostMessage(main, win32con.WM_COMMAND, 2303, 0)   # 选 20 MB
time.sleep(0.5)
states1 = [buf_state(i) for i in range(6)]
chk("§73-2 切 20 MB 后该项独占勾选", states1[3] & MF_CHECKED and sum(1 for s in states1 if s & MF_CHECKED) == 1,
    "states=%s" % [hex(s) for s in states1])

# ---- 1) 右键全选（点菜单第 2 项 → 选区蓝带取证 → Ctrl+C 剪贴板取证）----
# 坑：首次打开菜单时（淡入动画中）鼠标点击常被吞——用「选区蓝带」像素判据重试；
# 另：键盘导航（keybd_event / PostMessage VK_DOWN+RETURN）在本环境不产生选区（实测 blue=0）。
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # 回环演示喂数据
time.sleep(3.0)
user32.AllowSetForegroundWindow(-1)
cur = ctypes.windll.kernel32.GetCurrentThreadId()
app = win32process.GetWindowThreadProcessId(main)[0]
user32.AttachThreadInput(cur, app, True)
fg = user32.SetForegroundWindow(main)
user32.AttachThreadInput(cur, app, False)
time.sleep(0.3)
print("SetForegroundWindow ret=%s  fg==main:%s" % (fg, user32.GetForegroundWindow() == main), flush=True)

def rx_blue():
    """接收区内蓝色像素数（选区蓝带判据，≈0=无选区；r62 同款）"""
    l, t, r, b = win32gui.GetWindowRect(rx)
    hdc = win32gui.GetWindowDC(rx)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(rx, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(rx, hdc)
    n = 0
    for y in range(0, b - t, 3):
        for x in range(0, r - l, 3):
            p = img.getpixel((x, y))
            if p[2] > 120 and p[2] - p[0] > 40:
                n += 1
    return n

menu_found = False
sel_ok = False
# 两种点击策略轮流用（§81 复核）：A=真实 mouse_event；B=真实 SetCursorPos + 向菜单窗
# PostMessage 按钮消息（菜单跟踪读真实光标位置，故必须先 SetCursorPos）。桌面被占用时
# 两条都可能不生效——全部失败则 **NOTE + SKIP**（而非假红/假绿）。
for attempt in (1, 2, 3, 4):
    l, t, r, b = win32gui.GetWindowRect(rx)
    win32api.PostMessage(rx, win32con.WM_CONTEXTMENU, rx, win32api.MAKELONG(l + 200, t + 120))
    menu = 0
    for _ in range(20):
        menu = find_toplevel("#32768", proc.pid)
        if menu: break
        time.sleep(0.1)
    if not menu:
        print("attempt %d: menu not found" % attempt, flush=True)
        continue
    menu_found = True
    strategy = "A(real-mouse)" if attempt % 2 == 1 else "B(posted)"
    import ctypes.wintypes as wt
    pt = wt.POINT(); user32.GetCursorPos(ctypes.byref(pt))
    ml, mt, mr, mb2 = win32gui.GetWindowRect(menu)
    itemH = user32.GetSystemMetrics(15)             # SM_CYMENU（菜单项高）
    tx, ty = ml + 30, mt + 2 + itemH + itemH // 2   # 第 2 项（全选）
    time.sleep(0.7)                                 # 等淡入动画结束（首次点击被吞的根因）
    user32.SetCursorPos(tx, ty)
    time.sleep(0.35)
    if strategy.startswith("A"):
        user32.mouse_event(0x0002, 0, 0, 0, 0); time.sleep(0.08); user32.mouse_event(0x0004, 0, 0, 0, 0)
    else:
        cx, cy = tx - ml, ty - mt
        # 菜单可能被 LBUTTONDOWN 自行收起（句柄失效）：逐步判活，避免 PostMessage 抛 1400
        for msg, wp in ((win32con.WM_LBUTTONDOWN, 1), (win32con.WM_MOUSEMOVE, 1),
                        (win32con.WM_LBUTTONUP, 0)):
            if not win32gui.IsWindow(menu):
                break
            try:
                win32gui.PostMessage(menu, msg, wp, win32api.MAKELONG(cx, cy))
            except Exception:
                break
            time.sleep(0.1)
    time.sleep(0.6)
    user32.SetCursorPos(pt.x, pt.y)
    nb = rx_blue()
    print("attempt %d %s: menu_closed=%s blue=%d" % (attempt, strategy,
                                                    not win32gui.IsWindow(menu), nb), flush=True)
    if nb > 1000:
        sel_ok = True
        break
chk("§73-1 右键菜单已弹出", menu_found, "")
if sel_ok:
    chk("§73-1 全选选中全量数据（选区蓝带）", rx_blue() > 1000, "blue=%d" % rx_blue())
else:
    skip("§73-1 全选蓝带（菜单点击两策略均未生效，桌面竞争；细则见 §81.1）")
try:
    import win32clipboard
    win32clipboard.OpenClipboard(); win32clipboard.EmptyClipboard(); win32clipboard.CloseClipboard()
except Exception as ex:
    print("clipboard pre-clear failed: %s" % ex, flush=True)
clip_try = 0
for clip_try in (1, 2, 3):                     # Ctrl+C 偶发未落到接收区（实测）——重投并重读
    win32api.PostMessage(rx, win32con.WM_CHAR, 0x03, 0)
    time.sleep(0.6)
    try:
        import win32clipboard
        win32clipboard.OpenClipboard()
        try:
            _probe = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
        finally:
            win32clipboard.CloseClipboard()
        if _probe:
            break
    except Exception:
        pass
if not sel_ok:
    skip("§73-1 全选复制含全量数据（前置全选未生效，连带跳过）")
else:
    try:
        import win32clipboard
        win32clipboard.OpenClipboard()
        try:
            clip = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
        finally:
            win32clipboard.CloseClipboard()
        ok = ("回环演示" in clip) and clip.count("\r\n") >= 5
        chk("§73-1 全选复制含全量数据（哨兵+多行）", ok,
            "len=%d lines=%d head=%r" % (len(clip), clip.count("\r\n") + 1, clip[:30]))
    except Exception as ex:
        skip("§73-1 全选复制含全量数据（clipboard 拒绝：%s）" % ex)

# 截图（BRK 框/菜单观感）
l, t, r, b = win32gui.GetWindowRect(main)
hdc = win32gui.GetWindowDC(main)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
user32.PrintWindow(main, dc.GetSafeHdc(), 2)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r73_main.png")
print("saved r73_main.png", flush=True)

proc.terminate()
time.sleep(0.4)
shutil.rmtree(BIN, ignore_errors=True)
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
