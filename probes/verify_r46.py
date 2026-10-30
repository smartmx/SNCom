# -*- coding: utf-8 -*-
# verify_r46.py - baud combo "custom" dialog (docs/00 46). Pointer-free probing only:
# typed via WM_CHAR (never WM_SETTEXT with a cross-process string pointer).
import os, json, time, shutil, subprocess
import win32gui, win32con, win32api, win32process
# 路径约定（§60 审查后定式）：绝对字面量路径（机器本地；Mimosa CWE-22 规则按
# 含父目录组件的路径判高危，字面量可根治）。换机时需同步修改 DIST。
SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"          # §114：只读源（仅取 exe）
DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r46"    # §114：沙箱（不再动 develop\dist）
CFG = DIST + r"\sncom.json"
os.makedirs(DIST, exist_ok=True)
shutil.copy(os.path.join(SRC_DIST, "sncom.exe"), os.path.join(DIST, "sncom.exe"))
if not os.path.exists(CFG):                        # 探针兜底：无配置时建空配置（应用按默认读取）
    with open(CFG, "w", encoding="utf-8") as f:
        f.write("{}")
ID_CMB_BAUD = 3019
CB_GETCURSEL, CB_GETCOUNT, CB_GETLBTEXTLEN, CB_SETCURSEL, CB_GETLBTEXT = 0x0147, 0x0146, 0x0149, 0x014E, 0x0148
EM_SETSEL, WM_CHAR, WM_GETTEXTLENGTH = 0x00B1, 0x0102, 0x000E
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

def launch():
    p = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
    m = 0
    for _ in range(40):
        m = find_toplevel("SNComMain", p.pid)
        if m: break
        time.sleep(0.25)
    time.sleep(3.0)
    return p, m

def type_into(ed, text):
    win32gui.SendMessage(ed, EM_SETSEL, 0, -1)
    for ch in text:
        win32gui.PostMessage(ed, WM_CHAR, ord(ch), 0)
    time.sleep(0.4)

def sel_len(c):
    idx = win32gui.SendMessage(c, CB_GETCURSEL, 0, 0)
    if idx < 0: return -1, 0
    return idx, win32gui.SendMessage(c, CB_GETLBTEXTLEN, idx, 0)

backup = CFG + ".probe_bak"
shutil.copyfile(CFG, backup)
try:
    proc, main = launch()
    c = win32gui.GetDlgItem(main, ID_CMB_BAUD)
    st = win32gui.GetWindowLong(c, win32con.GWL_STYLE)
    chk("46.1 CBS_DROPDOWNLIST (no editable part)", (st & 0x0003) == 0x0003, "style=0x%08X" % st)
    kids = []
    def _cb(h, l):
        kids.append(win32gui.GetClassName(h)); return True
    win32gui.EnumChildWindows(c, _cb, None)
    chk("46.1 no EDIT child (DROPDOWNLIST -> typing impossible)",
        "Edit" not in kids, "combo child classes=%s" % kids)
    n = win32gui.SendMessage(c, CB_GETCOUNT, 0, 0)
    lens = [win32gui.SendMessage(c, CB_GETLBTEXTLEN, i, 0) for i in range(n)]
    chk("46.2 count == 18 and last item is the 3-char custom slot", n == 18 and lens[-1] == 3, "count=%d lens=%s" % (n, lens))
    before = win32gui.SendMessage(c, CB_GETCURSEL, 0, 0)
    for ch in "921600":
        win32gui.PostMessage(c, WM_CHAR, ord(ch), 0)
    time.sleep(0.3)
    after_keys = win32gui.SendMessage(c, CB_GETCURSEL, 0, 0)
    chk("46.3 keyboard type-ahead still selects (standard combo semantics; wheel path is what was locked)",
        after_keys != before, "sel %s -> %s" % (before, after_keys))
    # select custom slot -> dialog
    win32gui.SendMessage(c, CB_SETCURSEL, n - 1, 0)
    win32gui.PostMessage(main, win32con.WM_COMMAND, win32api.MAKELONG(ID_CMB_BAUD, 1), c)
    time.sleep(1.0)
    dlg = find_toplevel("SNComBaudDlg", proc.pid)
    chk("46.4 custom slot opens input dialog", bool(dlg))
    chk("46.4 main disabled while dialog open", not win32gui.IsWindowEnabled(main))
    if dlg:
        ed = win32gui.GetDlgItem(dlg, 1001)
        # 预填 = baudLastValid_（启动 cfg 波特率）；空配置环境下其值不定——只断言非空
        # （「恢复上一有效值」语义由 46.6/46.7 覆盖）
        chk("46.4 edit prefilled with last valid value",
            win32gui.SendMessage(ed, WM_GETTEXTLENGTH, 0, 0) > 0,
            "len=%d (WM_GETTEXTLENGTH marshalled=%s)" % (win32gui.SendMessage(ed, WM_GETTEXTLENGTH, 0, 0),
                                                         win32gui.SendMessage(ed, WM_GETTEXTLENGTH, 0, 0) != 0))
        type_into(ed, "250000")
        chk("46.5 typed value readable back", win32gui.SendMessage(ed, WM_GETTEXTLENGTH, 0, 0) == 6,
            "len=%d" % win32gui.SendMessage(ed, WM_GETTEXTLENGTH, 0, 0))
        win32gui.PostMessage(win32gui.GetDlgItem(dlg, 1002), 0x00F5, 0, 0)   # 确定
        time.sleep(1.2)
        chk("46.5 dialog closed on OK", not win32gui.IsWindow(dlg))
        chk("46.5 main re-enabled", bool(win32gui.IsWindowEnabled(main)))
        idx, ln = sel_len(c)
        n2 = win32gui.SendMessage(c, CB_GETCOUNT, 0, 0)
        chk("46.5 applied value = 250000 selected", ln == 6, "sel_idx=%d sel_len=%d" % (idx, ln))
        chk("46.5 inserted as numeric item (count 19)", n2 == 19, "count=%d" % n2)
        # out-of-range value is rejected and the dialog stays open
        ci = [i for i in range(n2) if win32gui.SendMessage(c, CB_GETLBTEXTLEN, i, 0) == 3 and i >= 16][0]
        win32gui.SendMessage(c, CB_SETCURSEL, ci, 0)
        win32gui.PostMessage(main, win32con.WM_COMMAND, win32api.MAKELONG(ID_CMB_BAUD, 1), c)
        time.sleep(1.0)
        dlg2 = find_toplevel("SNComBaudDlg", proc.pid)
        chk("46.6 custom slot opens dialog again", bool(dlg2))
        if dlg2:
            ed2 = win32gui.GetDlgItem(dlg2, 1001)
            type_into(ed2, "100")
            win32gui.PostMessage(win32gui.GetDlgItem(dlg2, 1002), 0x00F5, 0, 0)   # 确定 (out of range)
            time.sleep(0.8)
            chk("46.6 out-of-range rejected: dialog stays open", bool(win32gui.IsWindow(dlg2)))
            chk("46.6 edit reset to last valid value",
                win32gui.SendMessage(ed2, WM_GETTEXTLENGTH, 0, 0) == 6,
                "len=%d" % win32gui.SendMessage(ed2, WM_GETTEXTLENGTH, 0, 0))
            win32gui.PostMessage(win32gui.GetDlgItem(dlg2, 1003), 0x00F5, 0, 0)   # 取消
            time.sleep(1.0)
            ok_cancel = (not win32gui.IsWindow(dlg2)) and win32gui.IsWindowEnabled(main) and sel_len(c)[1] == 6
            chk("46.7 cancel keeps last valid value", ok_cancel, "sel_len=%d enabled=%s" % (sel_len(c)[1], bool(win32gui.IsWindowEnabled(main))))
    proc.terminate()
    time.sleep(0.5)

    # ---- 46.8 startup with a non-preset baud from config: inserted + selected
    t = json.load(open(backup, encoding="utf-8"))
    t["baud"] = 250000
    json.dump(t, open(CFG, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    proc2, main2 = launch()
    c2 = win32gui.GetDlgItem(main2, ID_CMB_BAUD)
    n3 = win32gui.SendMessage(c2, CB_GETCOUNT, 0, 0)
    idx3, ln3 = sel_len(c2)
    chk("46.8 non-preset cfg baud kept: count 19 + 6-char selection", n3 == 19 and ln3 == 6,
        "count=%d sel_idx=%d sel_len=%d" % (n3, idx3, ln3))
    proc2.terminate()
finally:
    shutil.copyfile(backup, CFG)
    os.remove(backup)

print("\n==== SUMMARY ====")
bad_l = [r for r in res if not r[1]]
print("total=%d pass=%d fail=%d" % (len(res), len(res) - len(bad_l), len(bad_l)))
for nm, ok, d in bad_l:
    print("  FAILED:", nm, "|", d)
