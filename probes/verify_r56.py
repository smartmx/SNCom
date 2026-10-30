# -*- coding: utf-8 -*-
# verify_r56.py - docs/00 §56:
#  1) wheel disabled on 加校验(cmb_checksum_) / 校验尾(cmb_chk_tail_) — the four combos
#     the §44 batch loop missed (created after the loop ran); REAL user path:
#     click-to-focus (ESC to close dropdown) then real mouse wheel.
#  2) tail option renamed 末尾追加校验[旧版无帧尾] -> 无帧尾追加校验 (len 11 -> 7, pointer-free).
import os, time, subprocess, ctypes, shutil
import sys                                   # §97：SKIP 路径 sys.exit（此前缺 import，走到即崩）
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

# §116（十七轮 P3-2）：环境性跳过单独计数（旧「chk(...,True,'SKIP')」口径并入此列）。
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
# sncom.json 写回 dist；转存日志也会落进 dist）。BIN = probes\_probe\out\r56。
BIN_DIR = os.path.abspath(os.path.join(HERE, "_probe", "out", "r56"))
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
        shutil.rmtree(BIN_DIR, ignore_errors=True)
        sys.exit(0)
    return ext

def cursel(c):
    return win32gui.SendMessage(c, 0x0147, 0, 0)   # CB_GETCURSEL

# §93（七轮审查 U3）：主窗/控件句柄可能因时序失效——统一 wait；失败优雅退出
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
if not main:
    print("NOTE r56: 主窗未出现（系统负载/时序）——本探针 SKIP，不判失败", flush=True)
    print("==== SUMMARY ==== total=0 pass=0 fail=0 (SKIP: main window not ready)")
    if proc.poll() is None:
        proc.kill()
    shutil.rmtree(BIN_DIR, ignore_errors=True)
    sys.exit(0)

def click_focus(c):
    l, t, r, b = win32gui.GetWindowRect(c)
    cx, cy = (l + r) // 2, (t + b) // 2
    SetCursorPos((cx, cy)); time.sleep(0.15)
    mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); time.sleep(0.06)
    mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(0.4)                                # dropdown opens
    keybd_event(win32con.VK_ESCAPE, 0, 0, 0)   # close, keep focus
    time.sleep(0.3)

def real_wheel(delta=-120, times=3):
    for _ in range(times):
        mouse_event(win32con.MOUSEEVENTF_WHEEL, 0, 0, delta, 0)
        time.sleep(0.08)
    time.sleep(0.4)

# ---- 1) 真实输入组（行为基准；被系统拒绝时本组 SKIP、不牵连其余） -----------------
targets = [
    ("加校验 cmb_checksum_", 3055),
    ("校验尾 cmb_chk_tail_", 3053),
    ("端口 cmb_port_ (control)", 3014),
    ("波特率 cmb_baud_ (control)", 3019),
]
if INPUT_OK:
    for name, cid in targets:
        c = win32gui.GetDlgItem(main, cid)
        assert c, name
        a = cursel(c)
        click_focus(c)
        b = cursel(c)
        real_wheel(-120, 3)
        d = cursel(c)
        if not INPUT_OK:
            # 本组依赖真实输入：中途被拒则"没输入"与"功能不对"不可分（§84 口径）→ SKIP
            skip("r56 真实输入组 wheel blocked: %s（输入中途被拒）" % name)
            continue
        chk("wheel blocked (real input): %s (sel %s -> %s)" % (name, a, d), a == b == d,
            "after_click=%s after_wheel=%s" % (b, d))
else:
    skip("r56 真实输入组 x4（输入被拒，桌面竞争；免鼠标组不受影响）")

# ---- 1b) renamed tail option: text length 11 -> 7 (CB_GETLBTEXTLEN, pointer-free) ----
tail = win32gui.GetDlgItem(main, 3053)
lens = [win32gui.SendMessage(tail, 0x0149, i, 0) for i in range(5)]   # CB_GETLBTEXTLEN
chk("tail option[4] renamed (len 11 -> 7)", lens[4] == 7, "lens=%s" % lens)

# ---- 2) 免鼠标组（§108 加固，codex 第十五轮 P3-1） --------------------------------
# 手法（§108 实验定案）：AttachThreadInput+SetFocus 非合成聚焦 + posted WM_MOUSEWHEEL
# （<WM_USER 无指针，内核允许；坐标取控件矩形中心——comctl32 列表只在矩形内滚）。
# 判别力已由 pre-§56 构建红对照实测（同一手法在该构建上复现改选，见下方 R 案）。
WM_MOUSEWHEEL, CB_GETCURSEL, LVM_GETTOPINDEX = 0x020A, 0x0147, 0x1027
ID_ASCII_LIST = 13
u32 = ctypes.WinDLL("user32", use_last_error=True)
ot = win32api.GetCurrentThreadId()


def mousefree_block(pid, hmain, combo_ids, tag):
    """免鼠标判据：先证通道（ASCII 列表须滚动），再逐 combo 聚焦+滚轮须不改选。
    返回 True=全部符合；None=前置未生效（本组 SKIP，不判失败）。"""
    tt = win32process.GetWindowThreadProcessId(hmain)[0]
    if not u32.AttachThreadInput(ot, tt, True):
        print("NOTE %s: AttachThreadInput 失败——免鼠标组 SKIP" % tag, flush=True)
        return None
    try:
        win32gui.PostMessage(hmain, 0x0111, 2218, 0)       # 小工具→ASCII 码表（开 ext+页 2）
        ext = wait_toplevel("SNComExt", pid)
        if not ext:
            print("NOTE %s: 扩展窗未出现——免鼠标组 SKIP" % tag, flush=True)
            return None
        lv = win32gui.GetDlgItem(ext, ID_ASCII_LIST)
        l, t, r, b = win32gui.GetWindowRect(lv)
        cx, cy = (l + r) // 2, (t + b) // 2
        top0 = win32gui.SendMessage(lv, LVM_GETTOPINDEX, 0, 0)
        u32.SetFocus(lv)
        time.sleep(0.3)
        for _ in range(6):
            win32gui.PostMessage(lv, WM_MOUSEWHEEL, win32api.MAKELONG(0, -120),
                                 win32api.MAKELONG(cx, cy))
            time.sleep(0.12)
        top1 = win32gui.SendMessage(lv, LVM_GETTOPINDEX, 0, 0)
        ok_ctrl = top1 != top0
        chk("%s 对照组：ASCII 列表滚轮生效（通道有效）" % tag, ok_ctrl,
            "top %d -> %d" % (top0, top1))
        if not ok_ctrl:
            print("NOTE %s: 对照组未滚动——『不改选』读数不可信，本组 SKIP" % tag, flush=True)
            return None
        ok_all = True
        for name, cid in combo_ids:
            c = win32gui.GetDlgItem(hmain, cid)
            if not c:
                chk("%s %s（句柄缺失）" % (tag, name), False, "cid=%d" % cid)
                ok_all = False
                continue
            l, t, r, b = win32gui.GetWindowRect(c)
            cx, cy = (l + r) // 2, (t + b) // 2
            a = cursel(c)
            u32.SetFocus(c)
            time.sleep(0.25)
            if u32.GetFocus() != c:
                skip("%s %s（聚焦未生效）" % (tag, name))
                continue
            for _ in range(3):
                win32gui.PostMessage(c, WM_MOUSEWHEEL, win32api.MAKELONG(0, -120),
                                     win32api.MAKELONG(cx, cy))
                time.sleep(0.12)
            d = cursel(c)
            chk("%s wheel blocked (mouse-free): %s (sel %s -> %s)" % (tag, name, a, d), a == d)
            ok_all = ok_all and (a == d)
        return ok_all
    finally:
        u32.AttachThreadInput(ot, tt, False)


mf = mousefree_block(proc.pid, main, targets, "免鼠标")
if mf is None:
    skip("免鼠标组（前置未生效）")

# ---- 3) 红对照：pre-§56 构建（probes/_probe/out/r56_old/sncom.exe） -------------
# 留存则用**同一免鼠标手法**跑旧构建：两个修复前漏网下拉必须复现改选（证明判据有判别力）。
OLD56 = os.path.join(HERE, "_probe", "out", "r56_old", "sncom.exe")
if os.path.exists(OLD56):
    p2 = subprocess.Popen([OLD56], cwd=os.path.dirname(OLD56))
    m2 = 0
    for _ in range(40):
        m2 = find_toplevel("SNComMain", p2.pid)
        if m2:
            break
        time.sleep(0.25)
    time.sleep(3.0)
    if m2:
        tt2 = win32process.GetWindowThreadProcessId(m2)[0]
        changed = []
        if u32.AttachThreadInput(ot, tt2, True):
            try:
                for name, cid in targets[:2]:              # 仅 §56 修复涉及且改选可测的两个
                    c = win32gui.GetDlgItem(m2, cid)
                    l, t, r, b = win32gui.GetWindowRect(c)
                    cx, cy = (l + r) // 2, (t + b) // 2
                    a = cursel(c)
                    u32.SetFocus(c)
                    time.sleep(0.25)
                    for _ in range(3):
                        win32gui.PostMessage(c, WM_MOUSEWHEEL, win32api.MAKELONG(0, -120),
                                             win32api.MAKELONG(cx, cy))
                        time.sleep(0.12)
                    changed.append(cursel(c) != a)
            finally:
                u32.AttachThreadInput(ot, tt2, False)
        chk("R 红对照：pre-§56 构建经同一手法复现改选（判别力）",
            bool(changed) and all(changed), "changed=%s" % changed)
    else:
        print("NOTE r56: 红对照旧构建主窗未出现——R SKIP", flush=True)
    if p2.poll() is None:
        p2.kill()
else:
    print("NOTE r56: 无 %s——R 红对照 SKIP（不判失败）" % OLD56, flush=True)

proc.terminate()
time.sleep(0.4)
shutil.rmtree(BIN_DIR, ignore_errors=True)

print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
