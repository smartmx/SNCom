# -*- coding: utf-8 -*-
# verify_r43_44.py - docs/00 43 (ext window position semantics) + 44 (wheel lock, ext width 480, baud combo)
import os, sys, time, subprocess, ctypes
import win32gui, win32con, win32api, win32process
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
ID_EXPAND, ID_BTN_CLEAR = 3012, 3000
ID_CMB_PORT, ID_CMB_BAUD, ID_CMB_DATABITS, ID_CMB_STOPBITS = 3014, 3019, 3021, 3023
ID_CMB_PARITY, ID_CMB_FLOW, ID_CMB_CHECKSUM, ID_CMB_CHKTAIL = 3025, 3027, 3055, 3053
ID_CMB_LIP, ID_CMB_NIC = 3072, 3069
WM_MOUSEWHEEL = 0x020A
CB_GETCURSEL, CB_GETCOUNT, CB_GETLBTEXTLEN = 0x0147, 0x0146, 0x0149
CB_SETCURSEL = 0x014E
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
def R(h):
    l, t, r, b = win32gui.GetWindowRect(h); return (l, t, r, b)
mr = R(main)
# ---- 1. open ext: flush right, top aligned, width 480, height = main height
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
chk("43.1 ext created", bool(ext))
er = R(ext)
chk("43.1 ext.left == main.right + 1", er[0] == mr[2] + 1, "ext.left=%d main.right=%d" % (er[0], mr[2]))
chk("43.1 ext.top == main.top", er[1] == mr[1], "ext.top=%d main.top=%d" % (er[1], mr[1]))
chk("43.1 ext width == 480", er[2] - er[0] == 480, "w=%d" % (er[2] - er[0]))
chk("43.1 ext height == main height", er[3] - er[1] == mr[3] - mr[1], "h=%d main_h=%d" % (er[3] - er[1], mr[3] - mr[1]))

# ---- 2. main window moves -> ext must NOT follow (docs/00 43 user decision)
win32gui.SetWindowPos(main, None, mr[0] + 120, mr[1] + 60, mr[2] - mr[0], mr[3] - mr[1],
                      win32con.SWP_NOZORDER | win32con.SWP_NOACTIVATE)
time.sleep(0.8)
er2 = R(ext)
chk("43.2 ext does NOT follow main move", er2 == er, "before=%s after=%s" % (er, er2))
mr2 = R(main)

# ---- 3. minimize / restore: ext hidden on minimize, shown again, position unchanged
win32gui.ShowWindow(main, win32con.SW_MINIMIZE)
time.sleep(1.0)
vis_min = bool(win32gui.IsWindowVisible(ext))
win32gui.ShowWindow(main, win32con.SW_RESTORE)
time.sleep(1.2)
vis_res = bool(win32gui.IsWindowVisible(ext))
er3 = R(ext)
chk("43.3 minimize hides ext", vis_min is False, "visible=%s" % vis_min)
chk("43.3 restore shows ext again", vis_res is True, "visible=%s" % vis_res)
chk("43.3 restore keeps position (no re-anchor)", er3 == er2, "before=%s after=%s" % (er2, er3))

# ---- 4. close + reopen re-anchors to the current right edge
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)   # close
time.sleep(0.6)
win32gui.PostMessage(win32gui.GetDlgItem(main, ID_EXPAND), 0x00F5, 0, 0)   # reopen
time.sleep(1.0)
mr3 = R(main); er4 = R(find_toplevel("SNComExt", proc.pid))
chk("43.4 reopen re-anchors to new right edge", er4[0] == mr3[2] + 1,
    "ext.left=%d main.right=%d" % (er4[0], mr3[2]))

# ---- 5. mouse wheel must not change combo selection (docs/00 44.2)
combos = [("port", ID_CMB_PORT), ("baud", ID_CMB_BAUD), ("databits", ID_CMB_DATABITS),
          ("stopbits", ID_CMB_STOPBITS), ("parity", ID_CMB_PARITY), ("flow", ID_CMB_FLOW),
          ("checksum", ID_CMB_CHECKSUM), ("chk_tail", ID_CMB_CHKTAIL), ("local_ip", ID_CMB_LIP),
          ("nic", ID_CMB_NIC)]
bad = []
for name, cid in combos:
    c = win32gui.GetDlgItem(main, cid)
    if not c:
        bad.append(name + "(missing)")
        continue
    before = win32gui.SendMessage(c, CB_GETCURSEL, 0, 0)
    r = R(c)
    lp = win32api.MAKELONG(r[0] + 10, r[1] + 8)
    for _ in range(3):
        win32gui.PostMessage(c, WM_MOUSEWHEEL, win32api.MAKEWORD(0, (-120) & 0xFFFF), lp)
        win32gui.PostMessage(c, WM_MOUSEWHEEL, win32api.MAKEWORD(0, 120), lp)
    ed = win32gui.GetWindow(c, win32con.GW_CHILD)
    if ed:
        for _ in range(3):
            win32gui.PostMessage(ed, WM_MOUSEWHEEL, win32api.MAKEWORD(0, (-120) & 0xFFFF), lp)
    time.sleep(0.25)
    after = win32gui.SendMessage(c, CB_GETCURSEL, 0, 0)
    if before != after:
        bad.append("%s(%s->%s)" % (name, before, after))
chk("44.2 wheel does not change any combo selection", not bad, "combos=%d changed=%s" % (len(combos), bad))

# ---- 6. §55(恢复 §46): baud combo is DROPDOWNLIST + 自定义 slot -> popup dialog
c = win32gui.GetDlgItem(main, ID_CMB_BAUD)
st = win32gui.GetWindowLong(c, win32con.GWL_STYLE)
chk("46.1 baud combo is CBS_DROPDOWNLIST (不可直输)", (st & 0x0003) == 0x0003, "style=0x%08X" % st)
n = win32gui.SendMessage(c, CB_GETCOUNT, 0, 0)
chk("46.2 baud item count == 18 (17 presets + custom slot)", n == 18, "count=%d" % n)
lens = [win32gui.SendMessage(c, CB_GETLBTEXTLEN, i, 0) for i in range(n)]
chk("46.2 custom slot = last item, 3 chars", lens[-1] == 3, "len(last)=%s lens=%s" % (lens[-1], lens))
wmcmd = (1 << 16) | (ID_CMB_BAUD & 0xFFFF)          # MAKEWPARAM(CBN_SELCHANGE)
win32gui.SendMessage(c, CB_SETCURSEL, n - 1, 0)
win32gui.PostMessage(main, win32con.WM_COMMAND, wmcmd, c)
time.sleep(0.8)
dlg = find_toplevel("SNComBaudDlg", proc.pid)
chk("46.4 selecting custom opens input dialog", bool(dlg))
chk("46.4 main window disabled while dialog open", not win32gui.IsWindowEnabled(main))
if dlg:
    chk("46.4 dialog is toolwindow (no taskbar entry)",
        bool(win32gui.GetWindowLong(dlg, win32con.GWL_EXSTYLE) & win32con.WS_EX_TOOLWINDOW),
        "ex=0x%08X" % win32gui.GetWindowLong(dlg, win32con.GWL_EXSTYLE))
    win32gui.SendMessage(win32gui.GetDlgItem(dlg, 1001), win32con.WM_SETTEXT, 0, "250000")
    win32gui.PostMessage(win32gui.GetDlgItem(dlg, 1002), 0x00F5, 0, 0)     # 确定
    time.sleep(1.0)
    chk("46.5 dialog closed after OK", not win32gui.IsWindow(dlg))
    chk("46.5 main re-enabled after OK", bool(win32gui.IsWindowEnabled(main)))
    n2 = win32gui.SendMessage(c, CB_GETCOUNT, 0, 0)
    sel = win32gui.SendMessage(c, win32con.CB_GETCURSEL, 0, 0)
    # GetWindowText 跨进程取 DROPDOWNLIST combo 恒空（§54 坑集）——用 CB_GETCURSEL 断言
    chk("46.5 applied baud selected (inserted numeric item)", sel == n2 - 1, "sel=%s count=%s" % (sel, n2))
    chk("46.5 new value inserted as numeric item (count 19)", n2 == 19, "count=%d" % n2)
    win32gui.SendMessage(c, CB_SETCURSEL, n2 - 2, 0)   # 「自定义」槽（17 预设后=idx n2-2）
    win32gui.PostMessage(main, win32con.WM_COMMAND, wmcmd, c)
    time.sleep(0.8)
    dlg2 = find_toplevel("SNComBaudDlg", proc.pid)
    chk("46.6 custom selected again opens dialog", bool(dlg2))
    if dlg2:
        win32gui.SendMessage(win32gui.GetDlgItem(dlg2, 1001), win32con.WM_SETTEXT, 0, "100")   # out of range
        win32gui.PostMessage(win32gui.GetDlgItem(dlg2, 1002), 0x00F5, 0, 0)                    # OK
        time.sleep(0.8)
        chk("46.6 out-of-range rejected (dialog stays)", bool(win32gui.IsWindow(dlg2)))
        rst = win32gui.GetWindowText(win32gui.GetDlgItem(dlg2, 1001))
        chk("46.6 edit reset to last valid value", rst == "250000", "edit=%s" % rst)
        win32gui.PostMessage(win32gui.GetDlgItem(dlg2, 1003), 0x00F5, 0, 0)                    # 取消
        time.sleep(0.8)
        sel3 = win32gui.SendMessage(c, win32con.CB_GETCURSEL, 0, 0)
        chk("46.7 cancel restores last valid selection",
            sel3 == n2 - 1 and win32gui.IsWindowEnabled(main),
            "sel=%s (want %d) enabled=%s" % (sel3, n2 - 1, bool(win32gui.IsWindowEnabled(main))))

print("\n==== SUMMARY ====")
bad_l = [r for r in res if not r[1]]
print("total=%d pass=%d fail=%d" % (len(res), len(res) - len(bad_l), len(bad_l)))
for nm, ok, d in bad_l:
    print("  FAILED:", nm, "|", d)
proc.terminate()
