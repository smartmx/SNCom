# -*- coding: utf-8 -*-
# verify_r53.py - docs/00 §53: window may shrink until just past the 发 送 button's
# right edge (UI-3 spec, §32 re-ratified); the toolbar must NOT raise the minimum
# (narrower than the toolbar row -> right-side buttons clip, 参照实现 semantics).
import os, time, subprocess, ctypes, ctypes.wintypes
import win32gui, win32con, win32process, win32api
user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
POINT = ctypes.wintypes.POINT

class MINMAXINFO(ctypes.Structure):
    _fields_ = [("ptReserved", POINT), ("ptMaxSize", POINT),
                ("ptMaxPosition", POINT), ("ptMinTrackSize", POINT),
                ("ptMaxTrackSize", POINT)]

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

# 发 送 button (IDC_BTN_SEND=3058) right edge in client coords
sb = win32gui.GetDlgItem(main, 3058)
assert sb
l, t, r, b = win32gui.GetWindowRect(sb)
pt = win32gui.ScreenToClient(main, (r, t))
btn_right = pt[0]

# WM_GETMINMAXINFO readback (system marshals the struct cross-process)
mmi = MINMAXINFO()
user32.SendMessageW(main, 0x0024, 0, ctypes.byref(mmi))
frame = (user32.GetSystemMetrics(32) * 2 + user32.GetSystemMetrics(92))  # CXSIZEFRAME*2+CXPADDEDBORDER
min_track = mmi.ptMinTrackSize.x
min_client = min_track - frame
over = min_client - btn_right
print("sendbtn right(client)=%d  minTrack=%d  frame=%d  minClient=%d  over-margin=%d px"
      % (btn_right, min_track, frame, min_client, over), flush=True)
ok = (0 <= over <= 12)
print("RESULT %s" % ("PASS" if ok else "FAIL"), flush=True)

# 行为目检：压到最小宽截图（右侧工具行按钮应被裁掉，不报错）
wl, wt, wr, wb = win32gui.GetWindowRect(main)
win32gui.SetWindowPos(main, 0, 0, 0, min_track, wb - wt, win32con.SWP_NOMOVE | win32con.SWP_NOZORDER)
time.sleep(0.8)
print("narrow window alive, rect=%s" % (win32gui.GetWindowRect(main),), flush=True)
proc.terminate()
