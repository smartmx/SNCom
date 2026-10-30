# -*- coding: utf-8 -*-
# §134/§135 E2E 探针：界面字体对齐 SSCOM（原版真机普查：主体控件=宋体 9pt，中英文同字体）
#   §134 首版=仅端口框宋体；§135 起升格为**全界面控件宋体**（用户「整个界面全部宋体」）；
#   菜单栏保持原生系统绘制（用户拍板不改）；状态栏=系统字体（对齐原版实测：其状态栏即系统字体）。
#   A1 端口框字体族=宋体、逻辑高=-(9*dpi//72)
#   A2 波特率框同样=宋体（§135 全局对齐；§134 时本项为「非宋体」对照，口径随 §135 反转）
#   A3 端口框内容可读（WM_SETFONT 未清内容；文本含 COM）
#   A4 抽查：按钮/复选/标签/编辑框均=宋体（全局生效）
#   A5 状态栏=非宋体（系统 UI 字体；对齐原版 SSCOM 实测）
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time
import win32gui, win32process

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("SNCOM_EXE") or os.path.abspath(
    os.path.join(HERE, os.pardir, "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "font134")
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + detail if detail else ""), flush=True)

class LOGFONTW(ctypes.Structure):
    _fields_ = [("lfHeight", wt.LONG), ("lfWidth", wt.LONG), ("lfEscapement", wt.LONG),
                ("lfOrientation", wt.LONG), ("lfWeight", wt.LONG), ("lfItalic", ctypes.c_ubyte),
                ("lfUnderline", ctypes.c_ubyte), ("lfStrikeOut", ctypes.c_ubyte),
                ("lfCharSet", ctypes.c_ubyte), ("lfOutPrecision", ctypes.c_ubyte),
                ("lfClipPrecision", ctypes.c_ubyte), ("lfQuality", ctypes.c_ubyte),
                ("lfPitchAndFamily", ctypes.c_ubyte), ("lfFaceName", ctypes.c_wchar * 32)]

def font_of(hwnd):
    hf = user32.SendMessageW(hwnd, 0x0031, 0, 0)          # WM_GETFONT
    assert hf, "WM_GETFONT 返回空"
    lf = LOGFONTW()
    assert gdi32.GetObjectW(hf, ctypes.sizeof(lf), ctypes.byref(lf)) > 0, "GetObjectW 失败"
    return lf

def wtext(h):
    n = user32.SendMessageW(h, 0x000E, 0, 0)              # WM_GETTEXTLENGTH
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.SendMessageW(h, 0x000D, n + 1, ctypes.cast(buf, ctypes.c_void_p))
    return buf.value

shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
p = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(40):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and \
           win32process.GetWindowThreadProcessId(h)[1] == p.pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    if out:
        main = out[0]
        break
    time.sleep(0.25)
assert main, "主窗未出现"
time.sleep(2.0)

PORT_ID, BAUD_ID = 3014, 3019                              # IDC_CMB_PORT / IDC_CMB_BAUD
cp = win32gui.GetDlgItem(main, PORT_ID)
cb = win32gui.GetDlgItem(main, BAUD_ID)
lf = font_of(cp)
try:
    dpi = int(user32.GetDpiForWindow(cp)) or 96
except Exception:
    dpi = 96
expect_h = -(9 * dpi // 72)
chk("A1 端口框字体=宋体 9pt", lf.lfFaceName == "宋体" and lf.lfHeight == expect_h,
    "face=%s h=%d 期望 h=%d dpi=%d" % (lf.lfFaceName, lf.lfHeight, expect_h, dpi))
lb = font_of(cb)
chk("A2 波特率框同样=宋体（§135 全局对齐）", lb.lfFaceName == "宋体" and lb.lfHeight == expect_h,
    "baud face=%s h=%d" % (lb.lfFaceName, lb.lfHeight))
t = wtext(cp)
chk("A3 端口框内容可读且含 COM", "COM" in t, repr(t))

# A4 抽查其余控件（按钮 3002 / 复选 3045 / 标签 3013 / 编辑 3047）
bad4 = []
for cid, tag in ((3002, "按钮N"), (3045, "复选HEX发送"), (3013, "标签端口号"), (3047, "编辑间隔")):
    lfx = font_of(win32gui.GetDlgItem(main, cid))
    if lfx.lfFaceName != "宋体" or lfx.lfHeight != expect_h:
        bad4.append("%s:%s/%d" % (tag, lfx.lfFaceName, lfx.lfHeight))
chk("A4 抽查按钮/复选/标签/编辑均=宋体", not bad4, "bad=%s" % bad4)

# A5 状态栏=系统字体（非宋体；对齐原版实测：SSCOM 状态栏即系统 UI 字体）
sb = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
face = font_of(sb).lfFaceName if sb else "<none>"
chk("A5 状态栏=系统字体（非宋体，对齐原版）", bool(sb) and face != "宋体",
    "statusbar face=%s" % face)
p.terminate(); p.wait()
bad = [n for n, ok in res if not ok]
print("TOTAL %d/%d" % (len(res) - len(bad), len(res)), flush=True)
sys.exit(1 if bad else 0)
