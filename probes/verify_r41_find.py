# -*- coding: utf-8 -*-
# verify_r41_find.py - find-bar / toolbar row right edges at minimum window width (docs 40 follow-up)
# ⚠️ §116 起本探针**过时**：查找条自 §89 移入扩展窗（extWnd_），`IDC_FIND_EDIT`（3074）不再是
#    主窗子控件、`IDC_FIND_CLOSE`（3080）已删——原断言目标在主窗下不存在，改为显式 SKIP 退出
#    （不再以 traceback 污染批跑日志）。查找页布局现由 verify_r89 / r41 / r45 族覆盖。
import os, time, subprocess, ctypes
import win32gui, win32con, win32process
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
ID_FIND, ID_FIND_EDIT, ID_FIND_CLOSE, ID_EXPAND = 3001, 3074, 3080, 3012

def find_main(pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_main(proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
cw = win32gui.GetClientRect(main)[2]
print("client width =", cw, flush=True)
# force the minimum track width (the value WM_GETMINMAXINFO enforces)
for _ in range(3):
    win32gui.SetWindowPos(main, None, 100, 100, 985, 700, win32con.SWP_NOZORDER | win32con.SWP_NOACTIVATE)
    time.sleep(0.4)
cw = win32gui.GetClientRect(main)[2]
print("client width at min =", cw, flush=True)
def dlg(parent, cid):
    # pywin32 的 GetDlgItem 在控件不存在时抛 1421（不是返回 0）——统一兜底
    try:
        return win32gui.GetDlgItem(parent, cid)
    except Exception:
        return 0


win32gui.PostMessage(dlg(main, ID_FIND), 0x00F5, 0, 0)   # BM_CLICK -> open find bar
time.sleep(0.8)
if not dlg(main, ID_FIND_EDIT):
    # §89 后查找条位于扩展窗（extWnd_）且 × 已删——本探针过时，显式 SKIP 退出
    print("SKIP verify_r41_find：查找条控件（3074）不在主窗（§89 起位于扩展窗，3080 已删）"
          "——本探针过时，查找页布局由 verify_r89 / r41 / r45 族覆盖", flush=True)
    proc.terminate()
    raise SystemExit(0)
def right_in_client(h):
    l, t, r, b = win32gui.GetWindowRect(h)
    return win32gui.ScreenToClient(main, (r, b))[0], win32gui.ScreenToClient(main, (l, t))[1]
for cid, name in ((ID_FIND_EDIT, "findEdit"), (ID_FIND_CLOSE, "findClose"), (ID_EXPAND, "btnExpand")):
    h = dlg(main, cid)
    if not h:
        print("%-10s (控件不存在，跳过)" % name, flush=True)
        continue
    xr, yt = right_in_client(h)
    print("%-10s right=%4d top=%3d  inside=%s" % (name, xr, yt, xr <= cw), flush=True)
# widest visible child in the find row band (~ y in [40,70])
band = []
def cb(h, l):
    if not win32gui.IsWindowVisible(h):
        return True
    xr, yt = right_in_client(h)
    if 40 <= yt <= 70:
        band.append((win32gui.GetDlgCtrlID(h), win32gui.GetClassName(h), xr))
    return True
win32gui.EnumChildWindows(main, cb, None)
band.sort(key=lambda v: -v[2])
print("find-row widest children:", band[:4], flush=True)
print("RESULT find bar clipped:", any(v[2] > cw for v in band), flush=True)
proc.terminate()
