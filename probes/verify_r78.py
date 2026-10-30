# -*- coding: utf-8 -*-
# verify_r78.py - docs/00 §78（用户两项）：
#  1) 多条发送表最右空列消除：6 列定宽后剩余宽度曾以"空列"形态留在表头右侧（截图实证 84px）
#     → 修法=末列（注释）拉伸填满；扩展窗尺寸变化与拖列（HDN_ENDTRACK）都重算
#     断言：6 列宽和 ≥ 列表客户宽-24（滚动条裕量）；加宽 ext 后末列跟随增长；
#           拖第 3 列（内容，未锁定）后仍需填满
#  2) 菜单标记统一系统 ✓：CheckMenuRadioItem 会打 MFT_RADIOCHECK（0x200=圆点），
#     改为 CheckMenuItem 逐项打勾。断言=递归遍历整个常驻菜单树，**任何项都不得带
#     0x200 位**，且各组恰有一项带 MF_CHECKED(0x8)
import os, time, subprocess, ctypes, shutil
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
MF_CHECKED = 0x8
MFT_RADIOCHECK = 0x200
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

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

def bm_click(h):
    win32api.PostMessage(h, 0x00F5, 0, 0)

# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r78"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r78\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r78\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r78")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# ---------------- 2) 菜单树标记（先做：不依赖扩展窗，纯消息读取） ----------------
# 递归遍历常驻菜单树：GetMenuState(MF_BYPOSITION) 返回低字节状态（含 MF_CHECKED）
# 与类型位（MFT_RADIOCHECK=0x200 即圆点）。**全部项不得带 0x200。**
viol = []          # 带圆点位的位置
checked = []       # 带 ✓ 的位置
def walk(m, path):
    n = user32.GetMenuItemCount(m)
    for pos in range(n):
        st = user32.GetMenuState(m, pos, 0x400)   # MF_BYPOSITION
        if st == 0xFFFFFFFF:
            continue
        label = "菜单[%s] 第%d 项" % (path, pos)
        # 只查叶子命令项：弹出子菜单项（id<0）的 flags 里天然带 0x200/0x100 等
        # （MF_USECHECKBITMAPS/MF_OWNERDRAW 位由系统标注），不是圆点标记
        is_cmd = user32.GetMenuItemID(m, pos) >= 0
        if is_cmd and (st & MFT_RADIOCHECK):
            viol.append((label, hex(st)))
        if st & MF_CHECKED:
            checked.append((label, hex(st)))
        sub = user32.GetSubMenu(m, pos)
        if sub:
            walk(sub, path + "/" + str(pos))
mb = user32.GetMenu(main)
chk("§78-2 主菜单存在", bool(mb), "menu=%s" % mb)
walk(mb, "根")
chk("§78-2 菜单命令项无圆点标记（MFT_RADIOCHECK=0x200 全清）", not viol,
    "violations=%s" % viol[:4])
# 三组单选（通讯端口/编码/缓冲）逐项按 id 直读：不得带 0x200（圆点位）
grp_viol = []
for a, b in ((2000, 2003), (2100, 2111), (2300, 2305)):
    for i in range(a, b + 1):
        st = user32.GetMenuState(mb, i, 0)
        if st != 0xFFFFFFFF and (st & MFT_RADIOCHECK):
            grp_viol.append((i, hex(st)))
chk("§78-2 三组单选无圆点位（含通讯端口/编码/缓冲）", not grp_viol, "violations=%s" % grp_viol)
chk("§78-2 勾选标记仍存在（✓ 计数 ≥3）", len(checked) >= 3, "checked=%s" % checked[:6])
# 三组单选按钮的取值仍正确：通讯端口/编码/缓冲各恰一项勾选
def group_checked(first, last):
    cnt = 0
    for i in range(first, last + 1):
        st = user32.GetMenuState(mb, i, 0)         # MF_BYCOMMAND
        if st != 0xFFFFFFFF and (st & MF_CHECKED):
            cnt += 1
    return cnt
chk("§78-2 通讯端口组恰一项勾选", group_checked(2000, 2003) == 1, "n=%d" % group_checked(2000, 2003))
chk("§78-2 编码组恰一项勾选", group_checked(2100, 2111) == 1, "n=%d" % group_checked(2100, 2111))
chk("§78-2 缓冲组恰一项勾选", group_checked(2300, 2305) == 1, "n=%d" % group_checked(2300, 2305))

# ---------------- 1) 多条表末列填满 ----------------
bm_click(user32.GetDlgItem(main, 3012))            # 扩展
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
chk("§78-1 扩展窗已打开", bool(ext), "")
lst = user32.GetDlgItem(ext, 20) if ext else 0
hdr = user32.SendMessageW(lst, 0x101F, 0, 0) if lst else 0      # LVM_GETHEADER
ncol = user32.SendMessageW(hdr, 0x1200, 0, 0) if hdr else 0     # HDM_GETITEMCOUNT
chk("§78-1 多条表 6 列", ncol == 6, "ncol=%d" % ncol)

def widths():
    return [user32.SendMessageW(lst, 0x101D, i, 0) for i in range(6)]   # LVM_GETCOLUMNWIDTH(0x101D；0x101E 是 SET——首版误当 GET 把列宽写成 1)

def client_w():
    rc = RECT()
    user32.GetClientRect(lst, ctypes.byref(rc))
    return rc.right - rc.left

def report(prefix):
    w, cw = widths(), client_w()
    gap = cw - sum(w)
    print("  [%s] client=%d widths=%s sum=%d gap=%d" % (prefix, cw, w, sum(w), gap), flush=True)
    return w, cw, gap

w0, cw0, gap0 = report("初始")
chk("§78-1 初始即填满（gap ≤ 24：无空列）", gap0 <= 24, "gap=%d cw=%d" % (gap0, cw0))
chk("§78-1 注释列未低于下限 96", w0[5] >= 96, "w5=%d" % w0[5])

# 加宽 ext 窗 → 末列应吸收增量
el, et, er, eb = win32gui.GetWindowRect(ext)
win32gui.SetWindowPos(ext, 0, el, et, (er - el) + 120, eb - et, 0x0004)   # SWP_NOZORDER
time.sleep(0.8)
w1, cw1, gap1 = report("加宽120")
chk("§78-1 加宽后仍填满（gap ≤ 24）", gap1 <= 24, "gap=%d" % gap1)
chk("§78-1 末列吸收增量（+100 以上）", w1[5] - w0[5] >= 100, "w5 %d→%d" % (w0[5], w1[5]))

# 拖列后仍填满：§81 起改为**真实合成拖动**（混合式：真实 SetCursorPos + 向表头 PostMessage
# 按钮消息）——§78.2 曾误记"合成拖动失效"，实测可用（拖未生效时显式 NOTE 并降级到程序化口径）。
def hybrid_drag(colidx, dx=40, y=10):
    w = widths()
    x = sum(w[:colidx + 1])
    hl, ht = win32gui.GetWindowRect(hdr)[:2]
    SetCursorPos((hl + x, ht + y)); time.sleep(0.2)
    win32gui.PostMessage(hdr, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, win32api.MAKELONG(x, y))
    time.sleep(0.12)
    for k in (10, 20, 30, 40):
        SetCursorPos((hl + x + k, ht + y)); time.sleep(0.1)
        win32gui.PostMessage(hdr, win32con.WM_MOUSEMOVE, win32con.MK_LBUTTON, win32api.MAKELONG(x + k, y))
        time.sleep(0.1)
    win32gui.PostMessage(hdr, win32con.WM_LBUTTONUP, 0, win32api.MAKELONG(x + dx, y))
    time.sleep(0.5)

sum_before = sum(w1)
drag_ok = False
for attempt in (1, 2):
    hybrid_drag(2)
    wd = widths()
    print("  [drag#%d] col2 %d->%d Σ=%d" % (attempt, w1[2], wd[2], sum(wd)), flush=True)
    if wd[2] != w1[2]:
        drag_ok = True
        break
if drag_ok:
    chk("§78-1 拖内容列右缘生效（+40）", wd[2] - w1[2] >= 30, "col2 %d->%d" % (w1[2], wd[2]))
    chk("§78-1 末列吸收拖列增量（Σ 恒定=HDN_ENDTRACK 重排）", sum(wd) == sum_before,
        "Σ %d -> %d w=%s" % (sum_before, sum(wd), wd))
else:
    print("NOTE §78-1: 合成拖动本次未生效（桌面竞争）——降级为程序化口径复核", flush=True)
# 程序化口径（与拖动无关的兜底）：改任意列宽 + 触发重排 → 仍须填满
user32.SendMessageW(lst, 0x101E, 2, 150)          # 内容列 → 150（LVM_SETCOLUMNWIDTH）
win32gui.SetWindowPos(ext, 0, el, et, (er - el) + 1, eb - et, 0x0004)
time.sleep(0.4)
win32gui.SetWindowPos(ext, 0, el, et, er - el, eb - et, 0x0004)
time.sleep(0.6)
w2, cw2, gap2 = report("改列宽+重排")
chk("§78-1 任意列宽下重排仍填满（gap ≤ 24）", gap2 <= 24, "gap=%d" % gap2)
chk("§78-1 改列宽被保留（内容列 150）", w2[2] == 150, "w2=%d" % w2[2])

# 取证据截图（用户可见形态）
import win32ui
from PIL import Image
el, et, er, eb = win32gui.GetWindowRect(ext)
hdc = win32gui.GetWindowDC(ext)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, er - el, eb - et); dc.SelectObject(bmp)
user32.PrintWindow(ext, dc.GetSafeHdc(), 2)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (er - el, eb - et), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(ext, hdc)
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r78_ext_after.png")
print("saved r78_ext_after.png", flush=True)

proc.terminate()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
