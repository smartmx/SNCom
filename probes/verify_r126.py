# -*- coding: utf-8 -*-
# §126 探针：终端仿真两处行为改动
#   A 回车方式：回显行不带时间戳前缀/方向标记（旧管线会带 [ts] 发→◇），且回车后
#     输入尾巴/光标落新行（位图行带断言）
#   B 输入期自动滚屏：灌数据→滚到顶（取消跟随）→键入→视口自动跳回底部；
#     自动滚屏勾选框保持用户原态（不勾）
#   C 按键方式回归：逐键即发不受影响
import ctypes, os, shutil, socket, subprocess, sys, time
import win32gui, win32con, win32process, win32api
user32 = ctypes.windll.user32

HERE = os.path.dirname(os.path.abspath(__file__))
SNCOM_ROOT = os.path.dirname(HERE)
SRC = os.environ.get("SNCOM_EXE") or os.path.join(SNCOM_ROOT, "develop", "dist", "sncom.exe")
BIN = os.path.join(HERE, "_probe", "out", "term126")
OUT = os.path.join(HERE, "out")
PORT = 5221
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + detail if detail else ""), flush=True)

shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
    # §140：终端仿真改为总开关默认关——本探针专测终端行为，预置开启（对齐旧「始终可用」口径）
    f.write('{"mode":"tcp_client","remote_ip":"127.0.0.1","remote_port":%d,"encoding":"gbk",'
            '"term_enable":true}' % PORT)

proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
def find(cls):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == proc.pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
main = 0
for _ in range(40):
    main = find("SNComMain")
    if main: break
    time.sleep(0.25)
assert main
time.sleep(2.5)
rx = win32gui.GetDlgItem(main, 3059)
chk_rx = win32gui.GetDlgItem(main, 3003)   # IDC_CHK_AUTOSCROLL=3003
assert rx and chk_rx

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT)); srv.listen(1)
win32api.PostMessage(main, win32con.WM_COMMAND, 3066, 0)     # 连接
srv.settimeout(8)
conn = None
try:
    conn, _ = srv.accept()
except socket.timeout:
    pass
chk("前置：TCP 连接建立", conn is not None)


rxfile = win32gui.GetDlgItem(main, 3039)                # IDC_CHK_RXFILE=3039
win32gui.SendMessage(rxfile, 0x00F5, 0, 0)              # BM_CLICK → BN_CLICKED 反射路由
time.sleep(0.8)

WM_CHAR, WM_KEYDOWN, WM_CLOSE = 0x0102, 0x0100, 0x0010
IDM_TERM_KEY, IDM_TERM_ECHO = 2211, 2212

def newlog():
    fs = [os.path.join(BIN, x) for x in os.listdir(BIN) if x.startswith("SNCom-Log")]
    return max(fs, key=os.path.getmtime) if fs else None

def logtext(path):
    if not path: return ""
    # 转存按 cfg_.encoding 写（WriteAllTo；fixture 固定 gbk）——默认 GBK 解码
    return _read_shared(path).decode("gbk", "replace")

def _read_shared(path):
    k32 = ctypes.windll.kernel32
    GENERIC_READ = 0x80000000
    FILE_SHARE_RW = 0x1 | 0x2
    OPEN_EXISTING = 3
    h = k32.CreateFileW(path, GENERIC_READ, FILE_SHARE_RW, None, OPEN_EXISTING, 0, None)
    if h in (-1, 0xFFFFFFFFFFFFFFFF): return ""
    try:
        size = k32.GetFileSize(h, None)
        buf = ctypes.create_string_buffer(size + 1)
        got = ctypes.c_ulong()
        k32.ReadFile(h, buf, size, ctypes.byref(got), None)
        return buf.raw[:got.value]
    finally:
        k32.CloseHandle(h)

def bands_of(img, maxw=80, minh=6):
    """文本行带聚类：返回 [(y0,y1)]。左上角 80px 内步长 1（短行 "AB"/"CD" 的字形采样
    密度要求——步长 2 会漏掉 2 字符短行的笔画，§126 实测）。"""
    w, h = img.size
    rows = []
    for yy in range(2, h - 4):
        ink = sum(1 for xx in range(2, min(w, maxw))
                  if all(c < 120 for c in img.getpixel((xx, yy))[:3]))
        rows.append(ink)
    bands, inb = [], False
    for yy, c in enumerate(rows):
        if c >= 2 and not inb:
            y0 = yy; inb = True
        elif c < 2 and inb:
            if yy - y0 >= minh: bands.append((y0, yy))
            inb = False
    return bands

def shoot(h):
    """r112 实证姿势：win32ui 位图 + user32.PrintWindow flag=2（渲染全内容）。"""
    import win32ui
    from PIL import Image
    l, t, r, b = win32gui.GetWindowRect(h)
    hdc = win32gui.GetWindowDC(h)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(h, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(h, hdc)
    return img

# ---------- A) 回车方式：回显行无 [ts]/标记 + 回车后落新行 ----------
time.sleep(0.5)
try:
    win32gui.SetFocus(rx)
except Exception:
    pass                                   # 前台锁下可能被拒——WM_CHAR 直投不依赖焦点
time.sleep(0.3)
for ch in "AB":
    win32api.PostMessage(rx, WM_CHAR, ord(ch), 0); time.sleep(0.15)
win32api.PostMessage(rx, WM_KEYDOWN, win32con.VK_RETURN, 0)
time.sleep(0.9)
for ch in "CD":
    win32api.PostMessage(rx, WM_CHAR, ord(ch), 0); time.sleep(0.15)
time.sleep(0.5)
try:
    os.makedirs(OUT, exist_ok=True)
    img = shoot(rx)
    img.save(os.path.join(OUT, "r126_enter_line.png"))
    bd = bands_of(img)
    chk("A1 位图：回显产生 ≥2 条独立行带（回车后落新行）", len(bd) >= 2, "bands=%s" % bd[:6])
except Exception as ex:
    chk("A1 位图：回显产生 ≥2 条独立行带（回车后落新行）", False, "shot fail %s" % ex)
except Exception as ex:
    chk("A1 位图：回显产生 ≥2 条独立行带（回车后落新行）", False, "shot fail %s" % ex)


# ---------- B) 输入期自动滚屏 ----------
for i in range(60):
    conn.sendall(("RXLINE%02dxxxxxxxxxxxxxxxx\r\n" % i).encode("ascii"))
    time.sleep(0.08)
time.sleep(1.2)
def _scroll():
    class S(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("fMask", ctypes.c_uint),
                    ("nMin", ctypes.c_int), ("nMax", ctypes.c_int),
                    ("nPage", ctypes.c_uint), ("nPos", ctypes.c_int),
                    ("nTrackPos", ctypes.c_int)]
    s = S(); s.cbSize = ctypes.sizeof(S); s.fMask = 0x17
    G = ctypes.windll.user32.GetScrollInfo
    G.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    G(rx, 1, ctypes.addressof(s))
    return s.nPos, s.nMax

def autoscroll_checked():
    return win32gui.SendMessage(chk_rx, 0x00F1, 0, 0) == 2          # BM_GETCHECK==BST_CHECKED
for _ in range(6):                                                  # 滚轮上翻（真实回看路径）
    win32api.PostMessage(rx, 0x020A, win32api.MAKELONG(0, 120),
                         win32api.MAKELONG(200, 200))
    time.sleep(0.1)
time.sleep(0.8)
p0, m0 = _scroll()
chk("B1 前置：滚轮上翻后视口离开底部（回看态）", p0 < m0 - 2, "pos=%d max=%d" % (p0, m0))
chk0 = autoscroll_checked()

win32api.PostMessage(rx, WM_CHAR, ord("Z"), 0)
time.sleep(0.9)

class SCROLLINFO(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_uint), ("fMask", ctypes.c_uint),
                ("nMin", ctypes.c_int), ("nMax", ctypes.c_int),
                ("nPage", ctypes.c_uint), ("nPos", ctypes.c_int),
                ("nTrackPos", ctypes.c_int)]
si = SCROLLINFO(); si.cbSize = ctypes.sizeof(SCROLLINFO); si.fMask = 0x17   # SIF_ALL
GKI = ctypes.windll.user32.GetScrollInfo
GKI.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
GKI(rx, 1, ctypes.addressof(si))                                    # SB_VERT=1
page = si.nPage if si.nPage else 28
chk("B2 键入后视口回底（nPos>=nMax-page+1）", si.nPos >= si.nMax - page + 1,
    "pos=%d max=%d page=%d" % (si.nPos, si.nMax, page))
chk("B3 勾选框未被键入篡改（前后读数一致）", autoscroll_checked() == chk0,
    "before=%s after=%s" % (chk0, autoscroll_checked()))

# ---------- B4/B5 接收续期（SSCOM 基准：收发后光标恒在底部空行）----------
win32api.PostMessage(rx, WM_CHAR, ord("Y"), 0)          # 重触临时跟随（计时归零）
time.sleep(0.2)
conn.sendall(b"RXTOUCH-RENEW\r\n")                       # 跟随期内收到数据 → 续期并保持底部
time.sleep(0.6)
sB4 = SCROLLINFO(); sB4.cbSize = ctypes.sizeof(SCROLLINFO); sB4.fMask = 0x17
GKI(rx, 1, ctypes.addressof(sB4))
pg4 = sB4.nPage if sB4.nPage else 28
chk("B4 跟随期内收到数据：续期并保持视口在底部", sB4.nPos >= sB4.nMax - pg4 + 1,
    "pos=%d max=%d page=%d" % (sB4.nPos, sB4.nMax, pg4))
time.sleep(2.6)                                          # 输入/收发静默 >2s → 临时跟随回退（停留原地）
img_before = shoot(rx)
conn.sendall(b"RXAFTER-REV1\r\nRXAFTER-REV2\r\n")        # 回退后收到数据 → 不拉底（尊重回看原态）
time.sleep(0.6)
img_after = shoot(rx)
# 滚动条读数在此态不可判（firstLine_ 本被 SetScrollInfo 钳到 nMax-nPage+1，nMax 涨则报告值
# 跟涨——33->34 假红实测）→ 用位图上 60% 对比：未跟随=顶部纹丝不动；跟随=内容整体上移。
# 裁掉右侧 20px（滚动条 thumb 随 nMax 变宽）与底部（光标闪烁伪差异）。
w, hh = img_before.size
crop = (0, 0, w - 20, int(hh * 0.6))
same_top = img_before.crop(crop).tobytes() == img_after.crop(crop).tobytes()
sB5 = SCROLLINFO(); sB5.cbSize = ctypes.sizeof(SCROLLINFO); sB5.fMask = 0x17
GKI(rx, 1, ctypes.addressof(sB5))
chk("B5 静默回退后收到数据：视口停留原处（不强制跟随）",
    same_top and sB5.nMax >= 63,
    "top_same=%s max=%d" % (same_top, sB5.nMax))

# ---------- C) 按键方式回归 ----------
win32api.PostMessage(main, win32con.WM_COMMAND, IDM_TERM_KEY, 0); time.sleep(0.4)
win32api.PostMessage(main, win32con.WM_COMMAND, IDM_TERM_ECHO, 0); time.sleep(0.4)
win32api.PostMessage(rx, WM_CHAR, ord("K"), 0)
time.sleep(0.9)
conn.settimeout(1.0)
got = b""
try:
    got = conn.recv(64)
except socket.timeout:
    pass
chk("C1 按键方式回显开：单键 K 即发", b"K" in got, "got=%r" % got)

win32gui.PostMessage(main, WM_CLOSE, 0, 0)
for _ in range(20):
    if proc.poll() is not None: break
    time.sleep(0.15)
if proc.poll() is None: proc.kill()
if conn: conn.close()
srv.close()

# A2/A3：日志为独占打开——应用退出后读
txt = logtext(newlog())
chk("A2 转存：回显行=日志第一行且无时间戳前缀/标记（feed_terminal_line）",
    txt.startswith("AB\r\n"), "head=%r" % txt[:60])
import re
chk("A3 转存：接收流时间戳正常（灌数据带 [ts] 收←◆）",
    bool(re.search(r"\[\d{2}:\d{2}:\d{2}\.\d{3}\]", txt)) and ("收" in txt),
    "head=%r" % txt[:60])

shutil.rmtree(BIN, ignore_errors=True)

tot = len(res)
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    tot, sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
sys.exit(1 if any(not ok for _, ok in res) else 0)
