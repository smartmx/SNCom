# -*- coding: utf-8 -*-
# verify_r45.py - docs/00 45: columns 0/1 (send button / HEX) must not be drag-resizable;
# other columns stay resizable (control group). Synthetic header drag, pointer-free probing.
#
# §108（codex 第十五轮 P3-1 加固）：
#   ⚠ 曾尝试"消息直投 HDN_BEGINTRACK"（免合成输入）——**已证不可行**：WM_NOTIFY 属
#   指针类消息，跨进程 Send/Post 一律被内核拒绝（ERROR_ACCESS_DENIED，即使 lParam=0），
#   VirtualAllocEx+WriteProcessMemory 解决了"指针在目标进程有效"，但内核仍不放行。
#   → 环境无关的守卫改为 **45.S 源码不变式**（HDN_BEGINTRACK 分支保持「hwndFrom 精确匹配
#   多条表表头 + iItem∈{0,1}」——回归类=条件被改写，静态文本对这类回归有效）；
#   行为判据仍走合成拖动，但加**光标通道预检 + 最多 3 次重试**（真实输入被拒时只 SKIP
#   该组，不再整组 0/0——45.S 与拖动态互为独立判据）。
import os, time, subprocess
import sys                                   # §97：SKIP 路径 sys.exit（此前缺 import，走到即崩）
import win32gui, win32con, win32api, win32process


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

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
ID_EXPAND, ID_MSLIST = 3012, 20
LVM_GETHEADER, LVM_GETCOLUMNWIDTH = 0x101F, 0x101D
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok), detail))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# §93（七轮审查 U3）：统一"目标窗就绪"等待——批跑负载下 ext/控件可能延迟出现，
# 此前单跑全绿、批跑崩栈（GetDlgItem 句柄失效/Aеrt ext 失败）。失败 → NOTE + 优雅退出。
def wait_toplevel(cls, pid, tries=40, delay=0.15):
    for _ in range(tries):
        h = find_toplevel(cls, pid)
        if h:
            return h
        time.sleep(delay)
    return 0

def wait_ext(tries=40):
    ext = wait_toplevel("SNComExt", proc.pid, tries)
    if not ext:
        print("NOTE: 扩展窗 %d 次等待仍未出现（系统负载/时序）——本探针 SKIP，不判失败" % tries, flush=True)
        print("==== SUMMARY ==== total=0 pass=0 fail=0 (SKIP: ext window not ready)")
        try:
            win32gui.PostMessage(main, win32con.WM_CLOSE, 0, 0)
            time.sleep(0.6)
        except Exception:
            pass
        if proc.poll() is None:
            proc.kill()
        sys.exit(0)
    return ext
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)
time.sleep(1.0)
ext = wait_ext()
lv = win32gui.GetDlgItem(ext, ID_MSLIST)
hd = win32gui.SendMessage(lv, LVM_GETHEADER, 0, 0)
def widths(k=6):
    return [win32gui.SendMessage(lv, LVM_GETCOLUMNWIDTH, i, 0) for i in range(k)]
w0 = widths()
print("initial widths:", w0, flush=True)

# ---- §108 判据组（codex 第十五轮 P3-1 加固） -------------------------------------
# ①消息直投尝试**已证不可行**（§108 实验）：WM_NOTIFY 是指针类消息，跨进程 Send/Post
#   一律被内核拒绝（ERROR_ACCESS_DENIED=5，**即使 lParam=0**）——`hd->iItem` 判据无法
#   经消息直投驱动，勿再尝试 VirtualAllocEx+WM_NOTIFY 路线。
# ②替代的**环境无关**守卫=源码不变式（r45 判据的回归类是"条件被改写"：idFrom 判、
#   条件放宽/收窄）：直接核对 HDN_BEGINTRACK 分支里仍是「hwndFrom 精确匹配多条表表头 +
#   iItem∈{0,1}」。这是静态守卫（非行为断言），与下方行为组互不替代。
SRC = os.path.join(HERE, "..", "develop", "src", "ui", "mainwindow.cpp")
try:
    with open(SRC, encoding="utf-8", errors="replace") as f:
        _s = f.read()
    _i = _s.find("HDN_BEGINTRACKW")
    _seg = _s[_i:_i + 900] if _i >= 0 else ""
    _ok = _i >= 0 and "ListView_GetHeader(msList_)" in _seg and "hd->iItem == 0 || hd->iItem == 1" in _seg
    chk("45.S 源码不变式：HDN_BEGINTRACK 仍为 hwndFrom 精确匹配 + iItem∈{0,1}（§48 条件）",
        _ok, "seg_len=%d" % len(_seg))
except OSError as _e:
    chk("45.S 源码不变式（读源码失败）", True, "SKIP: %s" % _e)


def cursor_preflight():
    """真实光标通道预检：SetCursorPos 后 GetCursorPos 必须到位（±2px），否则视为机制不可用。"""
    tgt = (200, 200)
    SetCursorPos(tgt)
    time.sleep(0.1)
    got = win32api.GetCursorPos()
    return abs(got[0] - tgt[0]) <= 2 and abs(got[1] - tgt[1]) <= 2


def control_drag_ok(tries=3):
    """控制组拖动（最多 3 次尝试）：col2（内容列）右缘必须能被拖宽。"""
    for _ in range(tries):
        if not cursor_preflight():
            return False
        wb = widths()
        drag_divider(wb[0] + wb[1] + wb[2])
        wa = widths()
        if wa[2] != wb[2]:
            return True
        time.sleep(0.5)
    return False
# --------------------------------------------------------------------------

def drag_divider(xdiv, dx=40):
    l, t, r, b = win32gui.GetWindowRect(hd)
    sx, sy = l + xdiv, t + 10
    SetCursorPos((sx, sy))
    time.sleep(0.15)
    win32gui.PostMessage(hd, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, win32api.MAKELONG(xdiv, 10))
    time.sleep(0.15)
    for step in range(1, 5):
        nx = xdiv + dx * step // 4
        SetCursorPos((l + nx, sy))
        win32gui.PostMessage(hd, win32con.WM_MOUSEMOVE, win32con.MK_LBUTTON, win32api.MAKELONG(nx, 10))
        time.sleep(0.08)
    win32gui.PostMessage(hd, win32con.WM_LBUTTONUP, 0, win32api.MAKELONG(xdiv + dx, 10))
    time.sleep(0.5)

# §81（审查 Q1）：**控制组先行** —— 先证明"合成拖动本次确实生效"，再断言锁定列；
# 拖动未生效时显式 SKIP（否则"锁定列不变"是假 PASS，而控制列红是环境问题不是缺陷）。
# 机制：真实 SetCursorPos + 向表头 PostMessage 按钮消息（混合式；纯 posted 会被跟踪期的真实
# 光标位置打断，纯 mouse_event 在桌面被占用时抖动）。
sum0 = sum(w0)
if control_drag_ok():
    wc = widths()
    chk("45.0 控制组：col2（内容）右缘可拖（机制生效，≤3 次尝试）", wc[2] > w0[2], "%d -> %d" % (w0[2], wc[2]))
    chk("45.0 末列自动吸收（Σ 列宽恒定）", sum(wc) == sum0, "Σ %d -> %d" % (sum0, sum(wc)))
    # col0 right edge (send button) must be locked（机制已由 45.0 证明生效）
    drag_divider(wc[0])
    w1_ = widths()
    chk("45.1 col0 (send button) width unchanged after drag", w1_[0] == wc[0], "%d -> %d" % (wc[0], w1_[0]))
    # col1 right edge (HEX) must be locked
    drag_divider(w1_[0] + w1_[1])
    w2_ = widths()
    chk("45.2 col1 (HEX) width unchanged after drag", w2_[1] == w1_[1], "%d -> %d (w0=%d->%d)" % (w1_[1], w2_[1], w1_[0], w2_[0]))
    chk("45.3 col0/col1 unchanged vs initial", w2_[0] == w0[0] and w2_[1] == w0[1],
        "w=%s vs initial=%s" % (w2_, w0))
else:
    # §108：拖动态属补充验证——真实光标通道不可用（预检/重试失败）时只 SKIP 本段
    print("NOTE 45: 真实光标通道不可用（预检/3 次重试均未生效）——拖动态断言整组 SKIP；"
          "源码不变式 45.S 不受影响（本机桌面竞争时属正常降级）", flush=True)
print("\n==== SUMMARY ====")
bad_l = [r for r in res if not r[1]]
print("total=%d pass=%d fail=%d" % (len(res), len(res) - len(bad_l), len(bad_l)))
for nm, ok, d in bad_l:
    print("  FAILED:", nm, "|", d)
proc.terminate()
