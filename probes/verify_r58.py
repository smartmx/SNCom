# -*- coding: utf-8 -*-
# verify_r58.py - docs/00 §58 + §66:
#  1) RX status part must NOT flash 「就绪」 on find toggle — sample SB_GETTEXTLENGTHW
#     of the RX part at ~20ms intervals right after an async find-button click.
#     (§66 段位前移：0=TX 1=RX 2=通知 3=版本；旧 bug 在 RX 段写「就绪」len2。)
#  2) §66: 连接段已删 → 4 段；0=TX、1=RX、无「已连接/未连接」文本。
#  3) version part width halved: visual screenshot + part sanity.
import os, time, subprocess, ctypes
import win32gui, win32con, win32process, win32api
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

sb = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
assert sb
# ⚠️ 跨进程只准用 SB_GETTEXTLENGTHW（0x040C，无指针出参）——SB_GETPARTS/SB_GETTEXTW
# 带指针跨进程会在目标进程内写非法地址（应用直接崩，r59 注释同款结论）。
def part_len(part):
    return user32.SendMessageW(sb, 0x0400 + 12, part, 0) & 0xFFFF   # SB_GETTEXTLENGTHW

# ---- §66: 4 段、0=TX、1=RX（长度结构证明；「未连接」len3 会让 part0 变 3，13=TX 计数）----
lens = [part_len(i) for i in range(6)]
chk("§66 status parts == 4 (version@3 len6, 越界 0)", lens[3] == 6 and lens[4] == 0 and lens[5] == 0,
    "lens=%s" % lens)
chk("§66 part0 = TX counter (len13, 未连接=3)", lens[0] == 13, "len0=%s" % lens[0])
chk("§66 part1 = RX counter (len13)", lens[1] == 13, "len1=%s" % lens[1])

# settle: RX part (1) should be "RX: 0 (0 B/s)" len 13 at rest
lens_at_rest = set()
for _ in range(6):
    lens_at_rest.add(part_len(1))
    time.sleep(0.3)
chk("RX part at rest len == 13 (RX: 0 (0 B/s))", lens_at_rest == {13}, "lens=%s" % lens_at_rest)

# click find asynchronously, sample RX part length every ~15ms for 0.8s
win32api.PostMessage(win32gui.GetDlgItem(main, 3001), 0x00F5, 0, 0)   # BM_CLICK 查找
bad = []
t0 = time.time()
while time.time() - t0 < 0.8:
    L = part_len(1)
    if L != 13:
        bad.append(L)
    time.sleep(0.015)
chk("find toggle: RX part never flashes 就绪(len2)", not any(x == 2 for x in bad),
    "bad lens=%s" % bad[:8])
chk("RX part stayed RX text the whole window", all(x == 13 for x in bad) or not bad,
    "deviations=%s" % bad[:8])

# toggle back, same check
win32api.PostMessage(win32gui.GetDlgItem(main, 3001), 0x00F5, 0, 0)
bad2 = []
t0 = time.time()
while time.time() - t0 < 0.8:
    L = part_len(1)
    if L != 13:
        bad2.append(L)
    time.sleep(0.015)
chk("find close: RX part stable (no len-2 就绪)", not any(x == 2 for x in bad2),
    "bad lens=%s" % bad2[:8])

# ---- 3) version part halved: capture status bar screenshot for visual check ----
l, t, r, b = win32gui.GetWindowRect(main)
import win32ui
from PIL import Image
hdc = win32gui.GetWindowDC(main)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
if not user32.PrintWindow(main, dc.GetSafeHdc(), 2):
    dc.BitBlt((0, 0), (r - l, b - t), mfc, (0, 0), win32con.SRCCOPY)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
img.crop((0, img.size[1] - 34, img.size[0], img.size[1])).save(
    os.path.join(HERE, "out", "r58_statusbar.png"))
print("saved r58_statusbar.png", flush=True)
proc.terminate()

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
