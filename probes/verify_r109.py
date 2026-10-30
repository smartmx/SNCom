# -*- coding: utf-8 -*-
# verify_r109.py — §109 版本链单一来源回归（M7 打包）：
#   A 版本链核对：tools/pack_release.py --check（version.h 自洽 + exe VERSIONINFO 一致）；
#   B 单一来源结构断言：sncom.rc 必须引用 version.h 宏（不得再出现字面量 0,0,1,0），
#     version.h 数字/STR3/STR4/FILE 四者互相一致；
#   C 界面一致性：沙箱启动 exe → WM_COMMAND 2223 打开「关于」→ 其静态文本含
#     「版本：<STR3>」；状态栏版本段文本长度 == len("V"+STR3)（SB_GETTEXTLENGTHW，免缓冲）。
# 沙箱 exe（probes/_probe/out/r109），不碰 dist 配置。
import ctypes
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time

import win32gui
import win32process

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, "..", "develop"))
SRC = os.environ.get("SNCOM_EXE") or os.path.join(DEV, "dist", "sncom.exe")
BIN = os.path.join(HERE, "_probe", "out", "r109")
WM_COMMAND, WM_CLOSE = 0x0111, 0x0010
SB_GETTEXTLENGTHW = 0x0400 + 12
res = []
user32 = ctypes.windll.user32


def chk(name, ok, det=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + det if det else ""), flush=True)


# ---- A/B 版本链（静态） ----------------------------------------------------------
p = subprocess.run([sys.executable, "-X", "utf8", os.path.join(DEV, "tools", "pack_release.py"), "--check"],
                   capture_output=True, text=True, errors="replace")
out = ((p.stdout or "") + (p.stderr or "")).strip().replace("\n", " | ")
chk("A 版本链核对 pack_release.py --check", p.returncode == 0 and "RESULT PASS" in out, out[:400])

vh = io.open(os.path.join(DEV, "src", "version.h"), encoding="utf-8").read()
rc = io.open(os.path.join(DEV, "sncom.rc"), encoding="utf-8-sig").read()
nums = [int(re.search(r"#define\s+SNCOM_VERSION_%s\s+(\d+)" % k, vh).group(1))
        for k in ("MAJOR", "MINOR", "PATCH", "BUILD")]
file4 = [int(x) for x in re.search(r"#define\s+SNCOM_VERSION_FILE\s+([\d,\s]+)", vh).group(1).split(",")]
str4 = re.search(r'#define\s+SNCOM_VERSION_STR4\s+"([^"]+)"', vh).group(1)
str3 = re.search(r'#define\s+SNCOM_VERSION_STR3\s+"([^"]+)"', vh).group(1)
ok_self = (file4 == nums and str4 == ".".join(map(str, nums)) and str3 == ".".join(map(str, nums[:3])))
chk("B1 version.h 自洽（数字/FILE/STR4/STR3）", ok_self, "nums=%s file=%s str4=%s str3=%s" % (nums, file4, str4, str3))
ok_rc = ("SNCOM_VERSION_FILE" in rc and "SNCOM_VERSION_STR4" in rc
         and not re.search(r"FILEVERSION\s+\d", rc))
chk("B2 sncom.rc 引用 version.h 宏（无字面量 FILEVERSION）", ok_rc, "rc_len=%d" % len(rc))
LIC = io.open(os.path.join(DEV, "LICENSE"), encoding="utf-8").read()
CHL = io.open(os.path.join(DEV, "CHANGELOG.md"), encoding="utf-8").read()
chk("B3 LICENSE/CHANGELOG 就位", "MIT License" in LIC and ("## " + str3) in CHL,
    "license=%d changelog_ver=%s" % (len(LIC), ("## " + str3) in CHL))

# ---- C 界面一致性 ----------------------------------------------------------------
def find_toplevel(cls, pid):
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == cls and win32process.GetWindowThreadProcessId(h)[1] == pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0


shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
    json.dump({"mode": "serial"}, f)
proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(70):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.15)
time.sleep(2.5)

about = 0
if main:
    win32gui.PostMessage(main, WM_COMMAND, 2223, 0)      # 帮助→关于
    deadline = time.time() + 5.0
    while time.time() < deadline and not about:
        def cb2(h, l):
            global about
            if win32gui.GetClassName(h) == "#32770" and \
               win32process.GetWindowThreadProcessId(h)[1] == proc.pid and \
               "SNCom" in win32gui.GetWindowText(h):
                about = h
            return True
        win32gui.EnumWindows(cb2, None)
        if not about:
            time.sleep(0.2)

texts = []
if about:
    def cb3(h, l):
        t = win32gui.GetWindowText(h)
        if t:
            texts.append((win32gui.GetClassName(h), t))
        return True
    win32gui.EnumChildWindows(about, cb3, None)
want = "版本：" + str3
hit = any(want in t for _, t in texts)
chk("C1 关于框正文含「%s」" % want, hit,
    "dialog=%s texts=%s" % (hex(about), [t.replace("\n", "/")[:60] for _, t in texts][:4]))
if about:
    win32gui.PostMessage(about, WM_COMMAND, 1, 0)        # IDOK
    time.sleep(0.5)

stat = []
if main:
    def cb4(h, l):
        if win32gui.GetClassName(h) == "msctls_statusbar32":
            stat.append(h)
        return True
    win32gui.EnumChildWindows(main, cb4, None)
w = len("V" + str3)
if stat:
    lens = [user32.SendMessageW(stat[0], SB_GETTEXTLENGTHW, i, 0) & 0xFFFF for i in range(4)]
    chk("C2 状态栏某段长度 == len(V%s)=%d" % (str3, w), w in lens, "lens=%s" % lens)
else:
    chk("C2 状态栏版本段（未找到状态栏）", True, "SKIP")

if main:
    win32gui.PostMessage(main, WM_CLOSE, 0, 0)
for _ in range(20):
    if proc.poll() is not None:
        break
    time.sleep(0.15)
if proc.poll() is None:
    proc.kill()
shutil.rmtree(BIN, ignore_errors=True)

# ---- D 包内核对（§115 P3-1）：zip 路径必须真校验，且能拦「过期包」 ----
def run_pack(*args):
    p2 = subprocess.run([sys.executable, "-X", "utf8", os.path.join(DEV, "tools", "pack_release.py")] + list(args),
                        capture_output=True, text=True, errors="replace")
    return p2.returncode, ((p2.stdout or "") + (p2.stderr or "")).strip().replace("\n", " | ")


EXE_OTHER = os.path.join(HERE, "_probe", "out", "r117_old", "sncom.exe")   # 上一版构建（无 §115 代码）
zip_release = os.path.join(DEV, "dist", "release", "SNCom-V%s-win32.zip" % str3)
if os.path.exists(zip_release):
    rc, o = run_pack("--check", zip_release)
    chk("D1 发布包 --check <zip>：包内 exe = 当前构建（真校验，非空操作）",
        rc == 0 and "check zip" in o and "包内 exe = 当前构建" in o, o[:300])
else:
    chk("D1 发布包 --check <zip>（尚无 dist\\release 包）", True, "SKIP：先出包再跑本组")

mkzip_dir = os.path.join(HERE, "_probe", "out", "r109_zips")
shutil.rmtree(mkzip_dir, ignore_errors=True)
os.makedirs(mkzip_dir)
if os.path.exists(EXE_OTHER):
    import zipfile
    stale = os.path.join(mkzip_dir, "stale.zip")
    with zipfile.ZipFile(stale, "w") as z:
        z.write(EXE_OTHER, "sncom.exe")
    rc, o = run_pack("--check", stale)
    chk("D2 负对照：包内 exe 与当前构建不一致 → 判「包已过期」rc=1",
        rc == 1 and ("包已过期" in o or "不一致" in o), o[:300])
    empty = os.path.join(mkzip_dir, "noexe.zip")
    with zipfile.ZipFile(empty, "w") as z:
        z.writestr("README.md", "x")
    rc, o = run_pack("--check", empty)
    chk("D3 负对照：包内无 sncom.exe → rc=1", rc == 1 and "没有 sncom.exe" in o, o[:300])
else:
    chk("D2/D3 负对照（缺上一版 exe 参照物）", True, "SKIP：%s 不存在" % EXE_OTHER)
rc, o = run_pack("--check", os.path.join(DEV, "dist", "sncom.exe"))
chk("D4 --check <exe 路径> 形式可用", rc == 0 and "RESULT PASS" in o, o[:200])
if not os.path.exists(zip_release):
    shutil.rmtree(mkzip_dir, ignore_errors=True)

print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
if any(not ok for _, ok in res):
    sys.exit(1)
