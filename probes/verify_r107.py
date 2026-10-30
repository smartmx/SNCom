# -*- coding: utf-8 -*-
# verify_r107.py — §107 小工具两枚外部程序回归（设备管理器 / 文件资源管理器）：
#   用户报「无效」——两者原被并进「小工具→扩展窗切页」区间分支，从未真正启动外部程序。
#   A 设备管理器：WM_COMMAND 2220 → 新出现 MMCMainFrame 窗口（标题含 设备管理/Device Manager）
#   B 文件资源管理器：WM_COMMAND 2221 → 新出现 CabinetWClass/ExploreWClass 窗口
#     （标题含 电脑/Computer——Win7 显示"我的电脑"、Win10+ 显示"此电脑"，同一 shell 命名空间）
#   判据=命令前后目标类窗口集合的**差集非空**（不依赖桌面原有窗口）；清理=关闭新增窗口，
#   MMC 若拒绝 WM_CLOSE（跨完整性）则按其进程 PID 精确 taskkill。
#   §108 加固（codex 第十五轮 P3-2）：devmgmt.msc 是**提权** mmc，非提权 shell 下
#   WM_CLOSE 与 taskkill 都可能被拒 → 清理失败不再静默：运行开始打印既有 mmc 残留、
#   运行结束把清不掉的 PID 汇总为 NOTE（请在【管理员】任务管理器结束；在提权终端
#   跑本探针可自动清理）。清理失败不判 FAIL（产品行为与断言无关）。
# 前置：沙箱 exe（probes/_probe/out/r107），不碰 dist 配置。
import ctypes
import json
import os
import shutil
import subprocess
import sys
import time

import win32gui
import win32process

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("SNCOM_EXE") or os.path.abspath(
    os.path.join(HERE, "..", "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "r107")
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

IDM_TOOL_DEV, IDM_TOOL_EXPLORER = 2220, 2221
WM_COMMAND, WM_CLOSE = 0x0111, 0x0010
MMC_CLASSES = ("MMCMainFrame",)
CAB_CLASSES = ("CabinetWClass", "ExploreWClass")
res = []
residue = []          # §108：清不掉的提权 mmc PID（收尾统一提示）


def snap(classes):
    out = []

    def cb(h, l):
        if win32gui.IsWindowVisible(h) and win32gui.GetClassName(h) in classes:
            out.append((h, win32gui.GetWindowText(h)))
        return True

    win32gui.EnumWindows(cb, None)
    return out


def find_toplevel(cls, pid):
    out = []

    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True

    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


def launch():
    shutil.rmtree(BIN, ignore_errors=True)
    os.makedirs(BIN)
    shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
    with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
        json.dump({"mode": "serial"}, f)
    p = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
    main = 0
    for _ in range(70):
        main = find_toplevel("SNComMain", p.pid)
        if main:
            break
        time.sleep(0.15)
    time.sleep(3.0)
    return p, main


def close_app(p, main):
    if main:
        win32gui.PostMessage(main, WM_CLOSE, 0, 0)
    for _ in range(30):
        if p.poll() is not None:
            break
        time.sleep(0.15)
    if p.poll() is None:
        p.kill()
    time.sleep(0.5)


def is_elevated():
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def mmc_pids():
    """当前 mmc.exe 的 PID 列表（提权残留盘点用）。"""
    out = []
    p = subprocess.run(["tasklist", "/FI", "IMAGENAME eq mmc.exe", "/FO", "CSV", "/NH"],
                       capture_output=True, text=True, errors="replace")
    for line in (p.stdout or "").splitlines():
        parts = [x.strip().strip('"') for x in line.split(",")]
        if len(parts) >= 2 and parts[0].lower() == "mmc.exe":
            try:
                out.append(int(parts[1]))
            except ValueError:
                pass
    return out


def kill_window(h, residue):
    """关新增窗口：WM_CLOSE → taskkill /F /T /PID；提权 mmc 两者都可能被拒——
    清不掉的 PID 记入 residue，由收尾统一提示人工处理（§108）。"""
    try:
        win32gui.PostMessage(h, WM_CLOSE, 0, 0)
    except Exception:
        pass
    time.sleep(1.0)
    if win32gui.IsWindow(h):
        try:
            _, pid = win32process.GetWindowThreadProcessId(h)
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)], capture_output=True)
        except Exception:
            pass
        time.sleep(0.5)
    if win32gui.IsWindow(h):
        try:
            _, pid = win32process.GetWindowThreadProcessId(h)
            if pid and pid not in residue:
                residue.append(pid)
        except Exception:
            pass


def run_cmd_case(cmd_id, classes, title_needles, name):
    p, main = launch()
    det = "main=%s" % bool(main)
    ok = False
    new = []
    if main:
        before = snap(classes)
        user32.PostMessageW(main, WM_COMMAND, cmd_id, 0)
        deadline = time.time() + 10.0                       # msc 经 mmc.exe 启动稍慢
        while time.time() < deadline:
            time.sleep(0.3)
            now = snap(classes)
            new = [w for w in now if w not in before]
            if new:
                break
        if new:
            h, title = new[0]
            ok = any(n in title for n in title_needles)
            det += " | cmd=%d new=%s" % (cmd_id, [(hex(h), title) for h, title in new])
        else:
            det += " | cmd=%d 无新窗口（%.0fs 超时）" % (cmd_id, 10)
    close_app(p, main)
    for h, _ in new:
        kill_window(h, residue)
    res.append((name, ok, det))


print("==== verify_r107 (§107) ====", flush=True)
pre = mmc_pids()
if pre:
    print("NOTE: 运行前已有 mmc.exe 残留 PID %s（多为前次提权实例）" % pre, flush=True)
if not is_elevated():
    print("NOTE: 本 shell 非提权 → case A 的 devmgmt.msc 会拉起**提权** mmc，"
          "WM_CLOSE/taskkill 都可能被拒；在【管理员】终端运行可自动清理", flush=True)
if "--no-devmgmt" in sys.argv:
    # §108：批跑/重复回归时可用 --no-devmgmt 跳过 case A（每次都会新建一个清不掉的
    # 提权 mmc，非提权 shell 无法回收）；发布前/手动验收请跑全量两案。
    res.append(("A 设备管理器（--no-devmgmt：本次跳过，避免残留提权 mmc）", True, "SKIP"))
    print("NOTE: --no-devmgmt 生效：跳过设备管理器一案（不新建提权 mmc）", flush=True)
else:
    run_cmd_case(IDM_TOOL_DEV, MMC_CLASSES, ("设备管理", "Device Manager"),
                 "A 设备管理器：命令后新出现 MMC 设备管理器窗口")
run_cmd_case(IDM_TOOL_EXPLORER, CAB_CLASSES, ("电脑", "Computer"),
             "B 文件资源管理器：命令后新出现「我的电脑/此电脑」窗口")

shutil.rmtree(BIN, ignore_errors=True)
print("==== verify_r107 (§107) ====", flush=True)
for name, ok, det in res:
    print("[%s] %s | %s" % ("PASS" if ok else "FAIL", name, det), flush=True)
fails = [n for n, ok, _ in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok, _ in res if ok), len(fails)), flush=True)
if residue:
    print("NOTE: 本轮新增的提权 mmc.exe 无法从本 shell 结束：PID %s —— 请在【管理员】"
          "任务管理器结束（或在提权终端重跑本探针 = 自动清理）" % residue, flush=True)
if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
