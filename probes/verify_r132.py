# -*- coding: utf-8 -*-
# §132 E2E 探针：界面态持久化（多条表格 / 定时发送勾选+间隔 / 多条循环勾选 / 协议页 5 项）
#   A 首启恢复：预置 sncom.json → 切协议页 → 断言下拉/勾选/文件路径/超时次数/表格全恢复
#   B 修改+保存：间隔 3000、协议显示取消勾选、增行→3、循环勾选 → 保存参数 → 关窗
#   C 落盘核对（规则 18：关应用后读 json）：改动值 + 原值保留 + 未知键透传
#   D 二次启动复验：修改后的值再次恢复（真闭环）
import json, os, shutil, subprocess, sys, time
import ctypes
import win32gui, win32con, win32api, win32process

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("SNCOM_EXE") or os.path.abspath(
    os.path.join(HERE, os.pardir, "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "persist132")
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + detail if detail else ""), flush=True)

# 控件 ID（mainwindow.cpp 枚举，稳定值）：协议页 14/15/26/27/28/29/30；
# 主窗定时发送 3046/3047；扩展页签 1、msList 20、msAdd 22、msLoop 25；保存参数 3011、扩展 3012
ID_PROTO_MODE, ID_PROTO_FILE, ID_XCHECK, ID_XBLK = 14, 15, 26, 27
ID_PROTO_SHOW, ID_PROTO_TIMEOUT, ID_PROTO_RETRIES = 28, 29, 30
ID_CHK_TIMED, ID_EDIT_INTERVAL = 3046, 3047
ID_MSLIST, ID_MSADD, ID_MSLOOP = 20, 22, 25
ID_SAVEPARAMS, ID_EXPAND = 3011, 3012
CB_GETCURSEL = 0x0147
BM_GETCHECK, BM_CLICK = 0x00F0, 0x00F5
LVM_GETITEMCOUNT = 0x1004
TCM_SETCURSEL = 0x130C

shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
fw = os.path.join(BIN, "fw132.bin")
with open(fw, "wb") as f:
    f.write(bytes(range(256)) * 5)

CFG = {
    "mode": "serial", "encoding": "gbk",
    "proto_kind": 1, "proto_xcheck": 2, "proto_xblk": 1, "proto_show": True,
    "proto_file": fw, "proto_timeout_ms": 1500, "proto_retries": 7,
    "send_interval_ms": 2500, "send_timed": True, "multisend_loop": False,
    "multisend_rows": [
        {"note": "n1", "text": "A1", "hex": False, "order": 1, "interval": 300},
        {"note": "n2", "text": "41 42", "hex": True, "order": 2, "interval": 400},
    ],
    "review_keep": {"x": 1},
}
def write_cfg(d):
    with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=1)
def read_cfg():
    with open(os.path.join(BIN, "sncom.json"), "r", encoding="utf-8") as f:
        return json.load(f)
write_cfg(CFG)

def launch():
    p = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
    main = 0
    def cb(h, l):
        nonlocal main
        if win32gui.GetClassName(h) == "SNComMain" and \
           win32process.GetWindowThreadProcessId(h)[1] == p.pid:
            main = h
        return True
    for _ in range(60):                            # 规则 17：条件化等待
        win32gui.EnumWindows(cb, None)
        if main: break
        time.sleep(0.25)
    assert main, "主窗未出现"
    time.sleep(1.5)
    return p, main

def kids_of(h):
    out = []
    win32gui.EnumChildWindows(h, lambda c, l: (out.append(c), True)[1], None)
    return out
def child_by_id(parent, cid):
    for c in kids_of(parent):
        if win32gui.GetDlgCtrlID(c) == cid:
            return c
    return 0

def wtext(h):
    # 跨进程 GetWindowText 对 EDIT/COMBO 假空（规则 20 系）：WM_GETTEXT 由 USER32 代管缓冲
    n = win32gui.SendMessage(h, win32con.WM_GETTEXTLENGTH, 0, 0)
    buf = ctypes.create_unicode_buffer(n + 1)
    win32gui.SendMessage(h, win32con.WM_GETTEXT, n + 1, buf)
    return buf.value

def open_ext(main, pid):
    win32gui.SendMessage(win32gui.GetDlgItem(main, ID_EXPAND), BM_CLICK, 0, 0)
    ext = 0
    for _ in range(25):
        found = []
        def cb(h, l):
            if win32gui.GetClassName(h) == "SNComExt" and \
               win32process.GetWindowThreadProcessId(h)[1] == pid:
                found.append(h)
            return True
        win32gui.EnumWindows(cb, None)
        if found:
            ext = found[0]
            break
        time.sleep(0.2)
    assert ext, "扩展窗未出现"
    tab = child_by_id(ext, 1)                      # IDC_EXT_TAB
    win32gui.SendMessage(tab, TCM_SETCURSEL, 4, 0)  # 规则 20：页签切换（跨进程点击不生效）
    win32gui.SendMessage(ext, win32con.WM_APP + 0x111, 4, 0)
    time.sleep(0.5)
    return ext

def close_app(p, main):
    win32api.PostMessage(main, win32con.WM_CLOSE, 0, 0)
    try:
        p.wait(timeout=8)
    except Exception:
        p.kill(); p.wait()
    time.sleep(0.4)

# ---------------- A 首启恢复 ----------------
p, main = launch()
ext = open_ext(main, p.pid)
g = lambda cid: child_by_id(ext, cid)
chk("A1 协议下拉恢复 YMODEM", win32gui.SendMessage(g(ID_PROTO_MODE), CB_GETCURSEL, 0, 0) == 1)
chk("A2 校验方式恢复 校验和", win32gui.SendMessage(g(ID_XCHECK), CB_GETCURSEL, 0, 0) == 2)
chk("A3 包大小恢复 1024", win32gui.SendMessage(g(ID_XBLK), CB_GETCURSEL, 0, 0) == 1)
chk("A4 显示勾选恢复", win32gui.SendMessage(g(ID_PROTO_SHOW), BM_GETCHECK, 0, 0) == 1)
fpath_txt = wtext(g(ID_PROTO_FILE))
chk("A5 文件路径恢复", os.path.normcase(fpath_txt) == os.path.normcase(fw), fpath_txt)
chk("A6 超时/次数恢复", wtext(g(ID_PROTO_TIMEOUT)) == "1500" and
    wtext(g(ID_PROTO_RETRIES)) == "7")
chk("A7 定时间隔恢复 2500",
    wtext(win32gui.GetDlgItem(main, ID_EDIT_INTERVAL)) == "2500")
chk("A8 定时勾选恢复", win32gui.SendMessage(win32gui.GetDlgItem(main, ID_CHK_TIMED), BM_GETCHECK, 0, 0) == 1)
n = win32gui.SendMessage(g(ID_MSLIST), LVM_GETITEMCOUNT, 0, 0)
chk("A9 多条表恢复 2 行", n == 2, "rows=%d" % n)
chk("A10 循环勾选未勾", win32gui.SendMessage(g(ID_MSLOOP), BM_GETCHECK, 0, 0) == 0)

# ---------------- B 修改 + 保存 ----------------
win32gui.SendMessage(win32gui.GetDlgItem(main, ID_EDIT_INTERVAL), win32con.WM_SETTEXT, 0, "3000")
time.sleep(0.3)
win32gui.SendMessage(g(ID_PROTO_SHOW), BM_CLICK, 0, 0)     # 显示勾选 → 取消
time.sleep(0.2)
win32gui.SendMessage(g(ID_MSADD), BM_CLICK, 0, 0)          # 增行 → 3
time.sleep(0.3)
win32gui.SendMessage(g(ID_MSLOOP), BM_CLICK, 0, 0)         # 循环勾选（未连接：静默等待）
time.sleep(0.5)
win32gui.SendMessage(win32gui.GetDlgItem(main, ID_SAVEPARAMS), BM_CLICK, 0, 0)   # 保存参数
time.sleep(0.6)
close_app(p, main)

# ---------------- C 落盘核对 ----------------
c = read_cfg()
chk("C1 间隔落盘 3000", c.get("send_interval_ms") == 3000, str(c.get("send_interval_ms")))
chk("C2 定时勾选落盘", c.get("send_timed") is True)
chk("C3 循环勾选落盘", c.get("multisend_loop") is True)
rows = c.get("multisend_rows") or []
chk("C4 表格 3 行落盘", len(rows) == 3, "len=%d" % len(rows))
r0 = rows[0] if rows else {}
chk("C5 首行字段保真", r0.get("note") == "n1" and r0.get("text") == "A1" and
    r0.get("order") == 1 and r0.get("interval") == 300 and r0.get("hex") is False)
chk("C6 二行 HEX 保真", len(rows) > 1 and rows[1].get("hex") is True and rows[1].get("text") == "41 42")
chk("C7 协议 5 项落盘", c.get("proto_kind") == 1 and c.get("proto_xcheck") == 2 and
    c.get("proto_xblk") == 1 and c.get("proto_show") is False and
    os.path.normcase(c.get("proto_file") or "") == os.path.normcase(fw))
chk("C8 超时/次数保留", c.get("proto_timeout_ms") == 1500 and c.get("proto_retries") == 7)
chk("C9 未知键透传保留", c.get("review_keep") == {"x": 1})

# ---------------- D 二次启动复验 ----------------
p, main = launch()
ext = open_ext(main, p.pid)
g = lambda cid: child_by_id(ext, cid)
chk("D1 间隔=3000", wtext(win32gui.GetDlgItem(main, ID_EDIT_INTERVAL)) == "3000")
chk("D2 表格=3 行", win32gui.SendMessage(g(ID_MSLIST), LVM_GETITEMCOUNT, 0, 0) == 3)
chk("D3 循环勾选=已勾", win32gui.SendMessage(g(ID_MSLOOP), BM_GETCHECK, 0, 0) == 1)
chk("D4 显示勾选=未勾", win32gui.SendMessage(g(ID_PROTO_SHOW), BM_GETCHECK, 0, 0) == 0)
close_app(p, main)

# ---------------- E 最小化态关闭：尺寸保真（§132 修复；曾落 160x28 图标矩形） ----------------
p, main = launch()
win32gui.ShowWindow(main, win32con.SW_MINIMIZE)
time.sleep(1.0)
close_app(p, main)
c = read_cfg()
chk("E1 最小化关闭不落图标矩形", (c.get("win_w") or 0) >= 900 and (c.get("win_h") or 0) >= 600,
    "win=%sx%s" % (c.get("win_w"), c.get("win_h")))

bad = [n for n, ok in res if not ok]
print("TOTAL %d/%d" % (len(res) - len(bad), len(res)), flush=True)
sys.exit(1 if bad else 0)
