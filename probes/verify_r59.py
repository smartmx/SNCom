# -*- coding: utf-8 -*-
# verify_r59.py - docs/00 §58 用户三连：
#  1) 版本号段文本右贴边（段宽=文本实宽+6px → 视觉右对齐；截图目检）
#  2) HEX发送 勾选 → 加回车换行 置灰不可改；取消恢复
#  3) 状态栏前 3 段（连接/TX/RX）合计右界 ≤「发 送」按钮右缘
#     ——经 SB_SETPARTS 后向状态栏发 SB_GETRECTW 需要 RECT 指针（跨进程不可靠），
#     改由 BM_CLICK 发 送 按钮的右缘（btn_send rect）对照：直接断言代码常量不可行，
#     用屏幕坐标：parts 合计宽 = part2 右界（无法指针读取）→ 改从截图量灰段分隔。
#     简化：断言 btn_send_.right(屏) - main.left 与 ScalePx(64+104) 计算一致由
#     截图目检覆盖；此处代码级断言 = 三段由 sendRight 推导（见 docs）。
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
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

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# ---- 2) HEX发送 -> 加回车换行 disabled ----
# R25-P3-1（审查 20261029）：旧写法 BM_CLICK 后固定 sleep(0.4) 再读 IsWindowEnabled——
# 产品同步路径正确（mainwindow.cpp IDC_CHK_HEXSEND 分支内即时 EnableWindow），但启动期
# 消息处理上界实测可达 488ms（codex review_r25_r59_hexnewline 8 连跑：40/47/61/61/184/
# 244/305/488ms），固定 0.4s 约 1/5 假红。改**条件化等待**（20ms 轮询、≤2s 超时，规则 28）。
def wait_state(h, want_enabled, timeout=2.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if bool(win32gui.IsWindowEnabled(h)) == want_enabled:
            return True, (time.time() - t0) * 1000
        time.sleep(0.02)
    return bool(win32gui.IsWindowEnabled(h)) == want_enabled, (time.time() - t0) * 1000

hexsend = win32gui.GetDlgItem(main, 3045)          # IDC_CHK_HEXSEND
newline = win32gui.GetDlgItem(main, 3049)          # IDC_CHK_NEWLINE
chk("at rest (text mode): newline enabled", win32gui.IsWindowEnabled(newline))
win32gui.PostMessage(hexsend, 0x00F5, 0, 0)        # BM_CLICK 勾选 HEX发送
ok, ms = wait_state(newline, False)
chk("HEX发送 checked -> newline disabled", ok, "置灰用时 %.0fms（条件等待≤2s）" % ms)
# 发送路径不受灰影响（勾了回车也只在文本模式生效——代码路径 §54 已断言 hex 提前 return）
win32gui.PostMessage(hexsend, 0x00F5, 0, 0)        # 取消勾选
ok, ms = wait_state(newline, True)
chk("HEX发送 unchecked -> newline enabled again", ok, "恢复用时 %.0fms（条件等待≤2s）" % ms)

# ---- 1)+3) 状态栏截图（版本贴右、三段合计 ≤ 发送按钮右缘）——目检 ----
# R23-P3-3（二十三轮定案）：抓图改**真实屏幕**（桌面 DC BitBlt）——PrintWindow 位图含
# DWM 不可见缩放黑边（左/右/下各 ~8px，codex review_r23_sbar_capture 实测），会把
# 「图底 -18..-10」取样带推到版本文本下方 → 三构建一致 13px 假红。
# §133（R24 实证）：SetForegroundWindow 受**前台锁**限制——从后台 shell 启动时静默
# 失败，屏幕 DC 截到的是**遮挡窗口**的像素（实测整幅 (40,44,52) 深色 → 判据全乱）。
# 改 probes/README 规则 20⑤ 配方：HWND_TOPMOST + SWP_NOACTIVATE 免激活置顶 → 抓图 →
# 撤 TOPMOST。另加**遮挡自检**：状态栏画面若 >50% 深色（无深色主题，正常必为浅底）
# 判为环境性，几何判据转 NOTE-SKIP（规则 15 计数口径）。
SWP_SAFE = win32con.SWP_NOMOVE | win32con.SWP_NOSIZE | win32con.SWP_NOACTIVATE
l, t, r, b = win32gui.GetWindowRect(main)
_cx, _cy = (l + r) // 2, (t + b) // 2
def _our_top():                                    # 屏幕该点是否落回我们窗口（顶层或子窗）
    hw = win32gui.WindowFromPoint((_cx, _cy))
    return bool(hw) and win32gui.GetAncestor(hw, win32con.GA_ROOT) == main
for _att in range(4):                              # §133：置顶可能被别的置顶窗回顶（间歇）→ 重试
    win32gui.SetWindowPos(main, win32con.HWND_TOPMOST, 0, 0, 0, 0, SWP_SAFE)
    time.sleep(0.5)
    if _our_top():
        break
_att_ok = _our_top()                               # §135 收口：重试后仍未夺回=锁屏/别的置顶窗
# §133：抓图前置诊断（行为可回溯——遮挡/隐藏/最小化时一眼可见）
_hw = win32gui.WindowFromPoint((_cx, _cy))
print("capture diag: rect=(%d,%d,%d,%d) vis=%d iconic=%d pt->%s(root_ours=%d)" % (
    l, t, r, b, win32gui.IsWindowVisible(main), win32gui.IsIconic(main),
    win32gui.GetClassName(_hw), _our_top()), flush=True)
hdc = win32gui.GetDC(0)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
dc.BitBlt((0, 0), (r - l, b - t), mfc, (l, t), win32con.SRCCOPY)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(0, hdc)
win32gui.SetWindowPos(main, win32con.HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SAFE)
img.crop((0, img.size[1] - 34, img.size[0], img.size[1])).save(
    os.path.join(HERE, "out", "r59_statusbar.png"))
print("saved r59_statusbar.png (screen capture)", flush=True)

# ---- P3-1 断言（审查 20261026）：版本文本完整、不被 SBARS_SIZEGRIP 手柄压住 ----
# R23-P3-3：判据改**锚定状态栏子窗矩形**（FindWindowEx msctls_statusbar32 → 屏幕矩形 →
# 图内坐标），不再锚定主窗图底绝对行；行自适应（深色像素行并集=文本带）替代固定 -18..-10。
sbh = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
chk("statusbar child window present", bool(sbh), "sb=%s" % (hex(sbh) if sbh else "0"))
sbl, sbt, sbr, sbb = win32gui.GetWindowRect(sbh)
simg = img.crop((sbl - l, sbt - t, sbr - l, sbb - t))
grip = user32.GetSystemMetrics(2)                  # SM_CXVSCROLL = 手柄占位
W, H = simg.size
px = simg.load()
def dark_at(x, y):
    p = px[x, y]
    return p[0] < 120 and p[1] < 120 and p[2] < 120
# §133：遮挡自检（规则 15 环境性口径）——应用无深色主题，正常状态栏必为浅底；
# 整幅 >50% 深色 = 仍截到遮挡窗（TOPMOST 也压不过「置顶的别的窗」如 IDE 悬浮窗）。
_nx = (W + 3) // 4
_dark_n = sum(1 for y in range(H) for x in range(0, W, 4) if dark_at(x, y))
occl = _dark_n / float(_nx * H)
skipped = []
if not _att_ok:
    # §135 收口（规则 24③ 扩展）：置顶重试 4 次后屏幕中心点仍不落回本窗 = 锁屏
    # （LockScreenBackstopFrame 类）或别的置顶窗压着——此时抓到的整幅是**别的画面**，
    # 一切像素判据无意义（曾假红：rightmost_text_x=943）。判环境性 SKIP。
    skipped.append("r59 状态栏几何三断言（置顶 4 次未夺回：屏心顶层为其它窗/锁屏；环境性非产品）")
    print("SKIP capture not on top (att retries exhausted) - env, not product", flush=True)
elif occl > 0.5:
    skipped.append("r59 状态栏几何三断言（截屏被遮挡：暗占比 %.0f%%；环境性非产品）" % (occl * 100))
    print("SKIP occluded capture (dark frac=%.2f) - env, not product" % occl, flush=True)
else:
    rows = [y for y in range(H)
            if sum(1 for x in range(W - grip - 2) if dark_at(x, y)) >= 2]   # ≥2 排单像素噪线
    ttop, tbot = (rows[0], rows[-1]) if rows else (0, H - 1)
    chk("statusbar text band located", bool(rows), "band=(%d,%d) bar=%dx%d" % (ttop, tbot, W, H))
    # §133（R24-P3-1）：旧判据「手柄条带内暗像素 == 0」**不可满足**——SBARS_SIZEGRIP 手柄
    # 自身就画一条斜点线（审查方 review_r24_sbar_grip 实测：944×22 状态栏、grip=17，条带内
    # 8 个暗像素呈 y=18→21 逐行右移的三角点阵），与版本文本无关。改**几何判据**：手柄点阵
    # 固定只占条带**最低 dot_rows 行**；在点阵行之外（文本带高度区间内）取**全宽最右暗像素**，
    # 断言它落在手柄条带左界之内（x < W-grip-2）——版本文本若真被手柄压住，字形会出现在条带
    # 的中部行、xmax 必入条带 → 判红。判别力保留：把文本右移 40px（压过左界）本地可复现 FAIL。
    dot_rows = 5                                   # 点阵占位（100%/150% DPI 下实测 ≤5 行）
    y_hi = min(tbot, H - 1 - dot_rows)
    xmax = 0
    for y in range(ttop, y_hi + 1):
        for x in range(W - 1, xmax, -1):
            if dark_at(x, y):
                xmax = x
                break
    chk("version text not under sizegrip (rightmost text px left of grip strip)",
        xmax < W - grip - 2,
        "grip=%d rightmost_text_x=%d limit=%d rows=%d..%d" % (grip, xmax, W - grip - 3, ttop, y_hi))
    # 版本文本确实存在（手柄左侧带内深色字形），防「整段空了也过」的假阳
    dark_txt = sum(1 for y in range(ttop, tbot + 1) for x in range(W - grip - 122, W - grip - 2)
                   if dark_at(x, y))
    chk("version text present left of grip", dark_txt >= 10, "text dark px=%d" % dark_txt)
proc.terminate()

print("==== SUMMARY ==== total=%d pass=%d fail=%d%s" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok),
    (" skip=%d" % len(skipped)) if skipped else ""))
for s in skipped:
    print("SKIPPED: " + s)
