# -*- coding: utf-8 -*-
# verify_r79.py - docs/00 §79（用户三项）：
#  1) 发送框 Ctrl+Enter 发送后不再插入换行：根因=**Ctrl 修饰下 Enter 派生的是 LF(0x0A)**
#     而非 CR（§75 只吞 '\r' 故用户仍见回车）。断言：Ctrl 按住时 '\n'/'\r' 都被吞（文本不变）、
#     松开 Ctrl 时 '\r' 照常插入（守卫是条件性的）、真实 Ctrl+Enter 能发送且文本无新增换行。
#  2) 帮助→检查更新（id 2400）存在；About 含开发者/仓库/MIT；exe 内含发布页 URL（应用不联网）。
#  3) 左列右缘左移 2 英文字符：发送按钮 + 各"吃余量"控件右缘 = R_new（296-2ch，ch≈7px）；
#     按钮上方串口/网络两区所有控件右边界对齐按钮右缘；中列不动（HEX显示 x 不变）。
import os, time, subprocess, ctypes, shutil
import win32gui, win32con, win32process, win32api
user32 = ctypes.windll.user32
res = []
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

def rect(cid_or_hwnd, main=None):
    h = cid_or_hwnd if main is None else user32.GetDlgItem(main, cid_or_hwnd)
    l, t, r, b = win32gui.GetWindowRect(h)
    return (l, r)

# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79")
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)
ml, mt, mr, mb = win32gui.GetWindowRect(main)
def rr(cid):
    l, r = rect(cid, main)
    return (l - ml, r - ml)

# ---------------- 3) 左列右缘左移 2 英文字符 ----------------
sendL, sendR = rr(3058)
chk("§79-3 发送按钮右缘左移 2 英文字符（10..20px）", 276 <= sendR <= 286, "sendR=%d" % sendR)
# 串口区（隐藏也在布局，位置有效）+ 网络区（当前可见）：右缘全部 == 按钮右缘
rows = {3015: "刷新", 3019: "波特率", 3025: "校验位", 3029: "RTS", 3036: "BREAK",
        3065: "远端端口", 3067: "断开", 3070: "刷新网卡", 3073: "本地端口"}
bad = []
for cid, nm in rows.items():
    l, r = rr(cid)
    if r != sendR:
        bad.append("%s(id=%d) r=%d" % (nm, cid, r))
chk("§79-3 按钮上方控件右边界对齐按钮右缘（串口+网络 9 个）", not bad,
    "sendR=%d bad=%s" % (sendR, bad))
# 左缘不动 + 中列不动（HEX显示 3037 x = lx+leftW+colGap = 8+6+282+10 = 306）
chk("§79-3 按钮左缘不动", sendL == 14, "sendL=%d" % sendL)
hexL, hexR = rr(3037)
# §80 起中列锚在按钮右缘 + 列间距（此前固定 306）；此处断言锚定关系，非固定坐标
chk("§79-3 中列锚在按钮右缘 + colGap", hexL == sendR + 10, "hex=%d sendR=%d" % (hexL, sendR))

# ---------------- 1) Ctrl+Enter 不插入换行 ----------------
# 前置：真实 Ctrl 键态必须**投递到被测进程的队列**（keybd_event 只对前台线程生效）——
# 故本组与"真实 Ctrl+Enter"共用同一前台守卫；夺不到前台则整组 NOTE + SKIP（否则会把
# "没前台"误判成"没吞"或"没发送"——首版就吃过这个假 FAIL）。
def focus_app_edit(hwnd_main, hwnd_edit, tries=8):
    for _ in range(tries):
        user32.AllowSetForegroundWindow(-1)
        cur = ctypes.windll.kernel32.GetCurrentThreadId()
        appid = win32process.GetWindowThreadProcessId(hwnd_main)[0]
        user32.AttachThreadInput(cur, appid, True)
        user32.SetForegroundWindow(hwnd_main)
        user32.SetFocus(hwnd_edit)
        user32.AttachThreadInput(cur, appid, False)
        time.sleep(0.35)
        if user32.GetForegroundWindow() == hwnd_main and user32.GetFocus() == hwnd_edit:
            return True
    return False

es = user32.GetDlgItem(main, 3057)
wset(es, "AB")
time.sleep(0.3)
base = wtext(es)
focused = focus_app_edit(main, es)
print("  [focus] fg==main && focus==es: %s" % focused, flush=True)
if focused:
    # 按键态已投递：按住真实 Ctrl → 送 WM_CHAR LF/CR（§79 根因：Ctrl+Enter 派生 LF）
    user32.keybd_event(0x11, 0, 0, 0)
    time.sleep(0.25)
    user32.PostMessageW(es, win32con.WM_CHAR, 0x0A, 0)   # LF
    time.sleep(0.2)
    user32.PostMessageW(es, win32con.WM_CHAR, 0x0D, 0)   # CR
    time.sleep(0.3)
    after_ctrl = wtext(es)
    chk("§79-1 Ctrl 按住时 WM_CHAR LF/CR 全被吞（文本不变）", after_ctrl == base,
        "base=%r after=%r" % (base, after_ctrl))
    user32.keybd_event(0x11, 0, 2, 0)                    # VK_CONTROL up
    time.sleep(0.5)
    after_plain = base
    for _ in range(3):                                   # 键态切换有延迟：重试到插入成功
        user32.PostMessageW(es, win32con.WM_CHAR, 0x0D, 0)
        time.sleep(0.35)
        after_plain = wtext(es)
        if len(after_plain) > len(base):
            break
    chk("§79-1 松开 Ctrl 后 CR 正常插入（守卫是条件性的）", len(after_plain) > len(base),
        "len %d -> %d" % (len(base), len(after_plain)))
else:
    skip("§79-1 Ctrl 键态用例整组（前台/焦点未夺到，keybd_event 只对前台生效；"
         "代码级判据：WM_CHAR 分支 wp==CR 或 LF 且 Ctrl 按下则 return 0——留人工复核）")
wset(es, "AB")
time.sleep(0.2)

# 真实 Ctrl+Enter：应发送且文本不新增换行（回环 + 转存日志取证）
win32api.PostMessage(main, win32con.WM_COMMAND, 2222, 0)     # 回环演示
time.sleep(1.5)
rxfile = user32.GetDlgItem(main, 3039)
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) != 1:
    bm_click(rxfile)                                          # 接收数据到文件
    time.sleep(0.6)
import glob
before_logs = set(glob.glob(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79\SNCom-Log-*.txt"))
wset(es, "CTRLENTER79")
time.sleep(0.3)
# （前台守卫复用上面 §79-1 段落里已执行的 focused）
txt_before = wtext(es)
if focused:
    user32.keybd_event(0x11, 0, 0, 0)                         # Ctrl down
    time.sleep(0.15)
    user32.keybd_event(0x0D, 0, 0, 0); time.sleep(0.1)        # Enter down
    user32.keybd_event(0x0D, 0, 2, 0); time.sleep(0.1)        # Enter up
    user32.keybd_event(0x11, 0, 2, 0)                         # Ctrl up
    time.sleep(1.5)
else:
    print("NOTE §79-1: 前台/焦点未夺到（合成输入本环境随机失效）——真实按键分支跳过，"
          "留人工复核", flush=True)
txt_after = wtext(es)
if focused:
    chk("§79-1 真实 Ctrl+Enter 后发送框无新增换行", txt_after == txt_before,
        "before=%r after=%r" % (txt_before, txt_after))
new_logs = sorted(set(glob.glob(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79\SNCom-Log-*.txt")) - before_logs)
data = b""
for p in new_logs[-1:]:
    with open(p, "rb") as f:
        data = f.read()
if focused:
    chk("§79-1 真实 Ctrl+Enter 确实发送（回环转存日志含载荷）", b"CTRLENTER79" in data,
        "log=%s len=%d" % (os.path.basename(new_logs[-1]) if new_logs else "-", len(data)))
if user32.SendMessageW(rxfile, 0x00F0, 0, 0) == 1:
    bm_click(rxfile)                                          # 关转存
    time.sleep(0.4)

# ---------------- 2) 检查更新 + 关于 ----------------
mb_ = user32.GetMenu(main)
helpm = user32.GetSubMenu(mb_, 6)                             # 帮助
n = user32.GetMenuItemCount(helpm)
ids = [user32.GetMenuItemID(helpm, i) for i in range(n)]
chk("§79-2 帮助菜单含「检查更新…」（id 2400）+ 关于", n == 2 and ids[0] == 2400,
    "n=%d ids=%s" % (n, ids))
# exe 内发布页 URL（应用内不联网——只 ShellExecute 打开网页）
exe = open(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r79\sncom.exe", "rb").read()
url = "https://github.com/smartmx/SNCom/releases/latest".encode("utf-16-le")
chk("§79-2 exe 内含发布页 URL（UTF-16 字面量）", url in exe, "found=%s" % (url in exe))
# 关于框内容
about_id = ids[1]
win32api.PostMessage(main, win32con.WM_COMMAND, about_id, 0)
dlg = 0
for _ in range(25):
    out = []
    def cbcls(h, l):
        if win32gui.GetClassName(h) == "#32770" and win32process.GetWindowThreadProcessId(h)[1] == proc.pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cbcls, None)
    if out:
        dlg = out[0]; break
    time.sleep(0.2)
if dlg:
    txt = ""
    def cbst(h, l):
        global txt
        if win32gui.GetClassName(h) == "Static":
            txt += win32gui.GetWindowText(h) + "\n"
        return True
    user32.EnumChildWindows(dlg, ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(cbst), 0)
    ok = ("CoderNotCutie" in txt) and ("github.com/smartmx/SNCom" in txt) and ("MIT License" in txt)
    chk("§79-2 关于框含 开发者/仓库/MIT License", ok, "dialog=%r text=%r" % (win32gui.GetWindowText(dlg), txt[:160]))
    win32api.PostMessage(dlg, win32con.WM_COMMAND, 1, 0)      # IDOK
    time.sleep(0.5)
else:
    chk("§79-2 关于框已弹出", False, "dialog not found")

proc.terminate()
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
if fails:
    print("FAIL: " + "; ".join(fails))
