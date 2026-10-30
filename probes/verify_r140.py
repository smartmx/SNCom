# -*- coding: utf-8 -*-
# verify_r140.py - docs/00 §140（用户需求）：发送菜单新增「启用终端仿真功能」总开关。
#  未勾选：终端仿真设置子菜单整条置灰、终端完全无效（接收区不响应键盘/IME）；
#  勾选：子菜单可选并生效（方式A/B/回显/换行按既有语义）。
# 判据：
#  A) 代码级：枚举 id/菜单构建/SyncMenuChecks 灰亮/ApplyTerminal 传真值/TermSend 单点闸/配置键
#  B) 行为级（沙箱 + 回环 + 状态栏 TX/RX 计数长度观测——SB_GETTEXTLENGTHW 跨进程安全）：
#   B1 默认关（子菜单灰、总开关未勾） B2 默认关时 WM_CHAR×5 计数不动
#   B3 菜单勾选后子菜单可选 B4 按键方式下 WM_CHAR×5 计数增长
#   B5 保存参数后 json term_enable=true B6 再关总开关后 WM_CHAR×5 计数又不动
import os, re, glob, time, shutil, subprocess, ctypes, json
import win32gui, win32con, win32api, win32process

user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
SNCOM = os.path.dirname(HERE)
DEV = os.path.join(SNCOM, "develop")
DIST = os.path.join(DEV, "dist")
BIN = os.path.join(HERE, "_bin")
os.makedirs(BIN, exist_ok=True)

ID_TERM_ENABLE = 2401
ID_TERM_KEY = 2211          # 隐式枚举：2100 段推算（IDM_SEND_HELP=2216 已实证）
ID_SAVEPARAMS = 3011

class MII(ctypes.Structure):
    # MENUITEMINFOW 全字段（x64 共 80B）——cbSize 过小会被 GetMenuItemInfoW 拒绝
    _fields_ = [("cbSize", ctypes.c_uint), ("fMask", ctypes.c_uint),
                ("fType", ctypes.c_uint), ("fState", ctypes.c_uint),
                ("wID", ctypes.c_uint), ("hSubMenu", ctypes.c_void_p),
                ("hbmpChecked", ctypes.c_void_p), ("hbmpUnchecked", ctypes.c_void_p),
                ("dwItemData", ctypes.c_void_p), ("dwTypeData", ctypes.c_wchar_p),
                ("cch", ctypes.c_uint), ("hbmpItem", ctypes.c_void_p)]

res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

# ---------- A) 代码级 ----------
def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()
mw_cpp = read(os.path.join(DEV, "src", "ui", "mainwindow.cpp"))
mw_h = read(os.path.join(DEV, "src", "ui", "mainwindow.h"))
st_h = read(os.path.join(DEV, "src", "core", "settings.h"))
st_cpp = read(os.path.join(DEV, "src", "core", "settings.cpp"))

chk("A1 枚举与菜单构建（2401+总开关项+分隔线）",
    "IDM_TERM_ENABLE = 2401" in mw_cpp
    and 'IDM_TERM_ENABLE, L"启用终端仿真功能"' in mw_cpp
    and mw_cpp.index('IDM_TERM_ENABLE, L"启用终端仿真功能"') < mw_cpp.index('L"终端仿真设置"'))
chk("A2 SyncMenuChecks 用成员句柄反查灰亮子菜单",
    "HMENU mTerm = mTermMenu_;" in mw_cpp
    and "if (GetSubMenu(mSend, i) == mTerm) { termPos = i; break; }" in mw_cpp
    and "CheckMenuItem(mSend, IDM_TERM_ENABLE," in mw_cpp)
chk("A3 ApplyTerminal 传真值 + TermSend 单点闸",
    "rxv_.SetTerminal(cfg_.term_enable," in mw_cpp
    and re.search(r"void MainWindow::TermSend[^}]*?if \(!cfg_\.term_enable\) return;", mw_cpp, re.S) is not None)
chk("A4 配置键 term_enable（默认 false + load/save/managed）",
    "bool term_enable = false;" in st_h
    and 'root->B(L"term_enable", term_enable)' in st_cpp
    and 'o->Set(L"term_enable", JsonValue::Bool(term_enable))' in st_cpp
    and 'L"term_enable", L"term_enter_send"' in st_cpp)

# ---------- B) 行为级 ----------
subprocess.run(["taskkill", "/F", "/IM", "sncom.exe"], capture_output=True)
time.sleep(1.2)
for old in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")) + glob.glob(os.path.join(BIN, "sncom.json")):
    os.remove(old)
shutil.copy2(os.path.join(DIST, "sncom.exe"), os.path.join(BIN, "sncom.exe"))
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)
mb = win32gui.GetMenu(main)
mSend = win32gui.GetSubMenu(mb, 4)
mTermMenu = None
for i in range(win32gui.GetMenuItemCount(mSend)):
    if win32gui.GetSubMenu(mSend, i):
        mTermMenu = win32gui.GetSubMenu(mSend, i)
        break
rx = win32gui.FindWindowEx(main, 0, "SNComRxView", None)
sb = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
chk("B0 前置：菜单/接收视图/状态栏句柄就位", bool(mb and mSend and rx and sb),
    "mSend=%s rx=%s sb=%s" % (bool(mSend), bool(rx), bool(sb)))

def menu_state():
    # popup = 「终端仿真设置」子菜单（mSend 里唯一 popup）：灰态用 GetMenuItemInfo.fState 读
    # （GetMenuState 对 popup 只回报 MF_POPUP|项数=0x510，灰位丢失——实测 0x510 两态相同）。
    import win32con as _wc
    pos = None
    for i in range(win32gui.GetMenuItemCount(mSend)):
        if win32gui.GetSubMenu(mSend, i):
            pos = i
            break
    st_popup = 0
    if pos is not None:
        # ⚠ 结构体必须给全 12 字段（cbSize 要 ≥ sizeof(MENUITEMINFOW)）；只给前 6 字段时
        # cbSize=32 过小，GetMenuItemInfoW 静默失败、fState 恒 0（曾致 B1/B6 假红）。
        mii = MII(); mii.cbSize = ctypes.sizeof(mii); mii.fMask = 0x1   # MIIM_STATE
        ctypes.windll.user32.GetMenuItemInfoW(mSend, pos, 1, ctypes.byref(mii))
        st_popup = mii.fState
    st_enable = win32gui.GetMenuState(mSend, ID_TERM_ENABLE, _wc.MF_BYCOMMAND) or 0
    return st_popup, st_enable
def sb_len(part):
    return user32.SendMessageW(sb, 0x040C, part, 0)     # SB_GETTEXTLENGTHW
def type_keys(n=5):
    for _ in range(n):
        win32api.PostMessage(rx, win32con.WM_CHAR, ord("A"), 0)
        time.sleep(0.02)
def counters_stable(sec=1.5):
    l1, l2 = sb_len(1), sb_len(2)
    t0 = time.time()
    while time.time() - t0 < sec:
        time.sleep(0.1)
        if (sb_len(1), sb_len(2)) != (l1, l2):
            return False
    return True

sp, se = menu_state()
chk("B1 默认关：子菜单置灰 + 总开关未勾且可选",
    (sp & 0x00000001) != 0 and (se & 0x00000008) == 0 and (se & 0x00000001) == 0,
    "popup_state=0x%x enable_state=0x%x (GRAYED=1 CHECKED=8)" % (sp, se))
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # 回环演示（有链路）
time.sleep(1.2)
chk("B2 默认关：WM_CHAR×5 计数不动（接收区不响应键盘）", counters_stable(), "静默观测 1.5s")

win32api.PostMessage(main, win32con.WM_COMMAND, ID_TERM_ENABLE, 0)
time.sleep(0.3)
sp, se = menu_state()
chk("B3 勾选总开关：子菜单可选 + 总开关已勾", (sp & 0x00000001) == 0 and (se & 0x00000008) != 0,
    "popup_state=0x%x enable_state=0x%x" % (sp, se))
win32api.PostMessage(main, win32con.WM_COMMAND, ID_TERM_KEY, 0)   # 按键立即发送
time.sleep(0.3)
type_keys(5)
grew = False
l1, l2 = sb_len(1), sb_len(2)
t0 = time.time()
while time.time() - t0 < 1.5:
    time.sleep(0.1)
    if (sb_len(1), sb_len(2)) != (l1, l2):
        grew = True
        break
st_key = win32gui.GetMenuState(mTermMenu, ID_TERM_KEY, win32con.MF_BYCOMMAND) or 0
chk("B4 启用+按键方式：WM_CHAR×5 计数增长（键盘生效）+菜单勾选态就位", grew and (st_key & 0x00000008) != 0,
    "IDM_TERM_KEY state=0x%x" % st_key)

win32api.PostMessage(user32.GetDlgItem(main, ID_SAVEPARAMS), 0x00F5, 0, 0)   # 保存参数
time.sleep(1.0)
try:
    with open(os.path.join(BIN, "sncom.json"), encoding="utf-8") as f:
        cfg = json.load(f)
    chk("B5 持久化：json term_enable=true", cfg.get("term_enable") is True,
        "term_enable=%r" % (cfg.get("term_enable"),))
except Exception as ex:
    chk("B5 持久化：json term_enable=true", False, "读配置失败 %s" % ex)

win32api.PostMessage(main, win32con.WM_COMMAND, ID_TERM_ENABLE, 0)   # 再关
time.sleep(0.3)
sp, se = menu_state()
chk("B6 再关总开关：子菜单又置灰", (sp & 0x00000001) != 0, "popup_state=0x%x" % sp)
type_keys(5)
chk("B7 再关后 WM_CHAR×5 计数不动", counters_stable(), "静默观测 1.5s")
proc.terminate()
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
