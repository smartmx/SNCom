# -*- coding: utf-8 -*-
# verify_r110.py — §110 终端仿真（M6 首增量）：接收区直接输入 E2E（免合成输入）
#   0 空屏尾巴可见（§111 P2-2 回归）：未连任何链路、接收区无一行数据时键入 Z → PrintWindow
#     位图必须变化（修复前：DrawTermTail 在 totalRows_==0 时直接 return，键入不可见）；随后退格清尾
#   A 回车方式（带回显 + LF 补全）：WM_CHAR "ABC" + VK_RETURN → TX 经软件回环回来 →
#     转存日志含 "ABC\r\n"（编码 gbk，ASCII 字节同形）
#   B 按键方式（无回车也发）：WM_COMMAND 2210 → WM_CHAR "X"、"Y" → 日志新增内容含 X/Y
#     （相位前快照作负对照：X/Y 不可能预先存在）
#   C 回车方式退格粒度：WM_COMMAND 2209 → WM_CHAR "Q" + VK_BACK + VK_RETURN →
#     日志新增不含 "Q"（本地行已被退格删除，中文不残留半字节=行级语义）
#   D 连按回车不重复发（§111 P2-1 回归）：键入 M + 回车 + 回车 → 日志中 "M\r\n" 之后紧跟空行
#     （修复前：第二回车重发上一行，序列为 M\r\nM\r\n，不含 M\r\n\r\n）
#   手法：posted WM_CHAR/WM_KEYDOWN（<WM_USER 非指针消息，内核允许）；软件回环（2222）提供链路；
#   转存日志（「接收数据到文件」）做数据级断言——**读前必须先取消勾选释放句柄**（§83 坑），
#   每相位 OFF→读→ON 重新勾选（新建日志文件=干净快照；文件名秒级时间戳，切换间隔 >1s）。
# 沙箱 exe（probes/_probe/out/r110），不碰 dist 配置。
import ctypes
import glob
import json
import os
import shutil
import subprocess
import sys
import time

import win32gui
import win32process

gdi32 = ctypes.windll.gdi32
user32 = ctypes.windll.user32

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, "..", "develop"))
SRC = os.environ.get("SNCOM_EXE") or os.path.join(DEV, "dist", "sncom.exe")
BIN = os.path.join(HERE, "_probe", "out", "r110")
WM_COMMAND, WM_CLOSE, WM_CHAR, WM_KEYDOWN = 0x0111, 0x0010, 0x0102, 0x0100
BM_CLICK, VK_RETURN, VK_BACK = 0x00F5, 0x0D, 0x08
IDM_TOOL_LOOP, IDM_TERM_ENTER, IDM_TERM_KEY = 2222, 2210, 2211   # §110：终端项 2210/2211/2212/2213
res = []


def chk(name, ok, det=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + det if det else ""), flush=True)


def find_toplevel(cls, pid):
    out = []

    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True

    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


def child(parent, rx_view_only):
    out = []

    def cb(h, l):
        c = win32gui.GetClassName(h)
        t = win32gui.GetWindowText(h)
        if rx_view_only and c == "SNComRxView":
            out.append(h)
        if (not rx_view_only) and c == "Button" and "接收" in t:
            out.append(h)
        return True

    win32gui.EnumChildWindows(parent, cb, None)
    return out


def newest_log():
    logs = sorted(glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")))
    return logs[-1] if logs else ""


def read_log(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        return b"<READ-FAIL %s>" % str(e).encode()


def log_payloads(data):
    """转存日志 → 各显示行的**有效负载**列表（剥掉 "[时:分:秒.毫]" 与 3 字方向标记）。
    日志每行 = 前缀 + 行文本 + CRLF；行文本自带 CRLF 时拆成的空尾行同样成一条负载 "" 。"""
    import re
    text = data.decode("gbk", "replace")
    out = []
    for rec in text.split("\r\n"):
        if not rec:
            continue
        out.append(re.sub(r"^\[\d{2}:\d{2}:\d{2}\.\d{3}\] .{3}", "", rec))
    return out


def dark_rows(hwnd):
    """PrintWindow 抓窗口位图 → 逐行暗像素计数（文字代理；与遮挡无关）。"""
    w, h = win32gui.GetClientRect(hwnd)[2:]
    hdc = win32gui.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, max(w, 1), max(h, 1))
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 1)
    prof = []
    for y in range(2, h - 2, 2):
        rd = 0
        for x in range(2, w - 2, 2):
            c = gdi32.GetPixel(mem, x, y) & 0xFFFFFF
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) // 3 < 140:
                rd += 1
        prof.append(rd)
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    win32gui.ReleaseDC(hwnd, hdc)
    return prof


shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
    json.dump({"mode": "serial", "encoding": "gbk",
               "term_enter_send": True, "term_key_send": False, "term_lf_send": True,
               "term_enable": True}, f)   # §140：终端总开关默认关，终端线探针预置开启
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(70):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.15)
time.sleep(2.5)
rx = child(main, True)
rx = rx[0] if rx else 0
chkbox = child(main, False)
chkbox = chkbox[0] if chkbox else 0
chk("前置：RxView 与「接收数据到文件」控件就位", bool(rx and chkbox),
    "rx=%s chk=%s" % (hex(rx), hex(chkbox) if chkbox else "0"))

if rx and chkbox and main:
    # ---- 相位 0：空屏（尚无任何数据）键入尾巴必须可见（§111 P2-2 回归） ----
    p0 = dark_rows(rx)
    win32gui.PostMessage(rx, WM_CHAR, ord("Z"), 0)
    time.sleep(1.0)
    p1 = dark_rows(rx)
    changed0 = sum(1 for i in range(min(len(p0), len(p1))) if p0[i] != p1[i])
    chk("0 空屏键入 Z：接收视图像素变化（尾巴画在左上角；修复前不可见）", changed0 > 0,
        "changed_rows=%d dark %d->%d" % (changed0, sum(p0), sum(p1)))
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_BACK, 0)              # 清尾（空屏本地退格，不发链路）
    time.sleep(0.6)

    win32gui.PostMessage(main, WM_COMMAND, IDM_TOOL_LOOP, 0)      # 软件回环（提供链路）
    time.sleep(1.5)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)                  # 转存开
    time.sleep(0.8)
    # ---- 相位 A：回车方式整行发送（LF 补全） ----
    for ch_ in "ABC":
        win32gui.PostMessage(rx, WM_CHAR, ord(ch_), 0)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)                  # 转存关（释放句柄）
    time.sleep(0.8)
    logA = newest_log()
    dataA = read_log(logA) if logA else b""
    chk("A 回车方式：日志含 'ABC\\r\\n'（整行+CRLF 经回环回来）", b"ABC\r\n" in dataA,
        "%s %d bytes" % (os.path.basename(logA), len(dataA)))
    chk("A 负对照：日志不含 X/Y/Q（尚未键入）",
        b"X" not in dataA and b"Y" not in dataA and b"Q" not in dataA)

    # ---- 相位 B：按键方式逐键即发（无回车） ----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(main, WM_COMMAND, IDM_TERM_KEY, 0)       # 切「按键立即发送」
    time.sleep(0.6)
    for ch_ in "XY":
        win32gui.PostMessage(rx, WM_CHAR, ord(ch_), 0)
        time.sleep(0.3)
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logB = newest_log()
    dataB = read_log(logB) if logB else b""
    chk("B 按键方式：未回车即见 X/Y 入日志（逐键即时发送）",
        (b"X" in dataB or b"Y" in dataB), "%s %d bytes" % (os.path.basename(logB), len(dataB)))

    # ---- 相位 C：回车方式退格粒度（Q 被删，不应外发） ----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(main, WM_COMMAND, IDM_TERM_ENTER, 0)     # 回「回车方式」
    time.sleep(0.6)
    win32gui.PostMessage(rx, WM_CHAR, ord("Q"), 0)
    time.sleep(0.3)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_BACK, 0)              # 本地退格
    time.sleep(0.3)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)            # 空行+CRLF
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logC = newest_log()
    dataC = read_log(logC) if logC else b""
    chk("C 退格粒度：日志新增不含 'Q'（本地行已删除）", b"Q" not in dataC,
        "%s %d bytes" % (os.path.basename(logC), len(dataC)))
    chk("C 回车仍发送（新增含 CRLF）", b"\r\n" in dataC or dataC.startswith(b"\r"),
        "bytes=%d" % len(dataC))

    # ---- 相位 D：连按回车不重复发上一行（§111 P2-1 回归） ----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(rx, WM_CHAR, ord("M"), 0)
    time.sleep(0.3)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)            # 发 "M"+CRLF
    time.sleep(0.6)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)            # 空行回车：只应发 CRLF
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logD = newest_log()
    dataD = read_log(logD) if logD else b""
    pays = log_payloads(dataD)
    chk("D 连按回车：'M' 只出现 2 次（TX+RX 各一）且末尾有空行负载（第二回车只发空行）",
        pays.count("M") == 2 and pays.count("") >= 1,
        "%s payloads=%r" % (os.path.basename(logD), pays))
    # （不做 "M\r\nM" 负对照：日志含 TX 回显+RX 两方向时，修复后本来就出现 "M\r\nM\r\n"；
    #   重复发送的字节级判据由 codex review_r16_term_tcp 的 A2（raw socket）承担）

    win32gui.PostMessage(main, WM_CLOSE, 0, 0)
for _ in range(20):
    if proc.poll() is not None:
        break
    time.sleep(0.15)
if proc.poll() is None:
    proc.kill()
time.sleep(0.5)
shutil.rmtree(BIN, ignore_errors=True)

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if any(not ok for _, ok in res):
    sys.exit(1)
