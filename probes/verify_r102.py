# -*- coding: utf-8 -*-
# verify_r102.py — §102 审查 P2-A/P2-B 端到端回归（沙箱 exe，不碰 dist 配置）：
#   A 配置端口启动恢复：cfg.port=次端口 → 组合框选中次端口；点「打开串口」后独占测试
#     证明实际打开的是配置端口（配置端口被占、首端口空闲）——修复前该断言必红
#     （SelectComboText 纯名匹配失败 → 回落 index 0 → 打开 COM1）。
#   B 启动立刻打开串口：open_on_start=true → 启动后按钮变「关闭串口」且端口被占；
#     缺省 false → 保持「打开串口」（负对照）。
# 前置：本机存在 ≥2 个可打开串口（无则 SKIP）；探针自建沙箱 probes/_probe/out/r102。
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
SRC = os.path.abspath(os.path.join(HERE, "..", "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "r102")
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

IDC_CMB_PORT, IDC_BTN_OPEN = 3014, 3017
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
OPEN_EXISTING, INVALID_HANDLE_VALUE = 3, -1
WM_GETTEXT, BM_CLICK, WM_CLOSE = 0x000D, 0x00F5, 0x0010
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


def find_ports(n):
    got = []
    for i in range(1, 25):
        nm = "COM%d" % i
        if port_openable(nm, False):
            got.append(nm)
            if len(got) >= n:
                break
    return got


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


ports = find_ports(2)
if len(ports) < 1:
    print("SKIP: 本机没有可打开串口，端口恢复断言需要真实口")
    sys.exit(0)
single_port = len(ports) < 2
p1 = ports[0]
p2 = ports[1] if not single_port else ""  # 单口环境：A 案降级为单口变体（判别力由 §104-A 承担）

try:
    # ---------- r102-A：配置端口启动恢复 + 实际打开正确端口 ----------
    cfgport = p1 if single_port else p2
    p, main = run({"mode": "serial", "port": cfgport})
    det = "main=%s cfg=%s" % (bool(main), cfgport)
    okA = False
    if main:
        cmb = user32.GetDlgItem(main, IDC_CMB_PORT)
        sel = cb_sel_text(cmb)
        okA1 = sel.split(" ")[0].strip() == cfgport        # 选中项前缀=配置端口
        user32.SendMessageW(user32.GetDlgItem(main, IDC_BTN_OPEN), BM_CLICK)
        time.sleep(1.5)
        okA2 = not port_openable(cfgport, True)            # 配置口被 SNCom 独占 = 真打开
        okA3 = port_openable(p1, True) if not single_port else True
        okA = okA1 and okA2 and okA3
        det += " | sel=%r okSel=%s 配置口被占=%s 另口空闲=%s" % (sel, okA1, okA2, okA3)
        if single_port:
            det += " | NOTE 单口环境：'非首项'判别力由 verify_r104-A1/A3 承担"
    close_app(p, main)
    res.append(("A 配置端口恢复+实际打开正确端口", okA, det))

    # ---------- r102-B：启动立刻打开串口（open_on_start）----------
    p, main = run({"mode": "serial", "port": p1, "open_on_start": True})
    det = "main=%s" % bool(main)
    okB = False
    if main:
        btn = user32.GetDlgItem(main, IDC_BTN_OPEN)
        t = ""
        for _ in range(40):                               # 至多 ~6s 等启动打开生效
            t = wtext(btn)
            if t == "关闭串口":
                break
            time.sleep(0.15)
        okB1 = t == "关闭串口"
        okB2 = not port_openable(p1, True)                # 端口确实被占
        okB = okB1 and okB2
        det += " | btn=%r okBtn=%s p1被占=%s" % (t, okB1, okB2)
    close_app(p, main)

    # 负对照：缺省 open_on_start=false → 不自动打开
    p, main = run({"mode": "serial", "port": p1})
    okB3 = False
    if main:
        time.sleep(1.0)
        okB3 = wtext(user32.GetDlgItem(main, IDC_BTN_OPEN)) == "打开串口"
    close_app(p, main)
    res.append(("B open_on_start 接通（含负对照）", okB and okB3,
                det + " | 缺省保持关闭=%s" % okB3))
finally:
    shutil.rmtree(BIN, ignore_errors=True)

print("==== verify_r102 (§102) ====", flush=True)
for name, ok, det in res:
    print("[%s] %s | %s" % ("PASS" if ok else "FAIL", name, det), flush=True)
fails = [n for n, ok, _ in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok, _ in res if ok), len(fails)), flush=True)
if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
