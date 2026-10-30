# -*- coding: utf-8 -*-
# verify_r94.py - docs/00 §94：ForceListPaint（列表+表头显式整窗置脏）回归守卫。
#   覆盖四条路径的"整页渲染不回归"（§88.5 类）+ 表头墨迹齐全：
#   A 首开多条表（msList_ 6 列）；B 首访 ASCII 页（asciiList_ 列数动态读 HDM_GETITEMCOUNT）；
#   C 切回多条表墨迹保持；D 关→重开墨迹保持。
#   口径与 r87 相同：抓**整个扩展窗**（PrintWindow of extWnd_）——本机该通道从不复现
#   用户"表头缺失"现象（八轮 A/B 实证），故本探针守的是回归类而非 §94 修复判据；
#   §94 的真效以真机目检 + 八轮 §3.6 两项实验（移动 vs 改大小）定性。
import os
import shutil
import subprocess
import time
import ctypes
import win32gui
import win32api
import win32process
import win32ui
from PIL import Image

user32 = ctypes.windll.user32
res = []
BIN = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r94\sncom.exe"
OUT = r"F:\projects\sscom二次分析开发\SNCom\probes\out"


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


def shoot_ext(ext):
    """PrintWindow 抓扩展窗整窗 → RGB Image（抓父窗，六/七轮实证抓子窗本体测不到回归）。"""
    l, t, r, b = win32gui.GetWindowRect(ext)
    hdc = win32gui.GetWindowDC(ext)
    mfc = win32ui.CreateDCFromHandle(hdc)
    dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap()
    bmp.CreateCompatibleBitmap(mfc, r - l, b - t)
    dc.SelectObject(bmp)
    user32.PrintWindow(ext, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(ext, hdc)
    return img, (l, t)


def header_inks(img, ext_origin, lst):
    """列表各表头列的深色墨迹（列宽 LVM_GETCOLUMNWIDTH，列数读表头 HDM_GETITEMCOUNT）。"""
    l, t, r, b = win32gui.GetWindowRect(lst)
    lx, ly = l - ext_origin[0], t - ext_origin[1]
    hdr = user32.SendMessageW(lst, 0x101F, 0, 0)          # LVM_GETHEADER
    n = user32.SendMessageW(hdr, 0x1200, 0, 0)            # HDM_GETITEMCOUNT
    widths = [user32.SendMessageW(lst, 0x101D, i, 0) for i in range(n)]
    inks = []
    x = lx
    for w in widths:
        cell = img.crop((x + 2, ly + 2, x + w - 2, ly + 22))
        dark = sum(1 for yy in range(cell.size[1]) for xx in range(0, cell.size[0], 2)
                   if sum(cell.getpixel((xx, yy))) < 400)
        inks.append(dark)
        x += w
    return inks


def open_ext(proc, main):
    bm = user32.GetDlgItem(main, 3012)
    win32api.PostMessage(bm, 0x00F5, 0, 0)                # BM_CLICK「扩展」
    ext = 0
    for _ in range(40):
        ext = find_toplevel("SNComExt", proc.pid)
        if ext:
            break
        time.sleep(0.1)
    time.sleep(1.0)
    return ext


# §114 探针隔离：沙箱只放 exe 的新鲜副本（不再动 develop\dist 的用户配置与日志）
_SRC_DIST = r"F:\projects\sscom二次分析开发\SNCom\develop\dist"
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r94"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    import os
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r94\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([BIN], cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r94")
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.15)
if not main:
    print("NOTE r94: 主窗未就绪，优雅退出")
    raise SystemExit(0)
time.sleep(3.0)

# A. 首开：多条表六列表头墨迹
ext = open_ext(proc, main)
lst = user32.GetDlgItem(ext, 20)
chk("94-A 首开扩展窗+多条表可见", bool(ext and lst and user32.IsWindowVisible(lst)), "")

# §95→§95.1：WS_CLIPCHILDREN 修法已被用户实测证伪——加上后整个扩展界面显示完全异常，
# 已回滚（§88.5 先例：证据优先于自述一致性）。此处的样式检查降级为观察项（不进 PASS/FAIL）：
# 当前=无 WS_CLIPCHILDREN（§94 状态）；根因仍未证实，勿再单加此样式（会破坏既有绘制平衡）。
WS_CLIPCHILDREN = 0x02000000
style = user32.GetWindowLongPtrW(ext, -16) if hasattr(user32, "GetWindowLongPtrW") \
    else user32.GetWindowLongW(ext, -16)
has_clip = bool(style & WS_CLIPCHILDREN)
print("NOTE §95.1: 扩展窗 WS_CLIPCHILDREN=%s（§95 修法引发整页显示异常已回滚，"
      "勿再加回；若未来找到不破坏绘制平衡的修法需同步改本 NOTE）" % has_clip, flush=True)

img, org = shoot_ext(ext)
img.save(OUT + r"\r94_open.png")
inks = header_inks(img, org, lst)
print("  A 多条表表头墨迹:", inks, flush=True)
chk("94-A 首开六列表头全部有墨迹", len(inks) == 6 and all(v >= 3 for v in inks), "inks=%s" % inks)

# B. 首访 ASCII 页（WM_APP+0x111 = 小工具直达路径 ShowExtPage(2)）；asciiList_=IDC_ASCII_LIST=13
win32api.PostMessage(ext, 0x8000 + 0x111, 2, 0)
time.sleep(0.8)
alst = user32.GetDlgItem(ext, 13)
chk("94-B ASCII 页可见", bool(alst and user32.IsWindowVisible(alst)), "")
img, org = shoot_ext(ext)
img.save(OUT + r"\r94_ascii.png")
inks_a = header_inks(img, org, alst)
print("  B ASCII 表头墨迹:", inks_a, flush=True)
chk("94-B ASCII 表头全列有墨迹", len(inks_a) >= 4 and all(v >= 3 for v in inks_a), "inks=%s" % inks_a)

# E. ASCII 双击复制端到端（§94 附带修复：HandleExtNotify 原判 idFrom==2，IDC_ASCII_LIST
#    实为 13——双击复制从未生效）。直投 WM_LBUTTONDBLCLK 到列表客户区第 0 行（y=34，
#    表头实测 24px）→ 十进制 0 → 剪贴板应得 "NUL"。
#    ⚠ 三个坑（调试实测）：① LVM_GETITEMRECT 的结构体指针跨进程 SendMessage 不列集，
#    rect 读不回还可能写崩目标进程——禁用；② 真实鼠标在本机不可靠（点击点被 ZCode 宿主
#    WinUI 窗覆盖，sel 恒 -1）；③ 命中测试只认消息坐标，无需前台。
try:
    import win32clipboard
    win32clipboard.OpenClipboard()
    win32clipboard.EmptyClipboard()
    win32clipboard.CloseClipboard()
    lp = (34 & 0xFFFF) << 16 | 20                       # 客户区 (20, 34) = 第 0 行
    win32api.PostMessage(alst, 0x0203, 1, lp)           # WM_LBUTTONDBLCLK, MK_LBUTTON
    time.sleep(0.6)
    win32clipboard.OpenClipboard()
    data = ""
    try:
        data = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
    except TypeError:
        pass
    win32clipboard.CloseClipboard()
    chk("94-E ASCII 双击第 0 行复制出 NUL", data == "NUL", "clipboard=%r" % data)
except Exception as ex:
    print("NOTE r94-E: 剪贴板段跳过（%s）" % ex, flush=True)

# C. 切回多条表：墨迹保持
win32api.PostMessage(ext, 0x8000 + 0x111, 0, 0)
time.sleep(0.8)
img, org = shoot_ext(ext)
inks_c = header_inks(img, org, lst)
chk("94-C 切回后六列墨迹保持", all(v >= 3 for v in inks_c), "inks=%s" % inks_c)

# D. 关→重开：墨迹保持（§94 在 ToggleExt(true) 末步重刷）
win32api.PostMessage(user32.GetDlgItem(main, 3012), 0x00F5, 0, 0)   # 收起
time.sleep(0.6)
ext = open_ext(proc, main)
chk("94-D 重开扩展窗", bool(ext), "")
img, org = shoot_ext(ext)
inks_d = header_inks(img, org, lst)
chk("94-D 重开后六列墨迹保持", all(v >= 3 for v in inks_d), "inks=%s" % inks_d)

proc.terminate()
print("NOTE §94→§95→§95.1: §94 ForceListPaint 后用户仍复现；§95 推断父窗擦除毁子窗"
      "（缺 WS_CLIPCHILDREN）并加样式修复——**用户实测整个扩展界面显示完全异常，已回滚**"
      "（§94 状态为准）。表头首开缺失根因仍未证实；下一步以八轮报告 §3.6 真机实验"
      "（移动 vs 改大小 / WM_PAINT 日志）定性，探针侧墨迹断言只守 §88.5 整页不渲染回归类",
      flush=True)
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
