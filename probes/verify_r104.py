# -*- coding: utf-8 -*-
# verify_r104.py — §104（codex 第十三轮 P2-A / P3-1）端到端回归（沙箱 exe，不碰 dist 配置）：
#   A 配置端口缺失 → 「COMx（未找到）」占位项选中、**不回落到第一项**：
#     A1 open_on_start=false：选中含 "未找到"、按钮「打开串口」、真串口无人占用；
#     A2 open_on_start=true：**绝不误开别的串口**（p1/p2 均空闲）且按钮进入「取消」（失败重连中）
#        ——修复前该断言必红（回落首项 → 直接打开 COM1）；
#     A3 外发 WM_DEVICECHANGE(DBT_DEVNODES_CHANGED) 触发 RefreshPorts（热插拔刷新路径）：
#        保持占位选择（keepPort 缺失不回落），选中文本仍含 "未找到"。
#   B §102 回归：存在端口 + open_on_start → 真打开该端口（负向：不影响正常恢复）。
#   D/E 失败重连门控（P3-1）：探针独占 p1 制造「打开失败（占用）」——
#     D auto_reconnect=false → 按钮保持「打开串口」（不进入重连循环）；
#     E auto_reconnect=true（缺省）→ 按钮变「取消」（保持 参照实现 语义）。
# 前置：本机存在 ≥1 个可打开串口（无则 SKIP）；单口环境下自动裁剪依赖第二口的断言
# （配置口=唯一口时「非首项」判别力由 A1/A3 的占位断言承担）。沙箱 probes/_probe/out/r104。
import ctypes
import json
import os
import shutil
import subprocess
import sys
import time

import win32con
import win32gui
import win32process

HERE = os.path.dirname(os.path.abspath(__file__))
# SNCOM_EXE 可覆盖被测 exe（红/绿对照用：指向修复前构建跑出红，再指向当前构建跑出绿）
SRC = os.environ.get("SNCOM_EXE") or os.path.abspath(
    os.path.join(HERE, "..", "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "r104")
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

IDC_CMB_PORT, IDC_BTN_OPEN = 3014, 3017
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
OPEN_EXISTING, INVALID_HANDLE_VALUE = 3, -1
WM_GETTEXT, WM_CLOSE = 0x000D, 0x0010
WM_DEVICECHANGE, DBT_DEVNODES_CHANGED = 0x0219, 0x0007
CB_GETLBTEXT, CB_GETCURSEL = 0x0148, 0x0147
res = []


def port_openable(name, exclusive):
    """exclusive=True 时独占（Share=0）打开——SNCom 已占用则失败。"""
    h = kernel32.CreateFileW("\\\\.\\" + name, GENERIC_READ | GENERIC_WRITE,
                             0 if exclusive else 3, None, OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE:
        return False
    kernel32.CloseHandle(h)
    return True


def port_hold(name):
    """探针独占持有该端口（模拟被别的程序占用）；返回句柄。"""
    return kernel32.CreateFileW("\\\\.\\" + name, GENERIC_READ | GENERIC_WRITE,
                                0, None, OPEN_EXISTING, 0, None)


def find_ports(n):
    got = []
    for i in range(1, 25):
        nm = "COM%d" % i
        if port_openable(nm, False):
            got.append(nm)
            if len(got) >= n:
                break
    return got


def absent_port():
    """返回一个本机不存在的端口名（COM60 起向后找）。"""
    for i in range(60, 90):
        nm = "COM%d" % i
        if not port_openable(nm, False):
            return nm
    return "COM99"


def find_toplevel(cls, pid):
    out = []

    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True

    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


def wtext(h):
    b = ctypes.create_unicode_buffer(256)
    user32.SendMessageW(h, WM_GETTEXT, 256, b)
    return b.value


def cb_sel_text(h):
    i = user32.SendMessageW(h, CB_GETCURSEL, 0, 0)
    if i < 0:
        return ""
    b = ctypes.create_unicode_buffer(256)
    user32.SendMessageW(h, CB_GETLBTEXT, i, b)
    return b.value


def run(cfg):
    shutil.rmtree(BIN, ignore_errors=True)
    os.makedirs(BIN)
    shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
    with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
        json.dump(cfg, f)
    p = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
    main = 0
    for _ in range(70):
        main = find_toplevel("SNComMain", p.pid)
        if main:
            break
        time.sleep(0.15)
    time.sleep(3.0)                      # 等 UI-6 首轮 / open_on_start 生效
    return p, main


def close_app(p, main):
    if main:
        win32gui.PostMessage(main, WM_CLOSE, 0, 0)
    for _ in range(30):
        if p.poll() is not None:
            break
        time.sleep(0.15)
    if p.poll() is None:
        p.kill()
    time.sleep(0.6)                      # 等端口释放


def free(nm):
    """端口空闲（含"无此端口名"→视为空闲，便于单口环境裁剪断言）。"""
    return (not nm) or port_openable(nm, True)


ports = find_ports(2)
if len(ports) < 1:
    print("SKIP: 本机没有可打开串口，本组断言需要至少一个真实口")
    sys.exit(0)
p1 = ports[0]
p2 = ports[1] if len(ports) > 1 else ""      # 单口环境：p2 为空 → 相关断言自动裁剪
absent = absent_port()

try:
    # ---------- A：配置端口缺失 → 占位项选中、不回落到第一项 ----------
    p, main = run({"mode": "serial", "port": absent})
    det = "absent=%s main=%s" % (absent, bool(main))
    okA = False
    if main:
        cmb = user32.GetDlgItem(main, IDC_CMB_PORT)
        btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
        sel1 = cb_sel_text(cmb)
        okA1 = sel1.startswith(absent) and "未找到" in sel1 and not sel1.startswith(p1)
        okA1b = wtext(btn) == "打开串口"
        okA1c = free(p1) and free(p2)                 # 谁也没被开
        # A3：热插拔刷新路径 —— 外发设备变更，保持占位（keepPort 缺失不回落）
        win32gui.PostMessage(main, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0)
        time.sleep(1.2)
        sel2 = cb_sel_text(cmb)
        okA3 = sel2.startswith(absent) and "未找到" in sel2
        okA = okA1 and okA1b and okA1c and okA3
        det += " | sel=%r btn=%r 真口空闲=%s 刷新后sel=%r" % (
            sel1, wtext(btn), okA1c, sel2)
    close_app(p, main)
    res.append(("A1/A3 缺失端口占位选中+刷新保持（不回落首项）", okA, det))

    # ---------- A2：缺失端口 + open_on_start=true → 绝不误开别的口 ----------
    p, main = run({"mode": "serial", "port": absent, "open_on_start": True})
    det = "absent=%s main=%s" % (absent, bool(main))
    okA2 = False
    if main:
        btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
        t = ""
        for _ in range(30):               # 至多 ~4.5s 观察失败重连状态
            t = wtext(btn)
            if t == "取消":
                break
            time.sleep(0.15)
        free1 = free(p1)                  # 修复前：首个真口被静默打开 → False
        free2 = free(p2)
        sel = cb_sel_text(user32.GetDlgItem(main, IDC_CMB_PORT))
        okA2 = t == "取消" and free1 and free2 and sel.startswith(absent)
        det += " | btn=%r sel=%r p1空闲=%s p2空闲=%s" % (t, sel, free1, free2)
    close_app(p, main)
    res.append(("A2 缺失端口+open_on_start 不误开他口（失败重连中）", okA2, det))

    # ---------- B：§102 回归——存在端口 + open_on_start → 真打开 ----------
    cfgport = p2 or p1                    # 有第二个口时用次口（可判"非首项"），否则用唯一口
    p, main = run({"mode": "serial", "port": cfgport, "open_on_start": True})
    det = "cfg=%s(次口=%s) main=%s" % (cfgport, bool(p2), bool(main))
    okB = False
    if main:
        btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
        t = ""
        for _ in range(40):
            t = wtext(btn)
            if t == "关闭串口":
                break
            time.sleep(0.15)
        okB1 = t == "关闭串口"
        okB2 = not port_openable(cfgport, True)        # 配置口被 SNCom 独占
        okB3 = free(p1) if cfgport != p1 else True     # 首口未被误开（配置口≠首口时才有判别力）
        okB = okB1 and okB2 and okB3
        det += " | btn=%r 配置口被占=%s 另口空闲=%s" % (t, okB2, okB3)
    close_app(p, main)
    res.append(("B §102 回归：存在端口正常自动打开", okB, det))

    # ---------- D/E：打开失败的重连门控（P3-1，探针独占 p1）----------
    hhold = port_hold(p1)
    hold_ok = hhold != INVALID_HANDLE_VALUE
    if hold_ok:
        # D：auto_reconnect=false → 失败只提示，按钮保持「打开串口」
        p, main = run({"mode": "serial", "port": p1, "open_on_start": True,
                       "auto_reconnect": False})
        det = "hold=%s main=%s" % (hold_ok, bool(main))
        okD = False
        if main:
            btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
            time.sleep(2.0)                            # 给重连循环充足时间现形
            t = wtext(btn)
            okD = t == "打开串口"
            det += " | btn=%r" % t
        close_app(p, main)
        res.append(("D 失败不重连（auto_reconnect=false）", okD, det))

        # E：缺省 auto_reconnect=true → 失败进入重连（按钮「取消」）
        p, main = run({"mode": "serial", "port": p1, "open_on_start": True})
        det = "hold=%s main=%s" % (hold_ok, bool(main))
        okE = False
        if main:
            btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
            t = ""
            for _ in range(30):
                t = wtext(btn)
                if t == "取消":
                    break
                time.sleep(0.15)
            okE = t == "取消"
            det += " | btn=%r" % t
        close_app(p, main)
        res.append(("E 失败进重连（auto_reconnect 缺省 true）", okE, det))
        kernel32.CloseHandle(hhold)
    else:
        res.append(("D/E 失败重连门控", True, "SKIP: 无法独占 %s" % p1))
finally:
    shutil.rmtree(BIN, ignore_errors=True)

print("==== verify_r104 (§104) ====", flush=True)
for name, ok, det in res:
    print("[%s] %s | %s" % ("PASS" if ok else "FAIL", name, det), flush=True)
fails = [n for n, ok, _ in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok, _ in res if ok), len(fails)), flush=True)
if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
