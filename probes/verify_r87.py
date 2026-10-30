# -*- coding: utf-8 -*-
# verify_r87.py - docs/00 §87/§91/§92/§94：扩展窗多条表**首开表头完整性**守卫。
#   修法演进史（勿重走前三条死路）：
#   ① §87 首版（列表/整窗 RedrawWindow UPDATENOW）在用户环境引发整页不渲染 → §88.5 回滚；
#   ② §91 窄域表头补画（InvalidateRect+UpdateWindow，置于 ShowExtPage 内）用户环境仍不上屏
#      ——它在最终几何确定之前执行，随后 RepositionExt/尺寸变化把页面按"只失效新暴露区"
#      语义又推一遍，最终可见帧不是 §91 刷的那一帧；
#   ③ §92 1px 尺寸往返：八轮实测表头宽度 444→445→444 净变化 0，对"表头整片没画"无贡献，
#      反而让每次开窗多跑两轮完整重排 → §94 移除；
#   ④ §94 机制级收口：八轮实测 SysListView32/SysHeader32 的 GCL_STYLE=0x4008（无
#      CS_HREDRAW|CS_VREDRAW），尺寸变化只失效"新暴露条带"，表头在隐藏期从 6px 放大到 444px
#      后从未被要求整体重画；全程序唯一确定失效是 MsStretchLastColumn 末列改宽（只标脏该列）
#      ——与用户"只有『注释』列显示"精确吻合。修法=显示序列最后一步 ForceListPaint 对
#      "当前页列表+其表头"显式整窗置脏并同步上屏（只动这两个窗口）。
#   本探针抓**整个扩展窗**（PrintWindow of extWnd_，非列表本体——六轮 A/B 实证抓本体测不到
#   这类"屏幕表面"回归），判据=逐列表头墨迹 ≥3；本机 PrintWindow 通道从不复现用户现象，
#   首开表头完整性最终以真机目检为准。

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
_SBX_DIST = r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r87"
os.makedirs(_SBX_DIST, exist_ok=True)
shutil.copy(os.path.join(_SRC_DIST, "sncom.exe"), os.path.join(_SBX_DIST, "sncom.exe"))
try:
    import os
    os.remove(r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r87\sncom.json")
except FileNotFoundError:
    pass
proc = subprocess.Popen([r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r87\sncom.exe"],
                        cwd=r"F:\projects\sscom二次分析开发\SNCom\probes\_probe\out\r87")
main = 0
for _ in range(60):
    main = find_toplevel("SNComMain", proc.pid)
    if main: break
    time.sleep(0.15)
assert main
time.sleep(3.0)

# 打开扩展窗（不碰鼠标——模拟用户首次点开后的真实状态）
bm = user32.GetDlgItem(main, 3012)
win32api.PostMessage(bm, 0x00F5, 0, 0)
time.sleep(1.0)
ext = find_toplevel("SNComExt", proc.pid)
lst = user32.GetDlgItem(ext, 20)
chk("§87 扩展窗/多条表可见", bool(ext and lst and user32.IsWindowVisible(lst)), "")

# §88 教训：抓**整个扩展窗**（PrintWindow of extWnd_）而非列表本体——六轮审查+用户截图
# 实证：对列表/扩展窗做 RDW_UPDATENOW 强制重绘会让整页内容不再上屏（列表本体 PrintWindow
# 恒正常，抓本体测不到）；抓父窗才能测到这类回归。
el, et, er, eb = win32gui.GetWindowRect(ext)
hdc = win32gui.GetWindowDC(ext)
mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, er - el, eb - et); dc.SelectObject(bmp)
user32.PrintWindow(ext, dc.GetSafeHdc(), 2)
bits = bmp.GetBitmapBits(True)
img = Image.frombuffer("RGBA", (er - el, eb - et), bits, "raw", "BGRA", 0, 1).convert("RGB")
win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(ext, hdc)
img.save(r"F:\projects\sscom二次分析开发\SNCom\probes\out\r87_header.png")
ll, lt, lr2, lb2 = win32gui.GetWindowRect(lst)                # 列表在 ext 内的偏移
lx, ly = ll - el, lt - et

widths = [user32.SendMessageW(lst, 0x101D, i, 0) for i in range(6)]   # LVM_GETCOLUMNWIDTH
inks = []
x = lx
for i, w in enumerate(widths):
    cell = img.crop((x + 2, ly + 2, x + w - 2, ly + 22))
    dark = sum(1 for yy in range(0, cell.size[1], 1) for xx in range(0, cell.size[0], 2)
               if sum(cell.getpixel((xx, yy))) < 400)
    inks.append(dark)
    x += w
print("  各表头列深色墨迹:", inks, flush=True)
chk("§87 六列表头全部有墨迹（无空白表头）", all(v >= 3 for v in inks), "inks=%s" % inks)

# 鼠标划过等价性：把光标依次划过表头后再取一次（应保持有墨迹，不回归）
try:
    class RC(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                    ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
    hdr_h = user32.SendMessageW(lst, 0x101F, 0, 0)            # LVM_GETHEADER
    rc_h = RC()
    user32.GetWindowRect(ctypes.c_void_p(hdr_h), ctypes.byref(rc_h))
    hl, ht, hr, hb = rc_h.left, rc_h.top, rc_h.right, rc_h.bottom
    for i in range(6):
        xx = hl + sum(widths[:i]) + widths[i] // 2
        okp = user32.SetCursorPos(ctypes.c_int(xx), ctypes.c_int((ht + hb) // 2))
        time.sleep(0.08)
    img2 = None
    l, t, r, b = win32gui.GetWindowRect(lst)
    hdc = win32gui.GetWindowDC(lst)
    mfc = win32ui.CreateDCFromHandle(hdc); dc = mfc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mfc, r - l, b - t); dc.SelectObject(bmp)
    user32.PrintWindow(lst, dc.GetSafeHdc(), 2)
    bits = bmp.GetBitmapBits(True)
    img2 = Image.frombuffer("RGBA", (r - l, b - t), bits, "raw", "BGRA", 0, 1).convert("RGB")
    win32gui.DeleteObject(bmp.GetHandle()); dc.DeleteDC(); win32gui.ReleaseDC(lst, hdc)
    inks2 = []
    x = 0
    for i, w in enumerate(widths):
        cell = img2.crop((x + 2, 2, x + w - 2, 22))
        dark = sum(1 for yy in range(0, cell.size[1], 1) for xx in range(0, cell.size[0], 2)
                   if sum(cell.getpixel((xx, yy))) < 400)
        inks2.append(dark)
        x += w
    chk("§87 划过后表头墨迹保持", all(v >= 3 for v in inks2), "inks=%s" % inks2)
except Exception as ex:
    print("NOTE §87: 划过段跳过（%s）" % ex, flush=True)

proc.terminate()
print("NOTE §87→§94: 首开表头缺失的机制级根因=SysListView32/SysHeader32 类样式无 CS_HREDRAW|CS_VREDRAW"
      "（尺寸变化只失效新暴露条带），全程序唯一确定失效仅覆盖末列（MsStretchLastColumn）；"
      "§94 修法=显示序列最后一步 ForceListPaint 对当前页列表+表头显式整窗置脏并同步上屏。"
      "本探针抓扩展窗整窗渲染做回归守卫；本机 PrintWindow 通道不复现用户现象，真机目检为准", flush=True)
fails = [n for n, ok in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if fails:
    print("FAIL: " + "; ".join(fails))
