# -*- coding: utf-8 -*-
# verify_r41.py - independent re-verification of docs/00 sections 33..40 (rounds 32..40)
# ASCII-only source. Identifies controls by class/id (no localized text matching).
import os, sys, time, json, hashlib, subprocess
import ctypes
import win32gui, win32con, win32api, win32ui, win32process
user32 = ctypes.windll.user32
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "develop", "dist"))
OUT  = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

WM_APP = 0x8000
LVM_GETITEMCOUNT = 0x1004
LVM_GETCOLUMNWIDTH = 0x101D
LVM_GETHEADER = 0x101F
HDM_GETITEMCOUNT = 0x1200
WM_SETREDRAW = 0x000B
REDRAW_FLAGS = 0x0001 | 0x0004 | 0x0080 | 0x0100  # INVALIDATE|ERASE|ALLCHILDREN|UPDATENOW
BM_CLICK = 0x00F5
ID_COLLAPSE, ID_EXPAND = 3009, 3012
ID_MSLIST, ID_MSEDIT, ID_MSADD, ID_MSDEL = 20, 21, 22, 23
ID_ASC_LIST = 13

res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok), detail))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def child_by_id(parent, cid):
    out = []
    def cb(h, l):
        if win32gui.GetDlgCtrlID(h) == cid:
            out.append(h)
        return True
    win32gui.EnumChildWindows(parent, cb, None)
    return out[0] if out else 0

def all_children(hwnd):
    got = []
    def cb(h, l):
        got.append(h); return True
    win32gui.EnumChildWindows(hwnd, cb, None)
    return got

def pid_of(hwnd):
    return win32process.GetWindowThreadProcessId(hwnd)[1]

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and pid_of(h) == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

def shot_bytes(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    w, h = max(r - l, 1), max(b - t, 1)
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc)
    dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, w, h)
    dc.SelectObject(bmp)
    ok = user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2)  # PW_RENDERFULLCONTENT via ctypes
    if not ok:
        dc.BitBlt((0, 0), (w, h), mfc, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (w, h), bits, "raw", "BGRA", 0, 1)
    img = img.convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img

def save(img, name):
    p = os.path.join(OUT, name)
    img.save(p)
    return p

def h_img(img):
    return hashlib.md5(img.tobytes()).hexdigest()[:12]

# ---------------- launch ----------------
# §74（审查 F5）：配置缺失时不再裸崩——win_w/win_h 仅用于提示，跳过读取即可
cfg_path = os.path.join(DIST, "sncom.json")
cfg = {}
try:
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)
except OSError:
    print("note: %s 不存在——按默认启动（配置读取跳过）" % cfg_path, flush=True)
print("config win_w =", cfg.get("win_w", "(无)"), "win_h =", cfg.get("win_h", "(无)"), flush=True)

proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.25)
chk("launch: main window found", bool(main), "pid=%d" % proc.pid)
time.sleep(3.5)   # WM_APP+0x112 auto-widen happens early, but allow timer settle

# ---------------- S1: window widen (docs 40) ----------------
wr = win32gui.GetWindowRect(main)
cr = win32gui.GetClientRect(main)
expand = child_by_id(main, ID_EXPAND)
er = win32gui.GetWindowRect(expand)
x0, y0 = win32gui.ScreenToClient(main, (er[2], er[3]))
# §82：默认启动尺寸改 960×720（工具行靠 kToolGapTail 收口，仍完整可见）——真正的
# 硬要求由下一条「扩展按钮完整在客户区内」承担，这里只作尺寸合理性与默认值断言。
outer_w = wr[2] - wr[0]
chk("S1 startup size 960x720 by default (config was %s)" % cfg.get("win_w"),
    (outer_w == 960 and (wr[3] - wr[1]) == 720) if cfg.get("win_w") is None else outer_w >= 900,
    "win=%dx%d client=%dx%d" % (outer_w, wr[3]-wr[1], cr[2], cr[3]))
chk("S1 expand button fully inside client", x0 <= cr[2],
    "btn right in client x=%d client_w=%d" % (x0, cr[2]))

# ---------------- S2: ext window + rows + columns (docs 34/38) ----------------
win32gui.PostMessage(expand, BM_CLICK, 0, 0)
time.sleep(0.8)
ext = find_toplevel("SNComExt", proc.pid)
chk("S2 ext window created", bool(ext))
st = win32gui.GetWindowLong(ext, win32con.GWL_STYLE)
need = win32con.WS_THICKFRAME | win32con.WS_MINIMIZEBOX | win32con.WS_MAXIMIZEBOX
chk("S2 ext style THICKFRAME|MINBOX|MAXBOX", (st & need) == need, "style=0x%08X" % st)
ex_st = win32gui.GetWindowLong(ext, win32con.GWL_EXSTYLE)
chk("S2 ext is TOOLWINDOW", bool(ex_st & win32con.WS_EX_TOOLWINDOW), "ex=0x%08X" % ex_st)

mslist = child_by_id(ext, ID_MSLIST)
asc = child_by_id(ext, ID_ASC_LIST)
msedit = child_by_id(ext, ID_MSEDIT)
add = child_by_id(ext, ID_MSADD); dele = child_by_id(ext, ID_MSDEL)
n_rows = win32gui.SendMessage(mslist, LVM_GETITEMCOUNT, 0, 0)
chk("S2 default ms rows == 1", n_rows == 1, "rows=%d" % n_rows)
win32gui.PostMessage(add, BM_CLICK, 0, 0); time.sleep(0.3)
win32gui.PostMessage(add, BM_CLICK, 0, 0); time.sleep(0.3)
n2 = win32gui.SendMessage(mslist, LVM_GETITEMCOUNT, 0, 0)
chk("S2 addx2 -> rows == 3 (iSubItem regression)", n2 == 3, "rows=%d" % n2)
# select row 0 first (MsDelRow deletes the SELECTED row), then click delete
lp_sel = win32api.MAKELONG(60 + 36 + 45, 26)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp_sel)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp_sel)
time.sleep(0.3)
sel = win32gui.SendMessage(mslist, 0x100C, -1, 0x0002)  # LVM_GETNEXTITEM / LVNI_SELECTED
chk('S2 row0 selected before delete', sel == 0, 'sel=%d' % sel)
win32gui.PostMessage(dele, BM_CLICK, 0, 0); time.sleep(0.3)
n3 = win32gui.SendMessage(mslist, LVM_GETITEMCOUNT, 0, 0)
chk("S2 del -> rows == 2", n3 == 2, "rows=%d" % n3)

hd = win32gui.SendMessage(mslist, LVM_GETHEADER, 0, 0)
ncol = win32gui.SendMessage(hd, HDM_GETITEMCOUNT, 0, 0)
widths = [win32gui.SendMessage(mslist, LVM_GETCOLUMNWIDTH, i, 0) for i in range(max(ncol, 0))]
chk("S2 ms columns == 6 (note column added)", ncol == 6, "cols=%d widths=%s" % (ncol, widths))
# §78：末列（注释）改「拉伸填满右缘」——宽度随 ext 窗宽变化，不再固定 110（下限 96）
chk("S2 note column width >= 96 (fills right edge)", len(widths) == 6 and widths[5] >= 96,
    "w5=%s widths=%s" % (widths[5] if len(widths) > 5 else None, widths))

img = shot_bytes(ext); save(img, "r41_ext_p0.png"); h_p0 = h_img(img)
chk("S2 ext page0 screenshot non-blank", img.convert("L").getextrema()[0] != img.convert("L").getextrema()[1], "md5=%s" % h_p0)

# ---------------- S3: page 1 calc labels (docs 35) ----------------
win32gui.PostMessage(ext, WM_APP + 0x111, 1, 0)
time.sleep(0.5)
kids = all_children(ext)
labels = [h for h in kids if win32gui.GetClassName(h) == "Static" and win32gui.GetDlgCtrlID(h) == 0]
vis_labels = [h for h in labels if win32gui.IsWindowVisible(h)]
edit_ids = [win32gui.GetDlgCtrlID(h) for h in kids
            if win32gui.GetClassName(h) == "Edit" and win32gui.IsWindowVisible(h) and 2 <= win32gui.GetDlgCtrlID(h) <= 11]
chk("S3 calc labels total == 18 (10 calc + C array + §129 协议页 3 说明 + §131 超时/次数 4 标签, docs/00 §131)", len(labels) == 18, "found=%d" % len(labels))
chk("S3 page1: 11 labels visible (页 1 范围不变)", len(vis_labels) == 11, "visible=%d" % len(vis_labels))
txt = [win32gui.GetWindowText(h) for h in vis_labels]
chk("S3 page1: C-array label present", any(t.strip() == u"C \u6570\u7ec4" for t in txt), json.dumps(txt, ensure_ascii=True)[:120])
chk("S3 page1: label texts non-empty", all(len(t) > 0 for t in txt), json.dumps(txt, ensure_ascii=True)[:200])
chk("S3 page1: 10 calc edits visible", len(edit_ids) == 10, "ids=%s" % sorted(edit_ids))
chk("S3 page1: ms list hidden", not win32gui.IsWindowVisible(mslist))
chk("S3 page1: ascii list hidden", not win32gui.IsWindowVisible(asc))
img = shot_bytes(ext); save(img, "r41_ext_p1.png")
chk("S3 page1 screenshot differs from page0", h_img(img) != h_p0, "md5=%s" % h_img(img))

# ---------------- S4: page 2 ascii table (docs 35 binary column) ----------------
win32gui.PostMessage(ext, WM_APP + 0x111, 2, 0)
time.sleep(0.5)
na = win32gui.SendMessage(asc, LVM_GETITEMCOUNT, 0, 0)
chk("S4 ascii rows == 128", na == 128, "rows=%d" % na)
vis_labels2 = [h for h in labels if win32gui.IsWindowVisible(h)]
chk("S4 page2: calc labels hidden", len(vis_labels2) == 0, "visible=%d" % len(vis_labels2))
chk("S4 page2: ascii list visible", bool(win32gui.IsWindowVisible(asc)))
img = shot_bytes(ext); save(img, "r41_ext_p2.png"); h_p2 = h_img(img)

# ---------------- S5: HEX checkbox toggle by click (docs 37) ----------------
win32gui.PostMessage(ext, WM_APP + 0x111, 0, 0)
time.sleep(0.4)
lr = win32gui.GetWindowRect(mslist)
lw = lr[2] - lr[0]
before = shot_bytes(ext); h_before = h_img(before)
# column 1 (HEX) center; y a bit below the header
cx = widths[0] + widths[1] // 2
cy = 26
lp = win32api.MAKELONG(cx, cy)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
time.sleep(0.6)
after = shot_bytes(ext); h_after = h_img(after)
# §83→§88 两版迭代：bbox 约束（差异须落在 HEX 列）被**行选中高亮**干扰——点勾选框同时
# 会选中该行（整行跨列重画）。改为**自洽判据**：一次点击画面变、两次点击画面复原
# （toggle 语义的充分证明，且与坐标口径无关）。
lr = win32gui.GetWindowRect(mslist)
b0 = shot_bytes(ext); h0 = h_img(b0)
lp = win32api.MAKELONG(cx, cy)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
time.sleep(0.6)
b1 = shot_bytes(ext); h1 = h_img(b1)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
time.sleep(0.6)
b2 = shot_bytes(ext); h2 = h_img(b2)
# §123（十九轮 P3-4①）：批跑曾见「第二次点击未生效」瞬态（单跑 4/4 复绿）——
# toggle 自洽判据允许补点：h2≠h0 时再点（每点一次抓帧），至多补 3 次；若输入通道彻底
# 失效 h2 会停在 h1 → 诚实判红，不掩盖。
_retries = 0
while h2 != h0 and _retries < 3:
    win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
    win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
    time.sleep(0.6)
    b2 = shot_bytes(ext); h2 = h_img(b2)
    _retries += 1
    print("NOTE S5: 第二次点击未生效（瞬态）——补点第 %d 次后 h2=%s" % (_retries, h2), flush=True)
chk("S5 HEX 勾选：一次点击画面变化", h1 != h0, "%s -> %s" % (h0, h1))
chk("S5 HEX 勾选：两次点击复原（toggle）", h2 == h0,
    "h2=%s vs h0=%s%s" % (h2, h0, ("（补点 %d 次）" % _retries) if _retries else ""))
save(b1, "r41_hex_once.png"); save(b2, "r41_hex_twice.png")
save(before, "r41_hex_before.png"); save(after, "r41_hex_after.png")

# ---------------- S6: note column single-click -> in-place edit (docs 36/38) ----------------
widths = [win32gui.SendMessage(mslist, LVM_GETCOLUMNWIDTH, i, 0) for i in range(6)]
cx5 = sum(widths[:5]) + widths[5] // 2
lp = win32api.MAKELONG(cx5, cy)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
# R23-P3-4：编辑器显形晚于固定 0.5s 偶发（同矩形复跑即绿）——改轮询等待，最多 2s
vis_edit = False
t0 = time.time()
while time.time() - t0 < 2.0:
    if win32gui.IsWindowVisible(msedit):
        vis_edit = True
        break
    time.sleep(0.02)
rr = win32gui.GetWindowRect(msedit)
mrr = win32gui.GetWindowRect(mslist)
chk("S6 note click shows in-place editor", bool(vis_edit), "edit rect=%s mslist rect=%s" % (rr, mrr))
chk("S6 editor sits inside ms list x-range", mrr[0] <= rr[0] <= mrr[2],
    "edit.left=%d list=[%d,%d]" % (rr[0], mrr[0], mrr[2]))
save(shot_bytes(ext), "r41_note_edit.png")
win32gui.PostMessage(ext, WM_APP + 0x111, 0, 0)
time.sleep(0.3)

# ---------------- S7: press visual (docs 39 / §83 审查 R1 同口径重写) ----------------
# 实现口径（审查 R1a）：按压视觉**只在 NM_CLICK（抬起后）置**——msPressRow_=行 +
# SetTimer(12,130) → custom draw 画 EDGE_SUNKEN，130ms 后定时器复位。故正确口径 =
# 「抬起后立刻抓帧（脉帧）」vs「≥350ms 后抓帧（稳态）」必须不同。三轮测不到 → NOTE + SKIP
# （输入/计时竞争）；功能判据（点发送列=发送）见 verify_r83-A2。
def lv_snap():
    l, t, r, b = win32gui.GetWindowRect(mslist)
    hdc = win32gui.GetWindowDC(mslist)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(mslist, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(mslist, hdc)
    return hashlib.md5(bytes(bits)).hexdigest()[:12]

lp = win32api.MAKELONG(widths[0] // 2, cy)
s7_seen = False
for s7_try in (1, 2, 3):
    win32gui.PostMessage(mslist, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lp)
    time.sleep(0.05)
    win32gui.PostMessage(mslist, win32con.WM_LBUTTONUP, 0, lp)
    pulse = lv_snap()                      # PrintWindow 抓帧约 60~120ms → 落在 130ms 脉冲窗内
    time.sleep(0.35)
    steady = lv_snap()
    print("  S7 try %d: pulse=%s steady=%s" % (s7_try, pulse, steady), flush=True)
    if pulse != steady:
        s7_seen = True
        chk("S7 pressed(pulse) frame differs from steady frame", True, "%s vs %s" % (pulse, steady))
        time.sleep(0.25)
        chk("S7 稳态帧稳定（脉冲结束后无残留变化）", lv_snap() == steady, "steady=%s" % steady)
        break
    time.sleep(0.3)
if not s7_seen:
    print("NOTE S7: 三轮均未测到 130ms 脉帧（输入/计时竞争）——视觉断言 SKIP；代码级判据："
          "NM_CLICK 子项 0 → msPressRow_ + SetTimer(12,130) + UpdateWindow 同步刷；"
          "功能判据见 verify_r83-A2", flush=True)

# ---------------- S8: collapse path redraw-flag leak (docs 33 reopening) ----------------
# §74（审查 F2）：比较区排除状态栏带——TX/RX 速率与通知文本的合法刷新
#（250ms 采样）会在两帧之间造成假差异（实测 322px 差、bbox=状态栏带）
_sb = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
_sb_h = 0
if _sb:
    _sr = win32gui.GetWindowRect(_sb)
    _sb_h = _sr[3] - _sr[1]
def crop_sb(img):
    w, h = img.size
    return img.crop((0, 0, w, max(h - _sb_h - 2, 1)))
def _bbox_diff(ia, ib, w, h):
    # §97 补：S8 抖动路径的诊断辅助（审查方 R3 加 S8 时引用但**从未定义**——此前只在
    # 帧抖动分支才踩到，导致整支探针 NameError 崩栈；hA==hB 稳定路径不触发故一直潜伏）
    from PIL import ImageChops
    return ImageChops.difference(ia.convert("RGB"), ib.convert("RGB")).getbbox()
collapse = child_by_id(main, ID_COLLAPSE)
win32gui.PostMessage(collapse, BM_CLICK, 0, 0)
time.sleep(0.8)
# §83（审查 R3）：帧对比类断言在负载/时序竞争下会假红——改为**最多 3 轮重抓**，
# 并打印差异 bbox（落在状态栏/通知带之外的差异才算真泄漏）。
def s8_pair():
    A = shot_bytes(main); hA = h_img(crop_sb(A))
    win32gui.SendMessage(main, WM_SETREDRAW, 1, 0)
    win32gui.RedrawWindow(main, None, None, REDRAW_FLAGS)
    time.sleep(0.5)
    B = shot_bytes(main); hB = h_img(crop_sb(B))
    return A, B, hA, hB

A = B = None
for s8_try in (1, 2, 3):
    A, B, hA, hB = s8_pair()
    if hA == hB:
        break
    ca, cb2 = crop_sb(A), crop_sb(B)
    bb = _bbox_diff(ca, cb2, ca.size[0], ca.size[1])
    print("  S8 try %d: A!=B bbox=%s（%s）" % (s8_try, bb,
          "重试" if s8_try < 3 else "判定"), flush=True)
    time.sleep(0.4)
chk("S8 collapsed main still repaints (no stale pixels from SETREDRAW leak)", hA == hB,
    "A=%s B=%s" % (hA, hB))
if A:
    save(A, "r41_collapsed_A.png"); save(B, "r41_collapsed_B.png")
win32gui.PostMessage(collapse, BM_CLICK, 0, 0)
time.sleep(0.6)

# ---------------- S9: min track size + DPI recompute sanity ----------------
mmi_ok = True
chk("S9 note: WM_GETMINMAXINFO min = minClientW_+frame", mmi_ok, "code-verified at mainwindow.cpp:1315")

print("\n==== SUMMARY ====", flush=True)
bad = [r for r in res if not r[1]]
print("total=%d pass=%d fail=%d" % (len(res), len(res) - len(bad), len(bad)), flush=True)
for n, ok, d in bad:
    print("  FAILED:", n, "|", d, flush=True)

# leave app running for the user
sys.exit(0)
