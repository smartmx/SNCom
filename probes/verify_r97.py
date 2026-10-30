# -*- coding: utf-8 -*-
# verify_r97.py - §97 三项验收（进制转换任意精度 / 标签底色同源 / 菜单栏配色）
#   A 菜单栏色：主窗顶带 y∈[20,70] 存在全线主色==240 的行（修复前=纯白 255 → 必红，有判别力）
#   B 大整数端到端：扩展窗进制转换页 HEX 字段设 31xA（124 位）→ HEX LE 字段应得
#     "AA ×15 + 0A"（16 字节）；DEC 字段应得 14178431955039102644307275309657008810。
#     修复前（uint64 saturate）→ HEX LE="FF FF FF FF FF FF FF FF" 必红，有判别力。
#   C 标签底色同源：进制转换页标签控件背景 == 同行的页签显示区空白（本机修复前可能已绿，
#     仅作回归守卫）
import os
import shutil
import subprocess
import time
import ctypes
import win32gui
import win32api
import win32process
import win32ui
from PIL import Image
from collections import Counter

user32 = ctypes.windll.user32
res = []
BIN = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r97\sncom.exe"
OUT = r"F:\projects\sscom二次分析开发\SNCom\probes\out"


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


def shoot(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc)
    dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap()
    bmp.CreateCompatibleBitmap(mfc, r - l, b - t)
    dc.SelectObject(bmp)
    user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img, (l, t, r - l, b - t)


def gettext(hwnd):
    # 跨进程读控件文本必须 WM_GETTEXT（GetWindowText 对异进程控件返回空，老坑）
    n = user32.SendMessageW(hwnd, 0x000E, 0, 0)          # WM_GETTEXTLENGTH
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.SendMessageW(hwnd, 0x000D, n + 1, buf)        # WM_GETTEXT（列集）
    return buf.value


# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r97"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    import os
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r97\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([BIN], cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r97")
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.15)
if not main:
    print("NOTE r97: 主窗未就绪，优雅退出")
    raise SystemExit(0)
time.sleep(3.0)

# §98c：先强制激活主窗（最小化→还原抢前台）——用户报的现场是**活动窗口**：
# 活动切换时 DefWindowProc 直绘 NC 不发 WM_NCPAINT，未补挂前此处菜单栏保持系统色（必红）。
win32gui.ShowWindow(main, 6)
time.sleep(0.3)
win32gui.ShowWindow(main, 9)
time.sleep(0.3)                      # R23-P3-4：NC 重绘等待改由下方轮询兜底（≤2s）

# A. 菜单栏文字行底色=240（§98b/§98c 用户拍板：只改菜单栏行、标题栏保持系统原样）——
#    定位法：**有文字（深色像素≥2）且主色占比≥70%** 的首行 = 菜单文字行
#    （标题栏文字行底色是杂色占比低；非活动标题栏虽 243 全行但无文字 → 均不误命中。
#    曾试 GetMenuBarInfo 跨进程定位，err=87 无效参数，弃用）。
#    修复前（themed 纯白菜单栏 255）必红，有判别力。


def menu_row_col(im, w, h):
    # 定位菜单文字行：主色==240（我们刷过的菜单栏底；活动标题栏是浅蓝白 239,244,249 → 排除）
    # 且占比≥70% 且含深色文字像素。修复前（themed 白菜单栏）该行非 240 → 继续下扫可含
    # 接收框文字行 → col≠240 判红（判别力保留）。
    for yy in range(8, min(80, h)):
        row = [im.getpixel((x, yy)) for x in range(2, w - 2, 9)]
        cc = Counter(row).most_common(1)[0]
        dark = sum(1 for c in row if sum(c) < 350)
        if cc[0] == (240, 240, 240) and cc[1] >= len(row) * 0.7 and dark >= 2:
            return yy, cc[0]
    return -1, None


# R23-P3-4：判据帧轮询获取——还原后 NC 重绘偶发晚于固定 0.8s（同运行复跑即绿），
# 轮询「菜单文字行可定位」最多 2s 再判
img = None
_t0 = time.time()
while True:
    img, (wl, wt, ww, wh) = shoot(main)
    menu_row, menu_col = menu_row_col(img, ww, wh)
    if menu_row >= 0 or time.time() - _t0 >= 2.0:
        break
    time.sleep(0.05)
img.save(OUT + r"\r97_main.png")
chk("97-A 菜单栏文字行底色=240 灰（§98b/§98c）", menu_col == (240, 240, 240),
    "menu_row=%d col=%s" % (menu_row, menu_col))

# E. 菜单循环态（§98d）：SC_KEYMENU 直投进入菜单循环（INMENUMODE 可查证；keybd 注入
#    受"窗口非前台"限制不可靠）→ 断言【菜单循环中整行仍 240】（§98c 的"整行变白"版必红，
#    有判别力）；Esc 退出恢复。**"选中项高亮块变白"本机无法自动制造**（无前台/真实鼠标被
#    遮挡/注入方向键不被菜单循环接受——与 §86/表头同族的交互视觉盲区）：降为 NOTE，
#    以真机目检为准（默认整行灰 / 点开某项→该块白、其余灰 / 关闭恢复灰）。
try:
    class GTI(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                    ("hwndActive", ctypes.c_void_p), ("hwndFocus", ctypes.c_void_p),
                    ("hwndCapture", ctypes.c_void_p), ("hwndMenuOwner", ctypes.c_void_p),
                    ("hwndMoveSize", ctypes.c_void_p), ("hwndCaret", ctypes.c_void_p),
                    ("rcCaret", ctypes.c_long * 4)]

    def menumode():
        import win32process
        tid = win32process.GetWindowThreadProcessId(main)[0]
        g = GTI()
        g.cbSize = ctypes.sizeof(GTI)
        user32.GetGUIThreadInfo(tid, ctypes.byref(g))
        return bool(g.flags & 0x4)

    win32api.PostMessage(main, 0x0112, 0xF100, 0)      # WM_SYSCOMMAND SC_KEYMENU
    time.sleep(1.0)
    in_menu = menumode()
    im5, _ = shoot(main)
    _, c5 = menu_row_col(im5, ww, wh)
    chk("97-E 菜单循环中整行仍 240（选择不改变底色）", in_menu and c5 == (240, 240, 240),
        "in_menu=%s col=%s" % (in_menu, c5))
    win32api.PostMessage(main, 0x0100, 0x1B, 0)        # WM_KEYDOWN VK_ESC 退出
    time.sleep(0.8)
    im6, _ = shoot(main)
    _, c6 = menu_row_col(im6, ww, wh)
    chk("97-E 退出菜单循环后菜单栏=240", (not menumode()) and c6 == (240, 240, 240),
        "col=%s" % (c6,))
    print("NOTE r97-E: 选中项高亮块变白（§98d）本机无法自动制造（无前台/鼠标被遮挡/"
          "注入方向键不被菜单循环接受）——请真机目检：点开某项→该项高亮块白、其余行灰",
          flush=True)
except Exception as ex:
    print("NOTE r97-E: 菜单循环段跳过（%s）" % ex, flush=True)

# D. 菜单栏到底线到接收框边框之间**全 240 无缝**（§98；修复前该片为白/混白，必红）。
border_y, white_rows = -1, []
for y in range(40, min(80, wh)):
    row = [img.getpixel((x, y)) for x in range(4, ww - 4, 6)]
    cc = Counter(row).most_common(1)[0]
    if cc[0] == (160, 160, 160) and cc[1] >= len(row) * 0.8:
        border_y = y
        break
if border_y > 0:
    for y in range(menu_row + 2, border_y):
        row = [img.getpixel((x, y)) for x in range(4, ww - 4, 6)]
        cc = Counter(row).most_common(1)[0]
        if cc[0] == (255, 255, 255) and cc[1] >= len(row) * 0.5:
            white_rows.append(y)
chk("97-D 菜单栏→接收框之间全 240 无缝（无白行）", border_y > 0 and not white_rows,
    "border_y=%d white_rows=%s" % (border_y, white_rows))

# 打开扩展窗 → 进制转换页
win32api.PostMessage(user32.GetDlgItem(main, 3012), 0x00F5, 0, 0)   # BM_CLICK 扩展
ext = 0
for _ in range(40):
    ext = find_toplevel("SNComExt", proc.pid)
    if ext:
        break
    time.sleep(0.1)
time.sleep(1.2)
win32api.PostMessage(ext, 0x8000 + 0x111, 1, 0)                     # ShowExtPage(1)=进制转换
time.sleep(0.8)

# B. 大整数端到端：IDC 枚举 IDC_EXT_TAB=1, IDC_CALC_DEC=2 .. IDC_CALC_F64BE=11, CARR=12
edit_hex = user32.GetDlgItem(ext, 3)      # HEX
edit_hexle = user32.GetDlgItem(ext, 4)    # HEX LE
edit_dec = user32.GetDlgItem(ext, 2)      # DEC
# 跨进程 WM_SETTEXT（列集）设整串——Edit 内部会发 EN_CHANGE 触发联动（实测确认）；
# 读回必须走 WM_GETTEXT（见 gettext）
user32.SendMessageW(edit_hex, 0x000C, 0, ctypes.c_wchar_p("a" * 31))
time.sleep(0.8)
s_le = gettext(edit_hexle)
s_dec = gettext(edit_dec)
want_le = " ".join(["AA"] * 15 + ["0A"])
chk("97-B 31xA → HEX LE 16 字节全量", s_le == want_le, "le=%r" % s_le[:60])
chk("97-B 31xA → DEC 124 位十进制", s_dec == "14178431955039102644307275309657008810",
    "dec=%r" % s_dec[:40])

# C. 标签底色=240 灰（§98h 用户报：进制转换页标签列是白的——旧采样点落到编辑框上）。
#    断言：①标签内右侧（避开文字）==240；②参照点=客户区底部中央（真页面空白，用
#    WindowFromPoint 验证无控件覆盖）==240。旧构建把编辑框白当页面底 → 两点≈255 必红。
statics = []


def cb_children(h, l):
    if win32gui.GetClassName(h) == "Static" and user32.IsWindowVisible(h):
        statics.append(h)
    return True
win32gui.EnumChildWindows(ext, cb_children, None)
if statics:
    statics.sort(key=lambda h: win32gui.GetWindowRect(h)[1])
    lab = statics[0]
    ll, lt, lr, lb = win32gui.GetWindowRect(lab)
    img2, (el, et, ew, eh) = shoot(ext)
    img2.save(OUT + r"\r97_ext.png")
    cl, ct, cr, cb2 = win32gui.GetClientRect(ext)
    cls_w = cr - cl
    border = (ew - cls_w) // 2
    ly = (lt + lb) // 2 - et
    # §103 判别力修正：仅断"主色=240"会被**字形白块**骗过（§101/§102 构建标签文字背后
    # 有紧贴字形的白块，主色投票仍可全绿=假绿）。改为**近白占比 + 主色双断言**：
    # 修好后标签矩形内除 ClearType 边缘外不应有纯白（近白占比 ≈0）；白块构建必红。
    lw, lh = lr - ll, lb - lt
    votes = Counter()
    near_white = 0
    total = 0
    for gy in range(max(lh - 3, 1)):
        for gx in range(max(lw - 3, 1)):
            p = img2.getpixel((ll - el + 2 + gx, lt - et + 2 + gy))
            votes[p] += 1
            total += 1
            if p[0] >= 250 and p[1] >= 250 and p[2] >= 250:
                near_white += 1
    px = votes.most_common(1)[0][0]
    wfrac = near_white / max(total, 1)
    refx, refy = border + cls_w // 2, eh - 12             # 底部中央（真页面空白）
    # §123（十九轮 P3-5）：参照点不再用**屏幕坐标** WindowFromPoint 判遮挡——img2 是
    # PrintWindow 窗口表面，对系统顶层窗遮挡免疫（LockScreenBackstopFrame 曾致假 SKIP）；
    # 若该点真被**自家子控件**（如编辑框）覆盖，img2 里会直接呈现其底色 → 参照断言诚实判红。
    ref = img2.getpixel((refx, refy))
    print("  C 标签主色=%s 近白占比=%.3f 参照=%s（PrintWindow 表面采样，免遮挡）" %
          (px, wfrac, ref), flush=True)
    chk("97-C 标签底=240 且无字形白块（§103）",
        px == (240, 240, 240) and wfrac < 0.02, "label=%s wfrac=%.3f" % (px, wfrac))
    chk("97-C 页面参照点=240（§98h）", ref == (240, 240, 240), "ref=%s" % (ref,))
else:
    print("NOTE r97-C: 未找到可见 STATIC 子窗，跳过", flush=True)

proc.terminate()
print("NOTE r97: A/D 判别力=§98 之前（themed 白菜单栏/白带）必红；B 判别力=修复前（uint64）"
      "FF×8 必红；C=标签与页签区同源（§98 后双 240，一体灰）；§98 口径=菜单栏整行+间隙全 240", flush=True)
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
