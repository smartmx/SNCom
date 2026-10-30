# -*- coding: utf-8 -*-
# verify_r80.py - docs/00 §80（用户三项，全部以「1 个英文字符宽 = ch」为单位，ch 由数据位
# 收窄量反推，避免探针硬编码字体度量）：
#  1) 数据位/停止位选择窗各收窄 1ch、校验位选择窗加宽 2ch → 该行总宽不变（右缘仍==按钮右缘）
#  2) 发送框与其上方整块（中列 + 右列）左移 2ch：发送框/打开文件锚点/首行控件 x 均 306-2ch
#  3) 超时时间框与「第 N 字节」框各收窄 2ch（48-2ch）
import os, time, subprocess, ctypes, shutil
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

# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r80"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r80\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r80\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r80")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
ml, mt, mr, mb = win32gui.GetWindowRect(main)
def rr(cid):
    h = user32.GetDlgItem(main, cid)
    l, t, r, b = win32gui.GetWindowRect(h)
    return (l - ml, r - ml, r - l)

# ---------------- 1) 第 3 行（数据位/停止位/校验位） ----------------
dL, dR, wData = rr(3021)
sL, sR, wStop = rr(3023)
pL, pR, wPar = rr(3025)
sendL, sendR, wSend = rr(3058)
ch = 42 - wData                       # §80 前数据位固定 42 → 收窄量即 1 个英文字符宽
chk("§80-1 数据位收窄 1 英文字符（42-ch）", 5 <= ch <= 10, "w=%d ch=%d" % (wData, ch))
chk("§80-1 停止位收窄 1 英文字符（48-ch）", 48 - wStop == ch, "w=%d want=%d" % (wStop, 48 - ch))
# §135 收口（外壳字体换代）：原判据「wPar-50 == 2ch」的 50 是旧字体下 (左列实测宽-固定件) 的
# 残留量；§80 公式 cmbW(无校验,64)+2ch 经 eat() 填充到左列右缘，实际宽随左列实测宽走——
# 换宋体后左列宽 -2px（实测 w=64、旧式 want=62）。改为字体无关语义：校验位窗为三窗最宽且比
# 停止位至少宽 2ch（§80「加宽校验位」意图）；「行总宽不变」由下方右缘==发送按钮右缘判据承担。
chk("§80-1 校验位窗为三窗最宽（≥停止位+2ch）", wPar > wData and wPar - wStop >= 2 * ch,
    "par=%d data=%d stop=%d ch=%d" % (wPar, wData, wStop, ch))
chk("§80-1 本行右缘未动（== 发送按钮右缘）", pR == sendR, "parityR=%d sendR=%d" % (pR, sendR))
chk("§80-1 本行左缘未动（数据位 x=54）", dL == 54, "x=%d" % dL)

# ---------------- 2) 发送框与上方整块左移 2ch ----------------
esL, esR, wEs = rr(3057)
fileL, _, _ = rr(3004)                # 打开文件（工具行锚点，§49 与中列首控件对齐）
hexL, _, _ = rr(3037)                 # HEX显示（中列首行）
midL, _, _ = rr(3040)                 # 加时间戳和分包显示（中列第二行）
r1L, _, _ = rr(3045)                  # HEX发送（右列第一行）
chk("§80-2 发送框左缘左移 2ch（306-2ch）", esL == 306 - 2 * ch, "x=%d want=%d" % (esL, 306 - 2 * ch))
chk("§80-2 中列首行跟随（HEX显示同 x）", hexL == esL, "hex=%d box=%d" % (hexL, esL))
chk("§80-2 中列第二行跟随（分包同 x）", midL == esL, "split=%d box=%d" % (midL, esL))
chk("§80-2 工具行锚点跟随（打开文件同 x，§49 不变量）", fileL == esL, "file=%d box=%d" % (fileL, esL))
# 右缘贴窗右缘（与默认窗宽无关，按客户区实测推）：右缘 = 边框 + 客户宽 - kMarginX(6)
class RC(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
rc = RC()
user32.GetClientRect(main, ctypes.byref(rc))
border = (mr - ml - (rc.right - rc.left)) // 2
wantR = border + (rc.right - rc.left) - 6
chk("§80-2 发送框右缘仍贴窗右缘（随左缘加宽 2ch）", abs(esR - wantR) <= 2,
    "r=%d want=%d（客户宽 %d）" % (esR, wantR, rc.right - rc.left))
chk("§80-2 右列整块亦左移（HEX发送 x < 626）", r1L < 596, "x=%d" % r1L)

# ---------------- 3) 超时框 / 第 N 字节框各收窄 2ch ----------------
_, _, wMs = rr(3043)
_, _, wStart = rr(3051)
chk("§80-3 超时时间框收窄 2ch（48-2ch）", 48 - wMs == 2 * ch, "w=%d want=%d" % (wMs, 48 - 2 * ch))
chk("§80-3 第 N 字节框收窄 2ch（48-2ch）", 48 - wStart == 2 * ch, "w=%d want=%d" % (wStart, 48 - 2 * ch))

proc.terminate()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
