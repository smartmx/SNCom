# -*- coding: utf-8 -*-
# verify_r54.py - docs/00 §54 seven user requests:
#  1) baud combo directly typeable (CBS_DROPDOWN + Enter commit + <300 restore)
#  3) ext window default width 480 (DPI scaled)
#  4) multisend note column placeholder 点击修改注释 (screenshot, human-judged)
#  5) send edit context menu with 转换为HEX/转换为编码字符 (screen shot, human-judged)
#  7) send box 4-line hint (screenshot, human-judged)
#  2) wheel: §44 probe verify_r43_44.py still green (run separately)
#  6) hex pair-mode: runcross hexself 18 cases (run separately)
import os, time, subprocess, ctypes, ctypes.wintypes, shutil
import win32gui, win32con, win32process, win32api


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
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
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
# sncom.json 写回 dist；转存日志也会落进 dist）。BIN = probes\_probe\out\r54。
BIN_DIR = os.path.abspath(os.path.join(HERE, "_probe", "out", "r54"))
shutil.rmtree(BIN_DIR, ignore_errors=True)
os.makedirs(BIN_DIR)
shutil.copy(os.path.join(DIST, "sncom.exe"), os.path.join(BIN_DIR, "sncom.exe"))
proc = subprocess.Popen([os.path.join(BIN_DIR, "sncom.exe")], cwd=BIN_DIR)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
dpi = user32.GetDpiForWindow(main) if hasattr(user32, "GetDpiForWindow") else 96
scale = dpi / 96.0

# ---- 1) baud：§55 恢复弹窗式——探针覆盖移至 verify_r43_44.py §46 段（18 项+弹窗全流程） ----
cmb = win32gui.GetDlgItem(main, 3019)              # IDC_CMB_BAUD
style = win32gui.GetWindowLong(cmb, win32con.GWL_STYLE)
chk("baud combo is CBS_DROPDOWNLIST (不可直输)", (style & 3) == 3, "style&3=%d" % (style & 3))

# ---- 3) ext default width 480 ----
win32gui.PostMessage(win32gui.GetDlgItem(main, 3012), 0x00F5, 0, 0)   # BM_CLICK 展开
time.sleep(1.2)
ext = find_toplevel("SNComExt", proc.pid)
assert ext
l, t, r, b = win32gui.GetWindowRect(ext)
want_w = round(480 * scale)
chk("ext default width == 480 @dpi", (r - l) == want_w, "got=%d want=%d" % (r - l, want_w))

# ---- 4) multisend note placeholder ----
win32gui.PostMessage(ext, win32con.WM_APP + 0x111, 2, 0) if False else None
# 多条发送是页 0（默认页）；直接截图
os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
def shot(hwnd, name, crop_bottom=None):
    wl, wt, wr, wb = win32gui.GetWindowRect(hwnd)
    w, h = wr - wl, wb - wt
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (w, h), mfc, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    from PIL import Image
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    if crop_bottom:
        img = img.crop((0, img.size[1] - crop_bottom, img.size[0], img.size[1]))
    p = os.path.join(HERE, "out", name)
    img.save(p)
    print("saved", name, img.size, flush=True)
    return img
import win32ui
shot(ext, "r54_ms_note.png")

# ---- 7) send box 4-line hint ----
send = win32gui.GetDlgItem(main, 3057)             # IDC_EDIT_SEND
win32gui.SendMessage(send, win32con.WM_SETTEXT, 0, 0)   # 清空（提示只在空时画）
shot(main, "r54_hint.png", crop_bottom=280)

# ---- 5) context menu（置顶后屏幕取证，P3-6 口径） ----
win32gui.PostMessage(win32gui.GetDlgItem(main, 3010), 0x00F5, 0, 0)   # 最前（TOPMOST）
time.sleep(0.4)
win32gui.SendMessage(send, win32con.WM_SETTEXT, 0, "123")
win32gui.SendMessage(send, win32con.EM_SETSEL, 0, 3)
# TrackPopupMenu 需要前台：合成 ALT 解前台锁（P3-6 同款），再归焦点后发 WM_CONTEXTMENU
keybd_event(win32con.VK_MENU, 0, 0, 0)
try:
    win32gui.SetForegroundWindow(main)          # 前台锁下可能被拒（§59 审查 P3-3）——降级继续
except Exception as e:
    print("SetForegroundWindow rejected (%s), continue topmost-only" % e, flush=True)
keybd_event(win32con.VK_MENU, 0, win32con.KEYEVENTF_KEYUP, 0)
time.sleep(0.4)
sl2, st2, sr2, sb2 = win32gui.GetWindowRect(send)
cx, cy = (sl2 + sr2) // 2, (st2 + sb2) // 2 - 40
SetCursorPos((cx, cy)); time.sleep(0.2)
mouse_event(win32con.MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0)
time.sleep(0.08)
mouse_event(win32con.MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0)
time.sleep(0.8)
pt = win32gui.GetCaretPos() if False else None
sl, st, sr, sb = win32gui.GetWindowRect(main)
gdi32 = ctypes.windll.gdi32
sdc = user32.GetDC(0)
mdc = win32ui.CreateDCFromHandle(sdc).CreateCompatibleDC()
bmp = win32ui.CreateBitmap()
W, H = sr - sl, 580
bmp.CreateCompatibleBitmap(win32ui.CreateDCFromHandle(sdc), W, H)
gdi32 = ctypes.windll.gdi32
mdc.SelectObject(bmp)
gdi32.BitBlt(mdc.GetSafeHdc(), 0, 0, W, H, sdc, sl, sb - 400, 0x00CC0020)  # SRCCOPY
bits = bmp.GetBitmapBits(True)
from PIL import Image
img = Image.frombuffer("RGBA", (W, H), bits, "raw", "BGRA", 0, 1).convert("RGB")
img.save(os.path.join(HERE, "out", "r54_ctxmenu.png"))
print("saved r54_ctxmenu.png", flush=True)
win32gui.DeleteObject(bmp.GetHandle()); mdc.DeleteDC(); user32.ReleaseDC(0, sdc)

# ---- 5b) 端到端：重开菜单，UP×2 选中「转换为 HEX」→ "123" 应变 "31 32 33" ----
def read_send():
    n = win32gui.SendMessage(send, win32con.WM_GETTEXTLENGTH, 0, 0)
    b2 = ctypes.create_unicode_buffer(n + 1)
    win32gui.SendMessage(send, win32con.WM_GETTEXT, n + 1, ctypes.addressof(b2))
    return b2.value
def open_menu():
    SetCursorPos((cx, cy)); time.sleep(0.2)
    mouse_event(win32con.MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0)
    time.sleep(0.08)
    mouse_event(win32con.MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0)
    time.sleep(0.6)
keybd_event(win32con.VK_ESCAPE, 0, 0, 0)   # 截图用的菜单关掉
time.sleep(0.3)
win32gui.SendMessage(send, win32con.EM_SETSEL, 0, -1)
open_menu()
for _ in range(2):
    keybd_event(win32con.VK_UP, 0, 0, 0); time.sleep(0.05)
keybd_event(win32con.VK_RETURN, 0, 0, 0)
time.sleep(0.5)
if INPUT_OK:
    chk("ctx menu 转换为HEX: 123 -> '31 32 33'", read_send() == "31 32 33", "got=%r" % read_send())
else:
    skip("r54-5b ctx menu 转换为HEX（真实输入被拒，桌面竞争）")

# ---- 5c) 反向：全选 '31 32 33' → UP×1「转换为编码字符」→ 应回 '123' ----
win32gui.SendMessage(send, win32con.EM_SETSEL, 0, -1)
open_menu()
keybd_event(win32con.VK_UP, 0, 0, 0); time.sleep(0.05)
keybd_event(win32con.VK_RETURN, 0, 0, 0)
time.sleep(0.5)
if INPUT_OK:
    chk("ctx menu 转换为编码字符: '31 32 33' -> '123'", read_send() == "123", "got=%r" % read_send())
else:
    skip("r54-5c ctx menu 转换为编码字符（真实输入被拒，桌面竞争）")
win32gui.PostMessage(win32gui.GetDlgItem(main, 3010), 0x00F5, 0, 0)   # 取消置顶
proc.terminate()
time.sleep(0.4)
shutil.rmtree(BIN_DIR, ignore_errors=True)

print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
