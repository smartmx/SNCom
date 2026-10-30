# -*- coding: utf-8 -*-
# verify_r77.py - docs/00 §77（SSCOM changelog 借鉴六项）：
#  2/3) 校验新增 CRC32 / ADD8_0：发送框 "123456789"（加回车换行关）→ 预览框断言
#       CRC32="26 39 F4 CB"（低字节在前）/ ADD8_0="23"（0-0xDD）
#  4) 转义字符：HEX 显示开 + 回环演示 + 勾「接收数据到文件」→ 发送 `\x41\r\nB`
#     → 转存日志含 "41 0D 0A 42"（证据取文件，绕开右键菜单/剪贴板的交互脆弱面）
#  9) 波特率上限 50Mbps：下拉「自定义」→ 弹窗 50000000 生效；60000000 拒绝留在弹窗
#  1) UDP 收包自动更新远端：远端先设 127.0.0.2:9990 → python 从 127.0.0.1:P 发一包
#     → 远端两框自动变 127.0.0.1:P
#  10) 缓冲占用染色：档位 500K + 发 400KB → RX 段染红（r-g≥10）；清除窗口恢复
#  11) 多条表 LVS_EX_INFOTIP(0x400) 样式位（悬停「第 N 条」由 LVN_GETINFOTIPW 提供）
#  备注（实测坑）：跨进程 GetWindowText 读 EDIT 拿到的是创建期缓存标题（假值）——
#  一律用 WM_GETTEXT 问控件本体；跨进程 SetWindowText 不产生 EN_CHANGE，按用户编辑补投。
import os, time, subprocess, ctypes, socket, shutil
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

# §120（十八轮 P3-5）：环境性跳过单独计数——与 r54/r56/r67/r73 同口径。
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

def wtext(h, cap=1024):
    b = ctypes.create_unicode_buffer(cap)
    user32.SendMessageW(h, 0x000D, cap, b)            # WM_GETTEXT（跨进程可靠）
    return b.value

def wset(h, s):
    user32.SendMessageW(h, 0x000C, 0, ctypes.c_wchar_p(s))   # WM_SETTEXT

def notify(main, cid, code):
    win32api.PostMessage(main, win32con.WM_COMMAND,
                         cid | (code << 16), user32.GetDlgItem(main, cid))

def bm_click(h):
    win32api.PostMessage(h, 0x00F5, 0, 0)

def bm_check(h):
    return user32.SendMessageW(h, 0x00F0, 0, 0)       # BM_GETCHECK

def grab(main):
    l, t, r, b = win32gui.GetWindowRect(main)
    hdc = win32gui.GetWindowDC(main)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(main, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
    return img

def status_scan(img, y, x0, x1):
    """状态栏行 y 上 [x0,x1) 的最大 r-g（RX 段染色检测；行 y 由状态栏窗口矩形给出）"""
    best = -255
    where = (0, 0)
    for yy in (y - 3, y, y + 3):
        for xx in range(x0, x1, 8):
            p = img.getpixel((xx, yy))
            if p[0] - p[1] > best:
                best = p[0] - p[1]
                where = (xx, yy)
    return best, where

def screen_grab(main):
    """真实屏幕合成截图（SRCCOPY from desktop DC）——RX 段染色覆盖窗是 ownerdraw STATIC，
    PrintWindow 渲染不出它（实测：PrintWindow 图为无染色，屏幕为 (246,223,223)）。"""
    l, t, r, b = win32gui.GetWindowRect(main)
    hdesk = user32.GetDC(0)
    mfc = win32ui.CreateDCFromHandle(hdesk); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    dc.BitBlt((0, 0), (r - l, b - t), mfc, (l, t), 0x00CC0020)   # SRCCOPY
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); user32.ReleaseDC(0, hdesk)
    return img


def front_and_grab(main, tag=""):
    """置前 + 屏幕取证：屏幕合成要求窗口可见/不被遮挡。**返回 None = 未能置前**（调用方须
    NOTE + SKIP 颜色断言）——实测夺前台随机失败，失败时抓到的是别的窗口像素（如 (28,46,74)
    的终端底色），会把"没前台"误判成"没染色/没恢复"。判定=GetForegroundWindow==main
    且窗口矩形中心点 WindowFromPoint 落在本窗口树上。"""
    for _ in range(8):
        user32.AllowSetForegroundWindow(-1)
        cur = ctypes.windll.kernel32.GetCurrentThreadId()
        app = win32process.GetWindowThreadProcessId(main)[0]
        user32.AttachThreadInput(cur, app, True)
        user32.SetForegroundWindow(main)
        user32.BringWindowToTop(main)
        user32.AttachThreadInput(cur, app, False)
        time.sleep(0.35)
        if user32.GetForegroundWindow() != main:
            continue
        img = screen_grab(main)
        if tag:
            img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\%s.png" % tag)
        return img
    print("NOTE 染色: 未能前置主窗（桌面被占用）——颜色断言 SKIP（可见性/阈值另测）", flush=True)
    return None


def find_child(parent, cls):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls:
            out.append(h)
        return True
    user32.EnumChildWindows(parent, ctypes.WINFUNCTYPE(ctypes.c_bool,
                            ctypes.c_void_p, ctypes.c_void_p)(cb), 0)
    return out[0] if out else 0

def newest_log():
    import glob
    fs = sorted(glob.glob(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r77\SNCom-Log-*.txt"))
    return fs[-1] if fs else ""

# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r77"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r77\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r77\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r77")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

es = user32.GetDlgItem(main, 3057)      # 发送框
send = user32.GetDlgItem(main, 3058)    # 发 送
newline = user32.GetDlgItem(main, 3049) # 加回车换行
hexshow = user32.GetDlgItem(main, 3037) # HEX 显示
rx = user32.GetDlgItem(main, 3059)      # 接收区

# 关「加回车换行」（默认开）——校验/转义断言需纯载荷
if bm_check(newline) == 1:
    bm_click(newline)
    time.sleep(0.3)

# ---- 2/3) 校验 CRC32 / ADD8_0 ----
# 帧尾口径 = 「无帧尾追加校验」（档位 5）→ 校验体=整段载荷，向量才落在文献值上
tail = user32.GetDlgItem(main, 3053)
user32.SendMessageW(tail, 0x014E, 4, 0)
notify(main, 3053, 1)
time.sleep(0.3)
wset(es, "123456789")
notify(main, 3057, 768)                 # EN_CHANGE（跨进程 WM_SETTEXT 不自动报）
time.sleep(0.4)
cmb = user32.GetDlgItem(main, 3055)     # 校验下拉
view = user32.GetDlgItem(main, 3056)    # 校验值框
def sel_kind(idx):
    user32.SendMessageW(cmb, 0x014E, idx, 0)                      # CB_SETCURSEL
    notify(main, 3055, 1)                                          # CBN_SELCHANGE
    time.sleep(0.4)
    return wtext(view)
v = sel_kind(7)                         # CRC32
chk("§77-2 CRC32 预览（'123456789'→26 39 F4 CB，低字节在前）", v.replace(" ", "").upper() == "2639F4CB", "view=%r" % v)
v = sel_kind(8)                         # ADD8_0
chk("§77-3 ADD8_0 预览（0-0xDD=0x23）", v.strip().upper() == "23", "view=%r" % v)
sel_kind(0)                             # 复原无校验
user32.SendMessageW(tail, 0x014E, 0, 0)
notify(main, 3053, 1)
time.sleep(0.2)

# ---- 4) 转义字符（HEX 显示开，回环演示喂链路，转存日志取证字节）----
if bm_check(hexshow) != 1:
    bm_click(hexshow)
    time.sleep(0.4)
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # 回环演示（软件回环）
time.sleep(1.2)
rxfile = user32.GetDlgItem(main, 3039)  # 接收数据到文件
log_before = newest_log()
if bm_check(rxfile) != 1:
    bm_click(rxfile)                    # 勾选即生效（无对话框）
    time.sleep(0.6)
wset(es, r"\x41\r\nB")                  # 字面 \x41 \r \n B → 41 0D 0A 42
notify(main, 3057, 768)
time.sleep(0.3)
bm_click(send)
time.sleep(1.6)
if bm_check(rxfile) == 1:
    bm_click(rxfile)                    # 关掉转存，避免继续污染
    time.sleep(0.5)
logp = newest_log()
logdata = b""
if logp and logp != log_before:
    with open(logp, "rb") as f:
        logdata = f.read()
chk("§77-4 转义 \\x41\\r\\nB → 字节 41 0D 0A 42（回环+转存日志取证）",
    b"41 0D 0A 42" in logdata, "log=%s len=%d" % (os.path.basename(logp), len(logdata)))

# 复原：HEX 显示关 + 加回车换行 开 + 清发送框
bm_click(hexshow)
time.sleep(0.3)
if bm_check(newline) == 0:
    bm_click(newline)
    time.sleep(0.3)
wset(es, "")

# ---- 9) 波特率上限 50000000 ----
baud = user32.GetDlgItem(main, 3019)
def sel_custom_baud():
    idx = user32.SendMessageW(baud, 0x0158, -1, ctypes.c_wchar_p("自定义"))   # CB_FINDSTRINGEXACT
    if idx < 0:
        return 0
    user32.SendMessageW(baud, 0x014E, idx, 0)
    notify(main, 3019, 1)
    dlg = 0
    for _ in range(20):
        dlg = find_toplevel("SNComBaudDlg", proc.pid)
        if dlg: break
        time.sleep(0.1)
    return dlg
dlg = sel_custom_baud()
chk("§77-9 自定义波特率弹窗已弹出", bool(dlg), "")
if dlg:
    wset(user32.GetDlgItem(dlg, 1001), "50000000")
    bm_click(user32.GetDlgItem(dlg, 1002))         # 确定
    time.sleep(0.6)
    got = wtext(baud)
    chk("§77-9 50000000 接受并生效", got == "50000000", "baud=%r" % got)
    dlg2 = sel_custom_baud()
    if dlg2:
        wset(user32.GetDlgItem(dlg2, 1001), "60000000")
        bm_click(user32.GetDlgItem(dlg2, 1002))    # 确定 → 应被拒
        time.sleep(0.5)
        alive = bool(win32gui.IsWindow(dlg2))
        back = wtext(user32.GetDlgItem(dlg2, 1001)) if alive else "-"
        chk("§77-9 60000000 拒绝（弹窗保留、回上一有效值）", alive and back == "50000000",
            "dlg_alive=%s edit=%r" % (alive, back))
        if alive:
            bm_click(user32.GetDlgItem(dlg2, 1003))    # 取消
            time.sleep(0.3)

# ---- 1) UDP 收包自动更新远端 ----
win32api.PostMessage(main, win32con.WM_COMMAND, 2003, 0)   # IDM_MODE_BASE+3 = UDP
time.sleep(0.6)
wset(user32.GetDlgItem(main, 3073), "777")                 # 本地端口
user32.SendMessageW(user32.GetDlgItem(main, 3072), 0x014E, 0, 0)   # 本地 IP 全部（任意）
wset(user32.GetDlgItem(main, 3064), "127.0.0.2")           # 远端 IP（先设不同值）
wset(user32.GetDlgItem(main, 3065), "9990")
time.sleep(0.2)
bm_click(user32.GetDlgItem(main, 3066))                    # 连接
time.sleep(0.8)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))
myp = s.getsockname()[1]
s.sendto(b"peer-test", ("127.0.0.1", 777))
s.close()
time.sleep(1.0)
rip = wtext(user32.GetDlgItem(main, 3064))
rpt = wtext(user32.GetDlgItem(main, 3065))
chk("§77-1 UDP 收包后远端 IP→127.0.0.1", rip == "127.0.0.1", "rip=%r" % rip)
chk("§77-1 UDP 收包后远端端口→来源端口", rpt == str(myp), "rpt=%r want=%s" % (rpt, myp))
bm_click(user32.GetDlgItem(main, 3067))                    # 断开
time.sleep(0.4)
win32api.PostMessage(main, win32con.WM_COMMAND, 2000, 0)   # 切回串口
time.sleep(0.5)

# ---- 10) 缓冲占用染色 ----
# 档位 500K：菜单单选态取证（cap 生效）+ RX 段主色取证（染色实现为 RX 段覆盖窗）
win32api.PostMessage(main, win32con.WM_COMMAND, 2300, 0)
time.sleep(0.4)
MB = user32.GetMenu(main)
mbuf = user32.GetSubMenu(user32.GetSubMenu(MB, 3), 2)      # 显示 → 显示缓冲上限
states = [user32.GetMenuState(mbuf, 2300 + i, 0) & 0xFFFF for i in range(6)]
chk("§77-10 缓冲档 500K 独占勾选（cap=512000 生效）",
    bool(states[0] & 0x8) and sum(1 for s in states if s & 0x8) == 1, "states=%s" % [hex(s) for s in states])
# 回环需在“模式切回串口”之后仍开着——按钮文案是唯一廉价判据（打开/关闭串口）
openbtn = user32.GetDlgItem(main, 3017)
def loop_ready():
    for _ in range(2):
        if "关闭" in wtext(openbtn):
            return True
        win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # 回环演示
        time.sleep(1.5)
    return "关闭" in wtext(openbtn)
chk("§77-10 回环链路就绪（可发送）", loop_ready(), "btn=%r" % wtext(openbtn))
bm_click(user32.GetDlgItem(main, 3000))                    # 先清空（基线占用归零）
time.sleep(0.8)
wset(es, "X" * 400000)
notify(main, 3057, 768)
time.sleep(0.3)
bm_click(send)
time.sleep(2.5)
time.sleep(0.4)                                    # 等覆盖窗重绘落屏（250ms 拍）
img = front_and_grab(main, "r77_tint")             # None = 未置前（helper 内已 NOTE + 落图）
# 状态栏行与 RX 段：由真实窗口矩形标定（此前按窗口底缘猜测 → 采样落进阴影带全黑）
sb = find_child(main, "msctls_statusbar32")
ml, mt, mr, mb = win32gui.GetWindowRect(main)
sl, stp, sr, sbb = win32gui.GetWindowRect(sb)
sendRight = win32gui.GetWindowRect(send)[2] - ml
def seg_mode(imgw):
    """RX 段区域主色（背景占比远大于文字，取众数即背景色）"""
    from collections import Counter
    c = Counter()
    for yy in range(stp - mt + 3, sbb - mt - 3):
        for xx in range(156, min(280, sendRight - 8)):
            c[imgw.getpixel((xx, yy))] += 1
    return c.most_common(1)[0]
# 与屏幕无关的硬判据（§81）：染色覆盖窗（id 3099）的**显隐 + 几何**——≥60% 必可见且
# 右缘贴「发 送」按钮右缘；清除后必隐藏。屏幕色值断言（下方）仅作补充。
ov = user32.GetDlgItem(main, 3099)
ov_vis = bool(user32.IsWindowVisible(ov)) if ov else False
ov_l, ov_r = (win32gui.GetWindowRect(ov)[0] - ml, win32gui.GetWindowRect(ov)[2] - ml) if ov else (0, 0)
chk("§77-10 78% 时染色覆盖窗可见（ownerdraw 覆盖段）", ov_vis, "visible=%s" % ov_vis)
chk("§77-10 覆盖窗右缘 = 发送按钮右缘（段宽口径）", ov and ov_r == win32gui.GetWindowRect(send)[2] - ml,
    "ov=%d sendR=%d" % (ov_r, win32gui.GetWindowRect(send)[2] - ml))
if img is None:
    skip("§77-10 缓冲 ~78% 染红色值断言（未取到屏幕画面，未能前置主窗）")
    m1 = m2 = None
else:
    m1, n1 = seg_mode(img)
    chk("§77-10 缓冲 ~78% → RX 段染红（r-g≥10）", m1[0] - m1[1] >= 10,
        "mode=%s n=%d x_range=[156,%d)" % (m1, n1, min(280, sendRight - 8)))
bm_click(user32.GetDlgItem(main, 3000))                    # 清除窗口 → 占用归零
time.sleep(1.2)
img2 = front_and_grab(main, "r77_clear")
ov_hidden = bool(ov) and not user32.IsWindowVisible(ov)
chk("§77-10 清除窗口后覆盖窗隐藏（回落 ≤60%）", ov_hidden, "visible=%s" % (not ov_hidden))
if img2 is not None:
    m2, n2 = seg_mode(img2)
    chk("§77-10 清除窗口后恢复（r-g≤6）", m2[0] - m2[1] <= 6, "mode=%s n=%d" % (m2, n2))
else:
    skip("§77-10 清除后恢复色值断言（未取到屏幕画面，未能前置主窗）")

# ---- 11) 多条表 INFOTIP 样式 ----
bm_click(user32.GetDlgItem(main, 3012))                    # 扩展
time.sleep(0.8)
ext = find_toplevel("SNComExt", proc.pid)
ms = user32.GetDlgItem(ext, 20) if ext else 0
st = user32.SendMessageW(ms, 0x1037, 0, 0) if ms else 0    # LVM_GETEXTENDEDLISTVIEWSTYLE
# 坑：0x1036 是 **SET**（首版当 GET 用——会顺手清掉样式，且返回旧值骗过断言）；GET = 0x1037
chk("§77-11 多条表 LVS_EX_INFOTIP(0x400) 已置（悬停「第 N 条」）", bool(st & 0x400), "style=0x%X" % st)

proc.terminate()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
if fails:
    print("FAIL: " + "; ".join(fails))
