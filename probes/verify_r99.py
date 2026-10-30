# -*- coding: utf-8 -*-
# §99 验证（codex 第十轮全量审查 U1/U2 修复）：
#   r99-A  深嵌套 sncom.json（20000 层）启动不再栈溢出（0xC00000FD）——窗口照常出现，
#          原文件改名 .bad 备份；关闭后重写默认配置
#   r99-B  敌意数值（reconnect_ms=0/buffer_cap=1G/split_ms=-5/win_w=99999/baud=99999999/
#          send_interval_ms=0）启动存活，关闭后落盘值全部被钳制
#   r99-C  正常数值（reconnect_ms=2000/buffer_cap=5M 等）往返不变形——防钳制误伤
# 前置：无 sncom.exe 运行（探针自会 taskkill）；结束后不重启 GUI（由主流程统一重启）。
import ctypes
import json
import os
import subprocess
import sys
import time

DEVELOP = r"F:\projects\sscom二次分析开发\SNCom\develop"
DIST = os.path.join(DEVELOP, "dist")
EXE = os.path.join(DIST, "sncom.exe")
CFG = os.path.join(DIST, "sncom.json")
CFG_BAD = CFG + ".bad"
TITLE = "SNCom 串口/网络数据调试器"
WM_CLOSE = 0x0010

user32 = ctypes.windll.user32
res = []


def find_window():
    return user32.FindWindowW(None, TITLE)


def kill_all():
    subprocess.run(["taskkill", "/F", "/IM", "sncom.exe"], capture_output=True)
    time.sleep(0.5)


def remove_cfg_files():
    for f in (CFG, CFG_BAD):
        try:
            if os.path.exists(f):
                os.remove(f)
        except OSError:
            pass


def launch(timeout=10.0):
    """启动并等主窗出现。返回 (Popen, found)。进程退出=崩溃（found=False）。"""
    p = subprocess.Popen([EXE], cwd=DIST)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if p.poll() is not None:
            return p, False
        if find_window():
            time.sleep(0.6)                     # 等首帧稳定
            return p, True
        time.sleep(0.2)
    return p, p.poll() is None


def close_graceful(p, timeout=8.0):
    hwnd = find_window()
    if hwnd:
        user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    try:
        p.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        return False


def read_cfg():
    with open(CFG, "r", encoding="utf-8-sig") as f:
        return json.load(f)


kill_all()
try:
    # ---------- r99-A：深嵌套不崩 + .bad 备份 ----------
    remove_cfg_files()
    depth = 20000
    payload = '{"a":' * depth + "1" + "}" * depth
    with open(CFG, "w", encoding="utf-8") as f:
        f.write(payload)
    p, ok = launch()
    alive = ok and p.poll() is None
    bad_exists = os.path.exists(CFG_BAD)
    okA = alive and bad_exists
    res.append(("A 深嵌套不崩+.bad 备份", okA,
                "alive=%s bad=%s poll=%s" % (alive, bad_exists, p.poll())))
    close_graceful(p)

    # ---------- r99-B：敌意数值钳制落盘 ----------
    remove_cfg_files()
    hostile = {"reconnect_ms": 0, "buffer_cap": 1 << 30, "split_ms": -5,
               "win_w": 99999, "baud": 99999999, "send_interval_ms": 0,
               "encoding": "gbk"}
    with open(CFG, "w", encoding="utf-8") as f:
        json.dump(hostile, f)
    p, ok = launch()
    okB = ok
    if ok:
        close_graceful(p)
        try:
            got = read_cfg()
            okB = (got["reconnect_ms"] >= 100 and got["buffer_cap"] <= 100 * 1024 * 1024
                   and got["split_ms"] >= 1 and got["win_w"] <= 16384
                   and got["baud"] == 50000000 and got["send_interval_ms"] >= 1)   # §100：上限=50M
            det = "reconnect=%s cap=%s split=%s win_w=%s baud=%s interval=%s" % (
                got.get("reconnect_ms"), got.get("buffer_cap"), got.get("split_ms"),
                got.get("win_w"), got.get("baud"), got.get("send_interval_ms"))
        except Exception as e:                  # noqa: BLE001
            okB, det = False, "读回失败: %s" % e
    else:
        det = "启动失败 poll=%s" % p.poll()
    res.append(("B 敌意数值钳制落盘", okB, det))

    # ---------- r99-C：正常数值往返不变形（§100：baud 50M=§77/§79 合法上限，勿被钳回） ----------
    remove_cfg_files()
    normal = {"reconnect_ms": 2000, "buffer_cap": 5 * 1024 * 1024,
              "split_ms": 20, "win_w": 960, "win_h": 720, "encoding": "gbk",
              "baud": 50000000}
    with open(CFG, "w", encoding="utf-8") as f:
        json.dump(normal, f)
    p, ok = launch()
    okC = ok
    if ok:
        close_graceful(p)
        try:
            got = read_cfg()
            okC = (got["reconnect_ms"] == 2000 and got["buffer_cap"] == 5 * 1024 * 1024
                   and got["split_ms"] == 20 and got["win_w"] == 960
                   and got["baud"] == 50000000)
            det = "reconnect=%s cap=%s split=%s win_w=%s baud=%s" % (
                got.get("reconnect_ms"), got.get("buffer_cap"),
                got.get("split_ms"), got.get("win_w"), got.get("baud"))
        except Exception as e:                  # noqa: BLE001
            okC, det = False, "读回失败: %s" % e
    else:
        det = "启动失败 poll=%s" % p.poll()
    res.append(("C 正常数值往返不变形", okC, det))
finally:
    kill_all()
    remove_cfg_files()

print("==== verify_r99 (§99 加固) ====", flush=True)
for name, ok, det in res:
    print("[%s] %s | %s" % ("PASS" if ok else "FAIL", name, det), flush=True)
fails = [n for n, ok, _ in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok, _ in res if ok), len(fails)), flush=True)
if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
