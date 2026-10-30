# -*- coding: utf-8 -*-
# verify_r70.py - docs/00 §70（用户三项）：
#  1) 菜单全部无快捷键标示：遍历菜单栏所有项（GetMenuString 跨进程安全），断言无 '&'
#  2) 定时发送中点击「关闭串口」→「停止发送」按钮复位为「发  送」（占用 COM 口真开串口）
#  3) 图标更换：截图目检（标题栏/任务栏）
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

# ---- 探测真实 COM 口（立即关闭——本探针要真开串口，不能占用）----
GENERIC_READWRITE = 0xC0000000
held, hold_name = None, None
for name in ("COM1", "COM3", "COM9", "COM10"):
    h = ctypes.windll.kernel32.CreateFileW("\\\\.\\%s" % name, GENERIC_READWRITE, 0,
                                           None, 3, 0, None)
    if h != -1 and h != 0xFFFFFFFFFFFFFFFF:
        ctypes.windll.kernel32.CloseHandle(h)
        held, hold_name = h, name
        break

proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\develop\dist")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# ---- 1) 菜单无 '&' ----
hmenu = user32.GetMenu(main)
def menu_str(hm, idcmd, bycmd):
    n = user32.GetMenuStringW(hm, idcmd, None, 0, bycmd)
    if n <= 0: return ""
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetMenuStringW(hm, idcmd, buf, n + 1, bycmd)
    return buf.value
bad = []
count = 0
def walk(hm, tag):
    global count
    n = user32.GetMenuItemCount(hm)
    for i in range(n):
        mid = user32.GetMenuItemID(hm, i)
        s = menu_str(hm, i, 0x400)             # MF_BYPOSITION
        if s:
            count += 1
            if "&" in s: bad.append("%s:%s" % (tag, s))
        sub = user32.GetSubMenu(hm, i)
        if sub: walk(sub, tag + "/" + s)
walk(hmenu, "root")
chk("§70-1 菜单项共 %d 条、全部无 '&' 快捷键标示" % count, count >= 25 and not bad, "bad=%s" % bad[:6])

# 全窗截图（标题栏图标/菜单目检）
def capture(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (r - l, b - t), hwnd, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img
os.makedirs(r"F:\projects\sscom二次分析开发\SNCom\probes\out", exist_ok=True)
capture(main).save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r70_main.png")
print("saved r70_main.png", flush=True)

# ---- 2) 定时发送中关闭串口 → 按钮复位（无需占口：直接开真实 COM 口）----
btn_send = win32gui.GetDlgItem(main, 3058)         # IDC_BTN_SEND
btn_open = win32gui.GetDlgItem(main, 3017)         # IDC_BTN_OPEN
chk_timed = win32gui.GetDlgItem(main, 3046)        # IDC_CHK_TIMED
if hold_name is None:
    print("SKIP: 无真实 COM 口，定时发送复位段跳过", flush=True)
else:
    # 选端口 → 打开串口
    # §135 收口修正：原代码 0x0143 实为 CB_ADDSTRING（注释误标 CB_SELECTSTRING）——「选中」
    # 从未生效，探针一直在开**配置端口**（dist 配置=COM3；用户 sscom5131 占用 COM3 时必红：
    # 打开失败→自动重连→按钮「取消」）。改为按名扫描列表项（项文本="COM1 - 通信端口"）+
    # CB_SETCURSEL 选中探针自己验证过空闲的端口，选中后回读确认（失败即 assert 自诊断）。
    cmb = win32gui.GetDlgItem(main, 3014)
    u = ctypes.windll.user32
    u.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint64, ctypes.c_uint64]
    u.SendMessageW.restype = ctypes.c_ssize_t
    sel, items = -1, []
    for i in range(u.SendMessageW(cmb, 0x0146, 0, 0)):          # CB_GETCOUNT
        ln = u.SendMessageW(cmb, 0x0149, i, 0)                  # CB_GETLBTEXTLEN
        b = ctypes.create_unicode_buffer(ln + 1)
        u.SendMessageW(cmb, 0x0148, i, ctypes.cast(b, ctypes.c_void_p).value)   # CB_GETLBTEXT
        items.append(b.value)
        if sel < 0 and (b.value == hold_name or b.value.startswith(hold_name + " - ")):
            sel = i
    assert sel >= 0, "端口 %s 不在下拉列表 %r" % (hold_name, items)
    u.SendMessageW(cmb, 0x014E, sel, 0)                         # CB_SETCURSEL
    assert u.SendMessageW(cmb, 0x0147, 0, 0) == sel, "端口选中未生效"
    print("selected item[%d] = %r" % (sel, items[sel]), flush=True)
    win32api.PostMessage(btn_open, 0x00F5, 0, 0)   # BM_CLICK 打开串口
    time.sleep(1.2)
    chk("§70-2 已连接（按钮=关闭串口）", win32gui.GetWindowText(btn_open) == "关闭串口",
        "text=%r" % win32gui.GetWindowText(btn_open))
    # 勾定时发送 → 点发送 → 按钮=停止发送
    win32api.PostMessage(chk_timed, 0x00F5, 0, 0)  # BM_CLICK 勾选
    time.sleep(0.3)
    win32api.PostMessage(btn_send, 0x00F5, 0, 0)   # BM_CLICK 发送（启动定时）
    time.sleep(0.6)
    chk("§70-2 定时发送中（按钮=停止发送）", win32gui.GetWindowText(btn_send) == "停止发送",
        "text=%r" % win32gui.GetWindowText(btn_send))
    # 关闭串口 → 发送按钮应复位
    win32api.PostMessage(btn_open, 0x00F5, 0, 0)   # BM_CLICK 关闭串口
    time.sleep(0.8)
    chk("§70-2 关闭串口后发送按钮复位「发  送」", win32gui.GetWindowText(btn_send) == "发  送",
        "text=%r" % win32gui.GetWindowText(btn_send))
    chk("§70-2 串口已关（按钮=打开串口）", win32gui.GetWindowText(btn_open) == "打开串口",
        "text=%r" % win32gui.GetWindowText(btn_open))

proc.terminate()
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
