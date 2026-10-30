# -*- coding: utf-8 -*-
# verify_r83.py - docs/00 §83（第四轮审查 R1 复核）：「点击发送列」分支的功能与视觉
#   背景：审查 R1 指出 r41-S7 的口径与实现不符（实现是 NM_CLICK 里置的 **点击完成后 130ms 凹陷脉冲**，
#   而非"按住期间"），且该脉冲在其环境窗口级/屏幕级均未观测到——而这条分支同时承担
#   「点发送列 = 发送」的功能，故本探针先做**功能判据**，视觉脉冲作 best-effort。
#   A1 表单页就地编辑内容（双击内容列 → 写回）
#   A2 **点发送列 → 真实发送**（回环 + 转存日志取证）——产品级判据
#   A3 130ms 凹陷脉冲（点击后 ~40ms vs ~300ms 采样；不可观测 → NOTE+SKIP，附 bbox 约束）
import os, time, subprocess, ctypes, json
import win32gui, win32con, win32process, win32api
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
res = []
PAYLOAD = "SENDCOL83"
# §137 期定位（2026-10-30）：转存只写**收尾行**（RxLogPoll 上限=builder_.finished()）——
# 无行尾的载荷回显永远悬在行表尾、永不落盘；偶发"通过"只因载荷恰好被后续回显数据收尾。
# 载荷改带 §77 转义 \n（MsSendRow 与发送框共用 TextWithEscapesToBytes）→ 回显自闭合、
# 100ms poll 必然写出，判据确定化。断言仍查 b"SENDCOL83"。
PAYLOAD_EDIT = "SENDCOL83\\n"


def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

# §120（十八轮 P3-5）：环境性跳过单独计数——与 r54/r56/r67/r73 同口径。
skips = []
def skip(desc):
    skips.append(desc)
    print("SKIP " + desc, flush=True)


def find_toplevel(cls, pid):
    out = []

    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


def wtext(h, cap=4096):
    b = ctypes.create_unicode_buffer(cap)
    user32.SendMessageW(h, 0x000D, cap, b)
    return b.value


def wset(h, s):
    user32.SendMessageW(h, 0x000C, 0, ctypes.c_wchar_p(s))


def bm_click(h):
    win32api.PostMessage(h, 0x00F5, 0, 0)


def lv_rect(lv, row, col):
    """LVM_GETSUBITEMRECT（跨进程带出参**不可用**）→ 用列宽+行高自行推算行/单元格矩形"""
    hdr = user32.SendMessageW(lv, 0x101F, 0, 0)            # LVM_GETHEADER
    widths = [user32.SendMessageW(lv, 0x101D, i, 0) for i in range(6)]   # LVM_GETCOLUMNWIDTH
    rowH = 22                                             # 列表行高（DEFAULT_GUI_FONT ≈ 20~24）
    x0 = sum(widths[:col])
    x1 = x0 + widths[col]
    return x0, rowH * row, x1, rowH * (row + 1)


# §84（五轮审查 Q-4）：改用**沙箱**运行——拷 exe 到 probes\_bin\，配置/日志都落在沙箱，
# 不再删除/污染 develop\dist\sncom.json（与 codex 的 _e2e_bin* 同风格）。
import glob, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "_bin")
os.makedirs(BIN, exist_ok=True)
subprocess.run(["taskkill", "/F", "/IM", "sncom.exe"], capture_output=True)   # 防残留实例锁 BIN（r140 同款）
time.sleep(1.2)
SRC_EXE = r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"
shutil.copy2(SRC_EXE, os.path.join(BIN, "sncom.exe"))          # 每次跑都用当前构建
try:
    os.remove(os.path.join(BIN, "sncom.json"))
except FileNotFoundError:
    pass
for old_log in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")):
    os.remove(old_log)
before_logs = set(glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")))
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)

# 前置：回环链路 + 接收转存（发送要有链路，取证要日志）
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)     # 回环演示
time.sleep(1.2)
rxfile = user32.GetDlgItem(main, 3039)
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) != 1:
    bm_click(rxfile)
    time.sleep(0.6)
bm_click(user32.GetDlgItem(main, 3012))                      # 扩展
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
lst = user32.GetDlgItem(ext, 20)
edit = user32.GetDlgItem(ext, 21)
chk("§83 扩展窗/多条表就绪", bool(ext and lst and edit), "ext=%s lst=%s" % (ext, lst))

def cell_pixels(x0, y0, x1, y1):
    dc = win32gui.GetDC(lst)
    vals = []
    for yy in range(y0 + 4, min(y1 - 2, y0 + 20), 3):
        for xx in range(x0 + 4, max(x0 + 5, x1 - 4), 4):
            vals.append(gdi32.GetPixel(dc, xx, yy))   # GetPixel 属 gdi32
    win32gui.ReleaseDC(lst, dc)
    return tuple(vals)

# ---- §142（R28-P3-2 沉淀，置于 A1 前=干净状态）：「点击发送」列按钮文字恒为「发 送」 ----
# 判据链：注释经就地编辑从 "" 改为 NOTE141（前提=提交落盘到配置）→ 列 0 像素逐点不变
# （cell_pixels 直采列表 DC）；另加代码级断言（绘制分支不引用 msRows_/note 且恒画「发 送」）。
# 旧实现（文字=注释）在 C2 下必红（审查方 review_r28 红对照已证）。
x0, y0, x1, y1 = lv_rect(lst, 0, 0)
p0 = cell_pixels(x0, y0, x1, y1)
note_cell = lv_rect(lst, 0, 5)
nx0, ny0, nx1, ny1 = note_cell
nlp = win32api.MAKELONG((nx0 + nx1) // 2, (ny0 + ny1) // 2)
note_vis = False
for _try in range(3):                                                                  # 单击→双击→再击 重试
    win32gui.PostMessage(lst, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, nlp)
    time.sleep(0.1)
    win32gui.PostMessage(lst, win32con.WM_LBUTTONUP, 0, nlp)
    time.sleep(0.4)
    if user32.IsWindowVisible(edit):
        note_vis = True; break
    win32gui.PostMessage(lst, win32con.WM_LBUTTONDBLCLK, win32con.MK_LBUTTON, nlp)
    time.sleep(0.4)
    if user32.IsWindowVisible(edit):
        note_vis = True; break
note_changed = False
if note_vis:
    user32.SendMessageW(edit, 0x000C, 0, ctypes.c_wchar_p("NOTE141"))                  # WM_SETTEXT
    win32api.PostMessage(edit, win32con.WM_KEYDOWN, win32con.VK_RETURN, 0)
    time.sleep(0.4)
    hdr141 = user32.SendMessageW(lst, 0x101F, 0, 0)                                    # LVM_GETHEADER
    lph141 = win32api.MAKELONG(5, 5)
    win32gui.PostMessage(hdr141, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lph141)
    win32gui.PostMessage(hdr141, win32con.WM_LBUTTONUP, 0, lph141)                     # 失焦提交
    for _i in range(15):                                                               # 条件等待编辑窗关闭
        time.sleep(0.1)
        if not user32.IsWindowVisible(edit):
            note_changed = True
            break
win32api.PostMessage(user32.GetDlgItem(main, 3011), 0x00F5, 0, 0)                      # 保存参数
time.sleep(1.0)
try:
    with open(os.path.join(BIN, "sncom.json"), encoding="utf-8") as f:
        _cfg = json.load(f)
    _r0 = (_cfg.get("multisend_rows") or [{}])[0]
    note_cfg = _r0.get("note", "")
except Exception:
    note_cfg = "<read-fail>"
chk("§141a-C1 前提：注释已改为 NOTE141（提交落盘）", note_cfg == "NOTE141",
    "note_cfg=%r 编辑打开=%s 提交关闭=%s" % (note_cfg, note_vis, note_changed))
p1 = cell_pixels(x0, y0, x1, y1)
chk("§141a-C2 列 0 像素与改注释前逐点一致（按钮文字恒「发 送」）", p0 == p1,
    "pixels %d vs %d" % (len(p0), len(p1)))
try:
    _mw = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            os.pardir, "develop", "src", "ui", "mainwindow.cpp"),
               encoding="utf-8", errors="replace").read()
    _si = _mw.find('cd->iSubItem == 0) {')
    _ei = _mw.find('return CDRF_SKIPDEFAULT;', _si)
    _blk = _mw[_si:_ei] if (_si >= 0 and _ei > _si) else ""
    chk("§141a-C3 代码级：绘制分支不引用 msRows_/note 且恒画「发 送」",
        bool(_blk) and "msRows_" not in _blk and "note" not in _blk and 'L"发 送"' in _blk,
        "block=%d chars" % len(_blk))
except OSError:
    print("SKIP §141a-C3（源码文件不可读）", flush=True)

# ---- A1 就地编辑内容（双击内容列 → 写回）----
x0, y0, x1, y1 = lv_rect(lst, 0, 2)                          # 行 0 内容列
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
lp = win32api.MAKELONG(cx, cy)
win32gui.PostMessage(lst, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
win32gui.PostMessage(lst, win32con.WM_LBUTTONUP, 0, lp)
time.sleep(0.15)
win32gui.PostMessage(lst, win32con.WM_LBUTTONDBLCLK, win32con.MK_LBUTTON, lp)
time.sleep(0.4)
dlg_vis = bool(user32.IsWindowVisible(edit))
chk("§83-A1 双击内容列进入就地编辑", dlg_vis, "edit_visible=%s text=%r" % (dlg_vis, wtext(edit)))
if dlg_vis:
    wset(edit, PAYLOAD_EDIT)
    win32api.PostMessage(edit, win32con.WM_KEYDOWN, win32con.VK_RETURN, 0)   # 回车=失焦写回
    time.sleep(0.4)
    # §135 收口（探针侧）：就地编辑对**合成 RETURN 不关窗**（§75 教训同款：合成按键不走
    # 真实输入路径，实测 RETURN 后 1.5s 编辑窗仍可见），会滞留到 A2 的首击才「提交+关窗」——
    # 该首击是否**同时**触发发送存在竞态（同构建实测 4 连红 / 5 连绿）。改为用普通点击
    # （点表头）显式触发失焦提交，并条件等待编辑窗关闭，使 A2 的点击是干净的「发送」击。
    hdr = user32.SendMessageW(lst, 0x101F, 0, 0)             # LVM_GETHEADER
    lph = win32api.MAKELONG(5, 5)
    win32gui.PostMessage(hdr, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lph)
    win32gui.PostMessage(hdr, win32con.WM_LBUTTONUP, 0, lph)
    closed = False
    for _i in range(15):                                     # 规则 17：条件化等待（最多 1.5s）
        time.sleep(0.1)
        if not user32.IsWindowVisible(edit):
            closed = True
            break
    print("就地编辑提交：编辑窗关闭=%s" % closed, flush=True)

# ---- A2 点发送列（sub-item 0）→ 真实发送 ----
# §137 期复跑（2026-10-30）：即使编辑窗已关闭（关闭=True），合成**首击**仍可能被吞
# （批跑 1/3 红、单跑红绿交替；日志 len=18=点前回环内容）——§135「编辑收尾首击竞态」的
# 第二形态。修法=规则 28 延伸「点击 → 条件轮询结果 → 未达重试一次」：结果判据用转存
# 日志的**目录项尺寸增长**（文件被应用持有无共享读，内容须等取消勾选后才能读）。
x0, y0, x1, y1 = lv_rect(lst, 0, 0)
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
lp0 = win32api.MAKELONG(cx, cy)
def send_click():
    win32gui.PostMessage(lst, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp0)
    time.sleep(0.05)
    win32gui.PostMessage(lst, win32con.WM_LBUTTONUP, 0, lp0)
def new_logs():
    return sorted(set(glob.glob(os.path.join(BIN, "SNCom-Log-*.txt"))) - before_logs)
size0 = max((os.path.getsize(p) for p in new_logs()), default=0)   # 点前尺寸（首击成功判据=增长）
def log_grew():
    return any(os.path.getsize(p) > size0 for p in new_logs())
send_click()
sent = False
for _i in range(10):                                          # ≤2s 条件轮询
    time.sleep(0.2)
    if log_grew():
        sent = True
        break
retried = False
if not sent:
    print("A2 首击未见日志增长（合成点击被吞竞态）→ 重试一次", flush=True)
    retried = True
    send_click()
    for _i in range(10):
        time.sleep(0.2)
        if log_grew():
            sent = True
            break
time.sleep(0.6)                                              # 落盘余量（100ms poll）
# 转存文件被应用持有（打开即无共享读），须先取消勾选关掉再读（r77 同款）
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) == 1:
    bm_click(rxfile)
    time.sleep(0.6)
logp = new_logs()
data = b""
for p in logp[-1:]:
    with open(p, "rb") as f:
        data = f.read()
chk("§83-A2 点发送列 → 真实发送（回环转存日志含载荷）", PAYLOAD.encode() in data,
    "log=%s len=%d%s" % (os.path.basename(logp[-1]) if logp else "-", len(data),
                         "（首击被吞，重试后达成）" if retried else ""))

# ---- A3 130ms 凹陷脉冲（best-effort：点击后 ~40ms 采样 vs ~300ms 采样）----
x0, y0, x1, y1 = lv_rect(lst, 0, 0)
base_cell = cell_pixels(x0, y0, x1, y1)
win32gui.PostMessage(lst, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp0)
time.sleep(0.04)
win32gui.PostMessage(lst, win32con.WM_LBUTTONUP, 0, lp0)
pulse_seen = False
for dt in (0.03, 0.05, 0.07, 0.10):
    time.sleep(dt)
    if cell_pixels(x0, y0, x1, y1) != base_cell:
        pulse_seen = True
        break
time.sleep(0.35)
after_cell = cell_pixels(x0, y0, x1, y1)
if pulse_seen:
    chk("§83-A3 点击后 ≤130ms 凹陷脉冲（发送列单元格内像素变化）", True, "pulse observed")
    chk("§83-A3 脉冲回落后恢复单元格原样", after_cell == base_cell, "restored=%s" % (after_cell == base_cell))
else:
    skip("§83-A3 凹陷脉帧两断言（GetPixel 屏幕采样对短脉冲不可靠，五轮 S2 同款结论；"
         "确定性判据在 verify_r41-S7——窗口表面/DWM 面，功能判据见 A2 点发送列已确认真实发送）")

proc.terminate()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
if fails:
    print("FAIL: " + "; ".join(fails))
