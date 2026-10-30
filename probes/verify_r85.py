# -*- coding: utf-8 -*-
# verify_r85.py - 用户报：点「收起」收起下方界面 → 再点「展开」后出现**组件混乱**。
#   本探针用「三态几何/可见性对照」定位：全新状态（基准） vs 收起态 vs 展开态——
#   展开态必须与基准**逐控件一致**（位置+尺寸+可见性），否则即混乱根因所在。
import os, json, time, subprocess, ctypes
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


def bm_click(h):
    win32api.PostMessage(h, 0x00F5, 0, 0)


# §88（六轮审查 T2）：本探针曾**无判别力**（在 §85 修复前构建上同样 10/10——短高配置
# 这条真实触发路径根本没测）。改为**沙箱 + 短高配置**判别：
#   ①旧配置 win_h=300 → 启动必须被钳到最小窗高（外框 ≥340）且**接收区不压面板行**
#     （修复前构建在此场景重叠 44px，六轮审查 A/B 实证）；
# ②再以默认高度做三态逐控件对照（原有口径）。
import glob, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "_bin")
os.makedirs(BIN, exist_ok=True)
SRC_EXE = r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"
shutil.copy2(SRC_EXE, os.path.join(BIN, "sncom.exe"))
CFG = os.path.join(BIN, "sncom.json")
with open(CFG, "w", encoding="utf-8") as f:
    json.dump({"mode": "serial", "win_w": 1024, "win_h": 300}, f)   # 短高配置（触发路径）
for old_log in glob.glob(os.path.join(BIN, "SNCom-Log-*.txt")):
    os.remove(old_log)
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)
ml, mt, mr, mb = win32gui.GetWindowRect(main)

IDS = {3009: "收起按钮", 3014: "端口combo", 3019: "波特率", 3021: "数据位", 3025: "校验位",
       3027: "流控", 3036: "BREAK", 3037: "HEX显示", 3040: "分包", 3043: "超时ms", 3045: "HEX发送",
       3051: "第N字节", 3055: "校验combo", 3056: "校验值", 3057: "发送框", 3058: "发送按钮",
       3059: "接收视图"}
def snapshot():
    out = {}
    for cid in IDS:
        h = user32.GetDlgItem(main, cid)
        if not h:
            out[cid] = None
            continue
        l, t, r, b = win32gui.GetWindowRect(h)
        out[cid] = (l - ml, t - mt, r - l, b - t, bool(user32.IsWindowVisible(h)))
    return out

def show(tag, snap):
    print("  [%s]" % tag, flush=True)
    for cid in (3059, 3057, 3058, 3014, 3019, 3037, 3043, 3051, 3056):
        v = snap.get(cid)
        print("     %-8s id=%-5d %s" % (IDS[cid], cid,
              "None" if v is None else "x=%d y=%d w=%d h=%d vis=%s" % v), flush=True)

# ---- §88 判别断言：短高配置 → 启动钳到最小窗高且不重叠（修复前构建在此红） ----
ml0, mt0, mr0, mb0 = win32gui.GetWindowRect(main)
outer_h = mb0 - mt0
chk("§85-判别 旧配置 win_h=300 → 启动钳到 ≥340", outer_h >= 340, "outer_h=%d" % outer_h)
short0 = snapshot()
rx, port = short0[3059], short0[3014]
chk("§85-判别 短窗启动不重叠（rx底 ≤ 面板顶）", (rx[1] + rx[3]) <= port[1],
    "rx bottom=%d port top=%d" % (rx[1] + rx[3], port[1]))

base = snapshot()
show("基准（全新）", base)
chk("基准：面板可见（端口/发送框/发送按钮）",
    base[3014][4] and base[3057][4] and base[3058][4], "")

bm_click(user32.GetDlgItem(main, 3009))          # 收起
time.sleep(0.8)
coll = snapshot()
show("收起后", coll)
chk("收起：面板控件全部隐藏（含发送框/发送按钮）",
    not coll[3014][4] and not coll[3057][4] and not coll[3058][4],
    "port=%s send=%s btn=%s" % (coll[3014][4], coll[3057][4], coll[3058][4]))
chk("收起：接收视图变高（吸收面板空间）", coll[3059][3] > base[3059][3],
    "h %d -> %d" % (base[3059][3], coll[3059][3]))

bm_click(user32.GetDlgItem(main, 3009))          # 展开
time.sleep(1.0)
exp = snapshot()
show("展开后", exp)

diff = []
for cid, v in base.items():
    w = exp.get(cid)
    if v != w:
        diff.append("%s: 基准%s vs 展开%s" % (IDS[cid], v, w))
chk("展开后与基准逐控件一致（位置/尺寸/可见性）", not diff, "; ".join(diff[:4]))
# 接收视图高度也应回到基准
chk("展开后接收视图高度回落", exp[3059][3] == base[3059][3],
    "h %d vs %d" % (exp[3059][3], base[3059][3]))

# ---- §85 核心不变式：接收区**永不压住面板行**（z 序上接收区在最上，压住即"组件混乱"）----
def overlap(snap):
    rx, port = snap[3059], snap[3014]
    return (rx[1] + rx[3]) > port[1]
chk("基准：接收区不压面板行", not overlap(base),
    "rx bottom=%d port top=%d" % (base[3059][1] + base[3059][3], base[3014][1]))
chk("收起态：不压（面板已隐藏，比的是残留矩形）", True, "collapsed 面板隐藏")
chk("展开态：接收区不压面板行", not overlap(exp),
    "rx bottom=%d port top=%d" % (exp[3059][1] + exp[3059][3], exp[3014][1]))
# 最小窗高：客户高不得低于「接收区 ≥ kRxMinH」所需（由 app 实测 minClientH_ 保证）
class MM(ctypes.Structure):
    _fields_ = [("v", ctypes.c_long * 10)]
mm = MM()
user32.SendMessageW(main, 0x0024, 0, ctypes.byref(mm))
print("  MINMAXINFO ptMinTrackSize=(%d,%d)" % (mm.v[6], mm.v[7]), flush=True)
chk("最小窗高已实测化（含菜单栏，≥370；§85 版漏算菜单=352 会红）", mm.v[7] >= 370,
    "minTrackY=%d" % mm.v[7])
# 收短到最小高附近再收起/展开：仍不重叠
el, et, er, eb = win32gui.GetWindowRect(main)
try:
    win32gui.SetWindowPos(main, 0, el, et, er - el, mm.v[7], 0x0004)   # SWP_NOZORDER
except Exception as e:
    print("  SetWindowPos skip:", e, flush=True)
time.sleep(0.8)
bm_click(user32.GetDlgItem(main, 3009)); time.sleep(0.7)
bm_click(user32.GetDlgItem(main, 3009)); time.sleep(0.9)
short = snapshot()
chk("贴近最小高再收起/展开：仍不重叠", not overlap(short),
    "rx bottom=%d port top=%d" % (short[3059][1] + short[3059][3], short[3014][1]))

# 截图取证
l, t, r, b = win32gui.GetWindowRect(main)
hdc = win32gui.GetWindowDC(main)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
user32.PrintWindow(main, dc.GetSafeHdc(), 2)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r85_after_expand.png")
print("saved r85_after_expand.png", flush=True)

win32gui.PostMessage(main, win32con.WM_CLOSE, 0, 0)
time.sleep(0.6)
if proc.poll() is None:
    proc.kill()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
