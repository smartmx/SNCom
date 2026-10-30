# -*- coding: utf-8 -*-
# verify_r112.py — §112 M6 第二增量：IME 中文输入 + 光标闪烁 + 尾巴折行（免合成输入）
#   A IME 提交字符入链路（回车方式）：WM_IME_CHAR 0x4E2D('中') + VK_RETURN →
#     转存日志（GBK）含 "中" 的字节 D6 D0 与 CRLF（字符走同一终端通道）
#   B IME 提交字符即发（按键方式 2211）：WM_IME_CHAR 0x6587('文') → 日志含 CE C4（无回车）
#   C 源码不变式（新机制的存在性锚点，配合人工真机目检）：
#     C1 rxview.cpp 含 WM_IME_* 四消息与 GCS_RESULTSTR/ImmSetComposition|Candidate|Font
#     C2 rxview.cpp 含 kTimerBlink + GetCaretBlinkTime（闪烁定时器）
#     C3 rxview.cpp 含 GetTextExtentExPointW（按行宽折行布局）
#     C4 死代码已清：rxview.h 无 HasTermInput；mainwindow.cpp 无 IDM_HELP_EXIT
#     C5 门禁细节：build.bat 有 SKIP_AUDIT_VAR 容忍尾空格；审计脚本无 "237" 陈旧文案
#   D 光标闪烁：夺焦点（AttachThreadInput+SetFocus）后连拍 8 帧（首行带逐像素扫描），
#     暗像素计数 max-min >= 6（约一根 12px 竖线）→ 闪烁生效；无法夺焦则 SKIP
#   E 尾巴折行：空屏键入 220 字符 → 位图变化行跨度 >= 14 采样行（≈2 行以上；单行裁剪只 1 行）
#   红对照（--rev 反向期望）：§112 前的 exe 上 D 应恒定不闪、E 应只有单行 → 本探针判红
# 沙箱 exe（probes/_probe/out/r112），不碰 dist 配置；日志读前先取消勾选释放句柄（§83）。
import ctypes
import glob
import json
import os
import shutil
import subprocess
import sys
import time

import win32api
import win32gui
import win32process

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
HERE = os.path.dirname(os.path.abspath(__file__))
SNCOM_ROOT = os.path.dirname(HERE)              # probes/ 的父目录（项目根）
DEV = os.path.join(SNCOM_ROOT, "develop")
SRC = os.environ.get("SNCOM_EXE") or os.path.join(DEV, "dist", "sncom.exe")
RED = "--rev" in sys.argv                       # 红对照模式：D/E 期望「检测到缺陷」
WM_COMMAND, WM_CLOSE, WM_CHAR, WM_KEYDOWN = 0x0111, 0x0010, 0x0102, 0x0100
WM_IME_CHAR = 0x0286
BM_CLICK, VK_RETURN, VK_BACK = 0x00F5, 0x0D, 0x08
IDM_TOOL_LOOP, IDM_TERM_ENTER, IDM_TERM_KEY = 2222, 2210, 2211
BIN = os.path.join(HERE, "_probe", "out", "r112")
res = []


def chk(name, ok, det=""):
    if RED and name[:1] in "DEF":
        ok = not ok
        name = "[RED] " + name
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


def grab(hwnd):
    """PrintWindow 抓位图 → (逐行暗像素 profile, 暗像素总数)。"""
    w, h = win32gui.GetClientRect(hwnd)[2:]
    hdc = win32gui.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, max(w, 1), max(h, 1))
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 1)
    prof = []
    tot = 0
    for y in range(0, h - 2, 2):
        rd = 0
        for x in range(2, w - 2, 2):
            c = gdi32.GetPixel(mem, x, y) & 0xFFFFFF
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) // 3 < 140:
                rd += 1
        prof.append(rd)
        tot += rd
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    win32gui.ReleaseDC(hwnd, hdc)
    return prof, tot


def caret_band_dark(hwnd, band=26):
    """首行带逐像素（x 步长 1，防 1px 宽光标被步长 2 跳过）数暗像素 → 闪烁判据。"""
    w, _ = win32gui.GetClientRect(hwnd)[2:]
    hdc = win32gui.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, max(w, 1), band)
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 1)
    n = 0
    for y in range(0, band):
        for x in range(2, w - 2):
            c = gdi32.GetPixel(mem, x, y) & 0xFFFFFF
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) // 3 < 140:
                n += 1
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    win32gui.ReleaseDC(hwnd, hdc)
    return n


def steal_focus(hwnd):
    """跨进程夺焦点：AttachThreadInput + SetFocus（§108-B r56 手法）。"""
    tid_me = win32api.GetCurrentThreadId()
    tid_tgt = win32process.GetWindowThreadProcessId(hwnd)[0]
    if tid_tgt == tid_me:
        return False
    ok = False
    try:
        if user32.AttachThreadInput(tid_me, tid_tgt, True):
            try:
                win32gui.SetFocus(hwnd)
                ok = win32gui.GetFocus() == hwnd
            finally:
                user32.AttachThreadInput(tid_me, tid_tgt, False)
    except Exception:
        ok = False
    return ok


# ---------- C 组：源码不变式（不依赖 exe） ----------
def src(path):
    p = os.path.join(DEV, path)
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError as e:
        return "<MISSING %s>" % e


rv = src("src/ui/rxview.cpp")
rvh = src("src/ui/rxview.h")
mw = src("src/ui/mainwindow.cpp")
bb = src("build.bat")
au = src("tools/win7_import_audit.py")

need_ime = ["WM_IME_STARTCOMPOSITION", "WM_IME_COMPOSITION", "GCS_RESULTSTR", "WM_IME_CHAR",
            "ImmSetCompositionWindow", "ImmSetCandidateWindow", "ImmSetCompositionFontW"]
chk("C1 IME 消息与 IMM 定位 API 齐备（组合/候选窗跟随光标）",
    all(k in rv for k in need_ime),
    "missing=%s" % [k for k in need_ime if k not in rv])
chk("C2 光标闪烁定时器（kTimerBlink + 系统周期）",
    "kTimerBlink" in rv and "GetCaretBlinkTime" in rv)
chk("C3 尾巴按行宽折行（GetTextExtentExPointW 布局）", "GetTextExtentExPointW" in rv)
chk("C4 死代码已清（HasTermInput / IDM_HELP_EXIT）",
    "HasTermInput" not in rvh and "IDM_HELP_EXIT" not in mw)
chk("C5 门禁细节（逃生口容忍尾空格 / 审计文案无 237 陈旧字面量）",
    "SKIP_AUDIT_VAR" in bb and "tokens=1" in bb
    and "237-entry baseline" not in au and "237 条" not in au)

# ---------- 运行时组 ----------
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
rx = child(main, True) if main else []
rx = rx[0] if rx else 0
chkbox = child(main, False) if main else []
chkbox = chkbox[0] if chkbox else 0
chk("前置：RxView 与「接收数据到文件」控件就位", bool(rx and chkbox),
    "rx=%s chk=%s" % (hex(rx), hex(chkbox) if chkbox else "0"))

if rx and chkbox and main:
    # ---- E 尾巴折行（空屏，无链路） ----
    p0, _t0 = grab(rx)
    for _ in range(420):
        win32gui.PostMessage(rx, WM_CHAR, ord("W"), 0)
        time.sleep(0.01)
    time.sleep(1.0)
    p1, t1 = grab(rx)
    rows = [i for i in range(min(len(p0), len(p1))) if p0[i] != p1[i]]
    span = (max(rows) - min(rows) + 1) if rows else 0
    chk("E 空屏键入 420 字：变化行跨度 >= 14 采样行（折成多行；单行裁剪只有 ~8）",
        span >= 14, "changed_rows=%d span=%d" % (len(rows), span))
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)   # 未连链路：清行不发送
    time.sleep(0.6)

    # ---- D 光标闪烁（空屏 + 尾巴 + 焦点） ----
    win32gui.PostMessage(rx, WM_CHAR, ord("Z"), 0)
    time.sleep(0.5)
    focused = steal_focus(rx)
    if not focused:
        print("    D SKIP: 无法跨进程夺取焦点（GetFocus 未同步）", flush=True)
    else:
        samples = []
        for _ in range(8):
            samples.append(caret_band_dark(rx))
            time.sleep(0.18)
        lo, hi = min(samples), max(samples)
        chk("D 光标闪烁：8 帧首行带暗像素计数波动 >= 6（相位翻转）",
            (hi - lo) >= 6, "samples=%s max-min=%d" % (samples, hi - lo))
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_BACK, 0)     # 清尾
    time.sleep(0.4)

    # ---- A IME 提交字符（回车方式，经回环 + 转存日志做字节判据） ----
    win32gui.PostMessage(main, WM_COMMAND, IDM_TOOL_LOOP, 0)
    time.sleep(1.5)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(rx, WM_IME_CHAR, 0x4E2D, 0)     # '中'
    time.sleep(0.4)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logA = newest_log()
    dataA = read_log(logA) if logA else b""
    chk("A IME 提交字符走终端通道：日志（GBK）含 '中'=D6 D0 + CRLF",
        b"\xd6\xd0\r\n" in dataA, "%s %d bytes" % (os.path.basename(logA), len(dataA)))

    # ---- B 按键方式：IME 提交字符即发 ----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(main, WM_COMMAND, IDM_TERM_KEY, 0)
    time.sleep(0.6)
    win32gui.PostMessage(rx, WM_IME_CHAR, 0x6587, 0)     # '文'
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logB = newest_log()
    dataB = read_log(logB) if logB else b""
    chk("B 按键方式：IME 提交字符未回车即入日志（含 '文'=CE C4）",
        b"\xce\xc4" in dataB, "%s %d bytes" % (os.path.basename(logB), len(dataB)))

    # ---- F1 回车方式 ESC = 清空本地行（§115 P3-4 口径）----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(main, WM_COMMAND, IDM_TERM_ENTER, 0)     # 回「回车方式」
    time.sleep(0.6)
    for ch_ in "JKL":
        win32gui.PostMessage(rx, WM_CHAR, ord(ch_), 0)
        time.sleep(0.2)
    win32gui.PostMessage(rx, WM_KEYDOWN, 0x1B, 0)                 # VK_ESCAPE：清行
    time.sleep(0.4)
    win32gui.PostMessage(rx, WM_KEYDOWN, VK_RETURN, 0)
    time.sleep(1.8)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logF1 = newest_log()
    dataF1 = read_log(logF1) if logF1 else b""
    chk("F1 回车方式 ESC 清空本地行：回车后日志不含 JKL、仅空行 CRLF",
        b"JKL" not in dataF1 and b"\r\n" in dataF1,
        "%s %d bytes" % (os.path.basename(logF1), len(dataF1)))

    # ---- F2 按键方式 ESC 发 0x1B；HOME 不上链路（视图滚动键保留）----
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    win32gui.PostMessage(main, WM_COMMAND, IDM_TERM_KEY, 0)       # 按键方式
    time.sleep(0.6)
    win32gui.PostMessage(rx, WM_KEYDOWN, 0x1B, 0)                 # ESC
    time.sleep(0.8)
    n_after_esc = None
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)                  # 关日志读一次
    time.sleep(0.8)
    logF2 = newest_log()
    dataF2 = read_log(logF2) if logF2 else b""
    n_after_esc = dataF2.count(b"\x1b")
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)                  # 再开日志（新相位）
    time.sleep(1.2)
    win32gui.PostMessage(rx, WM_KEYDOWN, 0x24, 0)                 # VK_HOME
    win32gui.PostMessage(rx, WM_KEYDOWN, 0x23, 0)                 # VK_END
    time.sleep(1.2)
    win32gui.SendMessage(chkbox, BM_CLICK, 0, 0)
    time.sleep(0.8)
    logF3 = newest_log()
    dataF3 = read_log(logF3) if logF3 else b""
    chk("F2 按键方式 ESC：日志恰含 1 个 0x1B（发出且仅一个字节）", n_after_esc == 1,
        "count=%s %s %d bytes" % (n_after_esc, os.path.basename(logF2), len(dataF2)))
    chk("G F2c 控制：HOME/END 不上链路（保留视图滚动；两版构建同真，不参与反向期望）",
        dataF3.count(b"\x1b") == 0,
        "%s %d bytes" % (os.path.basename(logF3), len(dataF3)))

    win32gui.PostMessage(main, WM_CLOSE, 0, 0)
for _ in range(20):
    if proc.poll() is not None:
        break
    time.sleep(0.15)
if proc.poll() is None:
    proc.kill()
time.sleep(0.5)
shutil.rmtree(BIN, ignore_errors=True)

tot = len(res)
pa = sum(1 for _, ok in res if ok)
print("==== SUMMARY ==== total=%d pass=%d fail=%d%s" % (tot, pa, tot - pa,
                                                       "  (RED MODE: D/E/F inverted)" if RED else ""), flush=True)
for n, ok in res:
    if not ok:
        print("FAILED: " + n)
sys.exit(1 if pa != tot else 0)
