# -*- coding: utf-8 -*-
# verify_r41_extresize.py - ext window is resizable and pages follow WM_SIZE (docs 34 item 1)
import os, time, subprocess
import win32gui, win32con, win32process
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
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
time.sleep(3.0)
win32gui.PostMessage(win32gui.GetDlgItem(main, 3012), 0x00F5, 0, 0)   # expand
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
tab = win32gui.GetDlgItem(ext, 1)
lv  = win32gui.GetDlgItem(ext, 20)
def m(h):
    r = win32gui.GetWindowRect(h); return (r[2]-r[0], r[3]-r[1])
e0, t0, l0 = m(ext), m(tab), m(lv)
print("before resize: ext=%s tab=%s list=%s" % (e0, t0, l0), flush=True)
win32gui.SetWindowPos(ext, None, 900, 120, 620, 520, win32con.SWP_NOZORDER | win32con.SWP_NOACTIVATE)
time.sleep(0.8)
e1, t1, l1 = m(ext), m(tab), m(lv)
print("after  resize: ext=%s tab=%s list=%s" % (e1, t1, l1), flush=True)
# §116：判据改增量一致（旧版写死 tab 高 >= 500，是 480x720 时代的阈值——620x520 下 tab=481
# 会假报 False）。现判据：tab/list 的增量必须与 ext 增量一致（页确实跟随 WM_SIZE）。
follow = (t1[0] - t0[0] == e1[0] - e0[0] and t1[1] - t0[1] == e1[1] - e0[1]
          and l1[0] - l0[0] == e1[0] - e0[0] and l1[1] - l0[1] == e1[1] - e0[1])
print("RESULT page follows resize:", follow,
      "(ext Δ=%s tab Δ=%s list Δ=%s)" % ((e1[0]-e0[0], e1[1]-e0[1]), (t1[0]-t0[0], t1[1]-t0[1]),
                                         (l1[0]-l0[0], l1[1]-l0[1])), flush=True)
# minimum size guard (WM_GETMINMAXINFO 320x240)
win32gui.SetWindowPos(ext, None, 900, 120, 100, 100, win32con.SWP_NOZORDER | win32con.SWP_NOACTIVATE)
time.sleep(0.6)
print("after forced 100x100: ext=%s (WM_GETMINMAXINFO only applies to user drags)" % (m(ext),), flush=True)
proc.terminate()
