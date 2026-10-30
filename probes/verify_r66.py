# -*- coding: utf-8 -*-
# verify_r66.py - docs/00 §66（用户两项）：
#  1) 状态栏删「已连接/未连接」段（4 段：0=TX 1=RX 2=通知 3=版本），宽度平分给 TX/RX；
#  2) 串口被占用 → 打开失败进重连等待 → LED 变黄；取消 → 灰；释放端口重开 → 绿。
# 占用模拟：本进程先 CreateFileW 独占打开真实 COM 口（串口不支持共享 → 应用打开=ACCESS_DENIED）。
# 注：所有落盘/进程路径为完整字面量内联（扫描器拦变量路径推导；换机需改字面量）。
import os, time, subprocess, ctypes, shutil
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

# ---- 独占占用一个真实 COM 口 ----
GENERIC_READWRITE = 0xC0000000
OPEN_EXISTING = 3
held = None
hold_name = None
for name in ("COM1", "COM3", "COM9", "COM10"):
    h = ctypes.windll.kernel32.CreateFileW("\\\\.\\%s" % name, GENERIC_READWRITE, 0,
                                           None, OPEN_EXISTING, 0, None)
    if h != -1 and h != 0xFFFFFFFFFFFFFFFF:
        held, hold_name = h, name
        break
if held is None:
    print("SKIP: 无可独占的 COM 口，LED 黄态段跳过（状态栏段仍验证）", flush=True)

# R23-P3-2 沙箱化：独立目录 + 最小配置（port=被占口）——不再读用户真实 sncom.json
#（旧写法 cwd=dist，用户配置 port 变更后探针独占 COM1 而应用按配置开空闲口，前提失效判红）；
# 端口选择也由配置承担——旧 0x0143 实为 CB_ADDSTRING（非 SELECTSTRING）且跨进程指针消息（规则 20）。
import json
SB = os.path.join(os.environ["TEMP"], "r66_sb")
shutil.rmtree(SB, ignore_errors=True)
os.makedirs(SB)
EXE_SRC = r"F:\projects\sscom二次分析开发\SNCom\develop\dist\sncom.exe"
EXE_DST = os.path.join(SB, "sncom.exe")
shutil.copyfile(EXE_SRC, EXE_DST)
cfg = {"win_w": 960, "win_h": 720, "port": hold_name or "COM1",
       "auto_reconnect": True, "open_on_start": False}
CFG_PATH = os.path.join(SB, "sncom.json")
open(CFG_PATH, "w", encoding="utf-8").write(json.dumps(cfg, ensure_ascii=False))
proc = subprocess.Popen([EXE_DST], cwd=SB)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.25)
assert main
time.sleep(3.0)

# 端口已由沙箱配置指定（port=被占口），无需 UI 选择

sb = win32gui.FindWindowEx(main, 0, "msctls_statusbar32", None)
assert sb
# ⚠️ 跨进程只准用 SB_GETTEXTLENGTHW（0x040C，无指针出参）——SB_GETPARTS/SB_GETTEXTW
# 带指针跨进程会在目标进程内写非法地址（应用直接崩，r59 注释同款结论）。
def part_len(part):
    return user32.SendMessageW(sb, 0x0400 + 12, part, 0) & 0xFFFF

# 段位结构证明（§66）：0=TX(13) 1=RX(13) 2=通知 3=版本(6)；越界段长度 0
lens = [part_len(i) for i in range(6)]
chk("§66-1 status parts == 4（连接段已删）", lens[3] == 6 and lens[4] == 0 and lens[5] == 0,
    "lens=%s" % lens)
chk("§66-1 part0=TX 计数（rest len13，非「未连接」len3）", lens[0] == 13, "len0=%s" % lens[0])
chk("§66-1 part1=RX 计数（rest len13）", lens[1] == 13, "len1=%s" % lens[1])
chk("§66-1 part3=版本 V0.0.1（len6）", lens[3] == 6, "len3=%s" % lens[3])

def capture():
    l, t, r, b = win32gui.GetWindowRect(main)
    hdc = win32gui.GetWindowDC(main)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    if not user32.PrintWindow(main, dc.GetSafeHdc(), 2):
        dc.BitBlt((0, 0), (r - l, b - t), main, (0, 0), win32con.SRCCOPY)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(main, hdc)
    return img, (l, t)

def led_rgb():
    img, (ml, mt) = capture()
    led = win32gui.GetDlgItem(main, 3016)               # IDC_LED_LINK
    rl, rt, rr, rb = win32gui.GetWindowRect(led)
    cx, cy = (rl + rr) // 2 - ml, (rt + rb) // 2 - mt
    rs = gs = bs = n = 0
    for dx in (-2, 0, 2):
        for dy in (-2, 0, 2):
            p = img.getpixel((cx + dx, cy + dy))
            rs += p[0]; gs += p[1]; bs += p[2]; n += 1
    return (rs // n, gs // n, bs // n)

def is_yellow(c): return c[0] > 200 and c[1] > 170 and c[2] < 130
def is_gray(c):   return abs(c[0]-c[1]) < 30 and abs(c[1]-c[2]) < 30 and 110 < c[0] < 200
def is_green(c):  return c[1] > 120 and c[0] < 110 and c[2] < 110

os.makedirs(r"F:\projects\sscom二次分析开发\SNCom\probes\out", exist_ok=True)

if held is not None:
    # ---- 2) 占用：打开失败 → 重连等待 → LED 黄 ----
    win32api.PostMessage(win32gui.GetDlgItem(main, 3017), 0x00F5, 0, 0)   # BM_CLICK 打开串口
    time.sleep(1.2)                                     # 打开失败即时 + EnterReconnect
    c = led_rgb()
    capture()[0].save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r66_led_yellow.png")
    chk("§66-2 端口被占用 → LED 黄（等待连接）", is_yellow(c), "rgb=%s" % (c,))
    # 打开按钮 = 取消（三态）
    btn_txt = win32gui.GetWindowText(win32gui.GetDlgItem(main, 3017))
    chk("§66-2 重连中按钮=「取消」", btn_txt == "取消", "text=%r" % btn_txt)
    # ---- 取消 → 灰 ----
    win32api.PostMessage(win32gui.GetDlgItem(main, 3017), 0x00F5, 0, 0)
    time.sleep(0.5)
    c = led_rgb()
    chk("§66-2 取消重连 → LED 灰", is_gray(c), "rgb=%s" % (c,))
    # ---- 释放端口 → 打开成功 → 绿 ----
    ctypes.windll.kernel32.CloseHandle(held)
    held = None
    time.sleep(0.3)
    win32api.PostMessage(win32gui.GetDlgItem(main, 3017), 0x00F5, 0, 0)
    time.sleep(1.5)
    c = led_rgb()
    capture()[0].save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r66_led_green.png")
    chk("§66-2 释放端口重开 → LED 绿（已连接）", is_green(c), "rgb=%s" % (c,))
    # 关闭 → 灰
    win32api.PostMessage(win32gui.GetDlgItem(main, 3017), 0x00F5, 0, 0)
    time.sleep(0.5)
    c = led_rgb()
    chk("§66-2 关闭串口 → LED 灰", is_gray(c), "rgb=%s" % (c,))
else:
    capture()[0].save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r66_led_gray.png")

proc.terminate()
shutil.rmtree(SB, ignore_errors=True)                    # 沙箱即弃（留存策略：_probe 瞬态）
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
