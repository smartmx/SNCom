# -*- coding: utf-8 -*-
# verify_r65.py - docs/00 §65 折行格数制（容量按格 + HEX 3 格单元 + 前缀补格）E2E：
#  1) HEX 显示模式 + 回环演示长帧（~430B → 连续 hex 长行）→ 找到连续满宽显示行带
#  2) 单元不劈开：每行墨迹簇宽度一致（簇 = "XX" 两字符，被劈开会出单字符簇）
#  3) 列相位对齐：每行最末簇起点 mod (3*cellW) 同余 —— 同时覆盖规则④
#     （首行前缀补格到 3 的倍数；若 H 不是 3 的倍数，首行末簇相位即偏离）
#  4) 相邻行最左墨迹 x 一致（续行从格 0、前缀行从 "[" 起，均为格 0）
#  5) 窄窗容量响应：主窗收窄 → 折行数（滚动范围/行带数）增加
import os, time, subprocess, ctypes, statistics, shutil
import win32gui, win32con, win32process, win32api, win32ui
from PIL import Image
user32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIST = os.environ.get("SNCOM_EXE") or os.path.join(os.path.dirname(HERE), "develop", "dist")   # §114：只读源（仅取 exe）；§120 支持环境变量指定被测 exe（红/绿对照同款口径）
DIST = os.path.join(HERE, "_probe", "out", "r65")                        # §114：沙箱（不再动 develop\dist）
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("  | " + detail if detail else ""), flush=True)

def _phase_cluster(ph, period):
    """§120/§123：右缘相位众数簇分类。返回 (ok, modal, inliers, outliers)——
    ≤2 带可偏离众数簇（±4px 圆周距）；§63 接缝类真 bug 会让带群劈两簇各半 →
    inliers 不足判红（判别力保留）。"""
    if not ph:
        return False, -1, 0, []
    def _cd(a, b):
        d = abs(a - b) % period
        return min(d, period - d)
    modal = max(ph, key=lambda v: sum(1 for u in ph if _cd(u, v) <= 4))
    inl = sum(1 for u in ph if _cd(u, modal) <= 4)
    outl = sorted(u for u in ph if _cd(u, modal) > 4)
    return inl >= len(ph) - 2, modal, inl, outl

# ---- 0) 判据自测（§123/十九轮 P3-4③：bands=15 异常态与渲染态相关、无法现场复现 →
#         用合成相位夹具守住容差分支的判别力）------------------------------------
for _ph, _want, _nm in (
        ([11] * 29, True, "全同簇"),
        ([19, 9, 11, 11, 9, 11, 11, 11, 11, 9, 11, 11, 11, 11, 11], True, "§120 批跑失败签名（1 离群）"),
        ([9, 16] * 10, False, "§63 接缝类真 bug（劈两簇各半）")):
    _ok, _m, _in, _o = _phase_cluster(_ph, 21)
    chk("判据自测：%s" % _nm, _ok == _want, "inliers=%d/%d modal=%s want=%s" % (_in, len(_ph), _m, _want))

def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0

# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = os.path.join(SRC_DIST, "") if "SRC_DIST" in globals() else r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = DIST if "DIST" in globals() else r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r65"
os.makedirs(_SBX_DIST, exist_ok=True)
_EXE = SRC_DIST if os.path.isfile(SRC_DIST) else os.path.join(_SRC_DIST, "sncom.exe")   # SNCOM_EXE 可指 exe 文件或 dist 目录
shutil.copy(_EXE, os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(os.path.join(DIST, "sncom.json"))
except FileNotFoundError:
    pass
proc = subprocess.Popen([os.path.join(DIST, "sncom.exe")], cwd=DIST)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

rx = win32gui.GetDlgItem(main, 3059)               # IDC_RXVIEW
assert rx

def scroll_info(hwnd):
    class SCROLLINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("fMask", ctypes.c_uint),
                    ("nMin", ctypes.c_int), ("nMax", ctypes.c_int),
                    ("nPage", ctypes.c_uint), ("nPos", ctypes.c_int),
                    ("nTrackPos", ctypes.c_int)]
    si = SCROLLINFO(); si.cbSize = ctypes.sizeof(si); si.fMask = 0x1F
    user32.GetScrollInfo(hwnd, 1, ctypes.byref(si))    # SB_VERT
    return si

def capture(hwnd):
    l, t, r, b = win32gui.GetWindowRect(hwnd)
    hdc = win32gui.GetWindowDC(hwnd)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r-l, b-t); dc.SelectObject(bmp)
    if not user32.PrintWindow(hwnd, dc.GetSafeHdc(), 2):
        dc.BitBlt((0,0),(r-l,b-t),hwnd,(0,0),win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA",(r-l,b-t),bits,"raw","BGRA",0,1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img

def ink_cols(img, y0, y1, thr=160):
    w, h = img.size
    cols = []
    for xx in range(2, w-2):
        ink = 0
        for yy in range(y0, y1):
            p = img.getpixel((xx, yy))
            if p[0] < thr and p[1] < thr and p[2] < thr:
                ink += 1
        cols.append(1 if ink else 0)
    return cols

def clusters(mask, off=2):
    out = []
    s = None
    for i, v in enumerate(mask):
        if v and s is None: s = i
        if not v and s is not None:
            out.append((s+off, i-s)); s = None
    if s is not None: out.append((s+off, len(mask)-s))
    return out

def band_rows(img):
    """按行拆带：f(y)=该扫描行墨迹像素数，f<=1 视为行间空隙；返回 [(y0,y1)]"""
    w, h = img.size
    prof = []
    for yy in range(0, h):
        n = 0
        for xx in range(4, w-4, 2):
            p = img.getpixel((xx, yy))
            if p[0] < 160 and p[1] < 160 and p[2] < 160: n += 1
        prof.append(n)
    bands = []
    y = 0
    while y < h:
        if prof[y] > 1:
            y0 = y
            while y < h and prof[y] > 1: y += 1
            bands.append((y0, y))
        else:
            y += 1
    return bands, prof

def find_wrap_run(img, min_bands=8, min_ext=0.55):
    """连续满宽显示行带（长 hex 行折行段）；返回 [(y0,y1)] 与行起点间距。
    过滤 <6px 的噪声带（WS_EX_CLIENTEDGE 边框线整宽全暗，会被误当满宽带）。"""
    w, h = img.size
    bands, prof = band_rows(img)
    # 先按 x 跨度过滤满宽带（并丢弃 <6px 噪声带）
    wide = []
    for (y0, y1) in bands:
        if y1 - y0 < 6:
            continue
        m = ink_cols(img, y0, y1)
        if not m: continue
        xs = [i for i, v in enumerate(m) if v]
        if xs and (xs[-1] - xs[0]) > w * min_ext:
            wide.append((y0, y1))
    # 连续（间距 ≤ 6px）满宽带合并成 run
    best = []
    cur = []
    for bnd in wide:
        if cur and bnd[0] - cur[-1][1] > 6:
            if len(cur) >= min_bands: best = cur
            cur = []
        cur.append(bnd)
    if len(cur) >= min_bands: best = cur
    pitch = statistics.median([b[0]-a[0] for a, b in zip(best, best[1:])]) if len(best) > 2 else 0
    return best, pitch

def mask_cols(img, y0, y1, x0, x1, thr=170):
    m = []
    for xx in range(x0, x1):
        ink = False
        for yy in range(y0, y1):
            p = img.getpixel((xx, yy))
            if p[0] < thr and p[1] < thr and p[2] < thr:
                ink = True; break
        m.append(1 if ink else 0)
    return m

def autocorr_period(m, lo, hi):
    best, bp = -1, 0
    for p in range(lo, hi+1):
        s = sum(1 for x in range(len(m)-p) if m[x] and m[x+p])
        if s > best: best, bp = s, p
    return bp, best

def best_shift(a, b, rng):
    bs, bv = 999, -1
    for s in range(-rng, rng+1):
        cnt = 0
        for x in range(max(0, -s), min(len(a), len(b)-s)):
            if a[x] and b[x+s]: cnt += 1
        if cnt > bv: bv, bs = cnt, s
    return bs, bv

def analyze(img, tag):
    """对 run 做格数制断言（列掩码 + 互相关，抗小字号反锯齿）"""
    w, h = img.size
    x0, x1 = 3, w - 22                      # 裁掉左右边框与垂直滚动条
    run, pitch = find_wrap_run(img)
    chk(tag + ": wrapped run found (>=8 full-width bands)", len(run) >= 8,
        "bands=%d pitch=%s" % (len(run), pitch))
    if len(run) < 8:
        return 0
    half = int(w * 0.55)                    # 右半区 = 纯数据（前缀行不受时间戳污染）
    masks = [mask_cols(img, y0+1, y1-1, x0, x1) for (y0, y1) in run]
    # 周期 = 单元宽（3 格）：最长行右半区自相关
    longest = max(masks, key=sum)
    period, peak = autocorr_period(longest[half-x0:], 10, 36)
    chk(tag + ": unit period detected (≈3 cells)", period >= 10, "period=%s peak=%s" % (period, peak))
    # 相邻行右半互相关位移（整周期位移等价于对齐，按 mod period 归一）≤3px
    # （劈单元/相位错 1 格 → 残差 ≈ period/3 → FAIL）
    ok, worst = True, ""
    for i in range(len(masks)-1):
        s, _ = best_shift(masks[i][half-x0:], masks[i+1][half-x0:], period)
        so = min(s % period, period - (s % period))
        if so > 3:
            ok = False; worst = "band%d->band%d shift=%d (resid %d)" % (i, i+1, s, so); break
    chk(tag + ": consecutive rows column-aligned (xcorr resid<=3px)", ok, worst)
    # 列相位：每行最右墨迹列 mod period 同余（覆盖前缀补格规则④——H 非 3 倍数即偏离）。
    # §120（十八轮 P3-1）收紧口径=**众数簇**：满宽带中偶有首/末带是其它宽行/被裁剪行
    # （渲染态相关：批跑 bands=15 态实测首带 phase=19 而众数=9/11，旧"全带同余"判红为假红；
    #   旧 exe 同签名复现=非回归）。判据=≤2 带可偏离众数簇（±4px 圆周距）；
    # §63 接缝类真 bug 会让带群劈成两簇（各占约一半）→ inliers 不足 → 仍红，判别力保留。
    ph = []
    for m in masks:
        for x in range(len(m)-1, -1, -1):
            if m[x]: ph.append(x % period); break
    okp, modal, inl, outl = _phase_cluster(ph, period)
    chk(tag + ": rightmost-ink phase congruent (modal cluster, <=2 outliers)",
        okp, "modal=%s inliers=%d/%d outliers=%s period=%s" % (modal, inl, len(ph), outl, period))
    print(tag + ": period=%s cellW≈%.1f bands=%d" % (period, period/3.0, len(run)), flush=True)
    return period

# ---- 1) HEX 显示模式 ON + 回环演示 ----
hexshow = win32gui.GetDlgItem(main, 3037)          # IDC_CHK_HEXSHOW
if win32gui.SendMessage(hexshow, 0x00F1, 0, 0) != 4:   # BM_GETCHECK != BST_CHECKED
    win32gui.PostMessage(hexshow, 0x00F5, 0, 0)        # BM_CLICK
    time.sleep(0.4)
win32gui.PostMessage(main, win32con.WM_COMMAND, 2222, 0)   # §67 起 IDM_TOOL_LOOP=2222（2221=资源管理器）
time.sleep(3.0)

# ---- 2) 滚到顶 → 截图 → 断言 ----
win32gui.PostMessage(rx, win32con.WM_VSCROLL, win32con.SB_TOP, 0)
time.sleep(0.6)
img = capture(rx)
out = os.path.join(HERE, "out"); os.makedirs(out, exist_ok=True)
img.save(os.path.join(out, "r65_hex_align.png"))
print("saved r65_hex_align.png", flush=True)
period = analyze(img, "A(960w)")

# ---- 3) 窄窗容量响应 ----
si0 = scroll_info(rx)
n0 = si0.nMax + 1
win32gui.SetWindowPos(main, 0, 0, 0, 560, 700, win32con.SWP_NOMOVE | win32con.SWP_NOZORDER)
time.sleep(0.8)                                    # 折行防抖 150ms + 重排 + 重绘
si1 = scroll_info(rx)
n1 = si1.nMax + 1
chk("narrow window: scroll range grows (capacity recalculated)", n1 > n0, "rows %d -> %d" % (n0, n1))
img2 = capture(rx)
img2.save(os.path.join(out, "r65_hex_narrow.png"))
print("saved r65_hex_narrow.png", flush=True)
if period:
    run2, _ = find_wrap_run(img2)
    chk("narrow window: band count grows", len(run2) >= 12, "bands=%d" % len(run2))
    analyze(img2, "B(560w)")

proc.terminate()
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
