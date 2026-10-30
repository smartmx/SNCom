# -*- coding: utf-8 -*-
# verify_r75.py - docs/00 §75（用户两项）：
#  1) 查找迁入扩展窗「查找」页：点「查找」→ 扩展窗可见 + 页签=3 + 查找控件为扩展窗子级；
#     输入词 → 计数标签更新；「下一个」跳转；× 关闭扩展；主窗顶部无残留（GetDlgItem=0）
#     且接收区顶到 kRxTop（无顶部条预留）
#  2) Ctrl+Enter 发送不再插入回车：GetKeyState(VK_CONTROL) 跨进程不可伪造，
#     代码级修复（WM_CHAR '\r' 且 Ctrl 按下 → 吞掉），真键盘留人工复核
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api
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

proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\develop\dist")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# 主窗顶部无查找控件残留（§75 已迁出）；ctypes 版 GetDlgItem 找不到返回 0 不抛异常
leaks = [i for i in (3074, 3075, 3076, 3077, 3078, 3079, 3080) if user32.GetDlgItem(main, i)]
chk("§75-1 主窗无查找控件（已迁出）", not leaks, "leaks=%s" % leaks)
rx = win32gui.GetDlgItem(main, 3059)
rl, rt, rr, rb = win32gui.GetWindowRect(rx)
ct = win32gui.ClientToScreen(main, (0, 0))[1]          # 客户区顶（54px 含标题栏+菜单）
chk("§75-1 接收区顶到客户区顶（无顶部条）", rt - ct <= 12, "rx_top-client_top=%d" % (rt - ct))

# 喂数据（供查找命中）
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)
time.sleep(3.0)

# 点「查找」→ 扩展窗 + 查找页
win32api.PostMessage(win32gui.GetDlgItem(main, 3001), 0x00F5, 0, 0)
time.sleep(0.8)
ext = find_toplevel("SNComExt", proc.pid)
chk("§75-1 扩展窗已弹出", bool(ext) and win32gui.IsWindowVisible(ext), "ext=%s" % ext)
if ext:
    tab = win32gui.GetDlgItem(ext, 1)                  # IDC_EXT_TAB = 1
    cur = user32.SendMessageW(tab, 0x1300 + 11, 0, 0)  # TCM_GETCURSEL（TCM_FIRST+11，非 +5）
    chk("§75-1 页签切到「查找」(index 3)", cur == 3, "cursel=%s" % cur)
    fe = win32gui.GetDlgItem(ext, 3074)
    chk("§75-1 查找编辑框为扩展窗子级且可见", bool(fe) and win32gui.IsWindowVisible(fe), "fe=%s" % fe)
    cnt = win32gui.GetDlgItem(ext, 3077)
    # 输入查找词（WM_SETTEXT 会触发 EN_CHANGE → 防抖 300ms → FindRescan）
    ctypes.windll.user32.SendMessageW(fe, win32con.WM_SETTEXT, 0, ctypes.c_wchar_p("END_MARK"))
    time.sleep(0.8)
    t0 = win32gui.GetWindowText(cnt) if cnt else ""
    chk("§75-1 查找计数已更新（n/N, N>0）", "/" in t0 and t0.split("/")[1].isdigit() and int(t0.split("/")[1]) > 0,
        "count=%r" % t0)
    # 「下一个」跳转 → 计数 n 变化
    nx = win32gui.GetDlgItem(ext, 3079)
    win32api.PostMessage(nx, 0x00F5, 0, 0)             # BM_CLICK
    time.sleep(0.5)
    t1 = win32gui.GetWindowText(cnt) if cnt else ""
    chk("§75-1 「下一个」跳转后计数变化", t1 != t0, "%r -> %r" % (t0, t1))
    # 关闭扩展窗（§89：查找页 × 已删——用主窗「扩展」按钮收起，等效原关闭路径）
    cl = user32.GetDlgItem(main, 3012)
    win32api.PostMessage(cl, 0x00F5, 0, 0)
    time.sleep(0.6)
    chk("§75-1 × 关闭扩展窗", not win32gui.IsWindowVisible(ext), "")

# ---- 2) Ctrl+Enter：吞掉逻辑仅限 Ctrl ----
# 实测（消息注入实验）：多行 EDIT 的回车插入依赖**真实按键状态**——posted 的
# WM_KEYDOWN/WM_CHAR('\r'/'\n') 一律不插入（len 2->2 不变），消息级无法复现真键盘
# 路径；修复=`SendEditProc` 在 WM_CHAR '\r' 且 GetKeyState(VK_CONTROL)<0 时吞掉
# （真实 Ctrl+Enter 时 TranslateMessage 派生的 '\r' 会被编辑框插入——用户报的 bug）。
print("NOTE §75-2: Ctrl+Enter WM_CHAR 吞掉为代码级修复（真实键盘依赖，消息注入不可复现），留人工复核", flush=True)

proc.terminate()
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
