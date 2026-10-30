# -*- coding: utf-8 -*-
# verify_r77_cfg.py [A|B] - 校验设置持久化往返（§77 附带老 bug 回归）：
#   老 bug：启动时 EN_CHANGE（控件创建灌初值 + ApplyFonts WM_SETFONT）把
#   cfg.checksum_* 覆盖成 none/1 →「保存过校验设置重启即丢」。
#   修复：uiInitDone_ 建窗期通知闸门。
#   A) 启动回显 cursel=5/edit='9' → WM_CLOSE 存盘应保真 add8/9；
#   B) 运行期经消息改 CRC32/4 → 存盘应对应 crc32/4（闸门不误伤运行期同步）。
#   §120：并入 §114 沙箱红线——本探针测的就是「配置存盘往返」，应用必须把配置写进
#   **沙箱** sncom.json（fixture 同样由探针自写，§81 的「外部 bash 预写」流程退场），
#   绝不再以 develop\dist 为 cwd / 不再写用户配置。
import os, sys, json, time, subprocess, ctypes, shutil
import win32gui, win32process, win32api
user32 = ctypes.windll.user32
PHASE = sys.argv[1] if len(sys.argv) > 1 else "A"
res = []
# §120（十八轮 P3-5）：环境性跳过单独计数。
skips = []
def skip(desc):
    skips.append(desc)
    print("SKIP " + desc, flush=True)


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


def text(h):
    # 跨进程 GetWindowText 读 EDIT 会拿到系统缓存的创建期标题（假值，实测恒 '1'）——
    # 必须直接 WM_GETTEXT 问控件本体。
    b = ctypes.create_unicode_buffer(64)
    user32.SendMessageW(h, 0x000D, 64, b)
    return b.value


# Q3（§81 审查轮核实）：A 组依赖 fixture（checksum_kind=add8/start=9）——现由探针自写进沙箱，
# 缺失只剩"写盘失败"一种可能，仍保留显式 SKIP 守卫。B 组与初值无关（改成 crc32/4 再断言存盘）。
HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.join(os.path.dirname(HERE), "develop")
BIN = os.path.join(HERE, "_probe", "out", "r77_cfg")
shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(os.path.join(DEV, "dist", "sncom.exe"), os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as _f:
    _f.write(json.dumps({"checksum_kind": "add8", "checksum_start": 9}))
CFG = os.path.join(BIN, "sncom.json")


def fixture_ok():
    try:
        with open(CFG, "r", encoding="utf-8-sig") as f:
            obj = json.load(f)
        return obj.get("checksum_kind") == "add8" and obj.get("checksum_start") == 9
    except Exception:
        return False


if PHASE == "A" and not fixture_ok():
    skip("§77-1 fixture 未写入沙箱（checksum_kind=add8/checksum_start=9）——A 组 SKIP，不判失败")
    shutil.rmtree(BIN, ignore_errors=True)
    raise SystemExit(0)

proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
main = 0
for _ in range(40):
    main = find_toplevel("SNComMain", proc.pid)
    if main:
        break
    time.sleep(0.25)
assert main, "SNComMain 未找到"
time.sleep(2.5)

cs = user32.GetDlgItem(main, 3055)                    # IDC_CMB_CHECKSUM
st = user32.GetDlgItem(main, 3051)                    # IDC_EDIT_CHKSTART
tl = user32.GetDlgItem(main, 3053)                    # IDC_CMB_CHKTAIL

if PHASE == "A":
    cursel = user32.SendMessageW(cs, 0x0147, 0, 0)    # CB_GETCURSEL
    chk("A1 启动回显 校验=ADD8（cursel=5）", cursel == 5, "cursel=%d" % cursel)
    chk("A2 启动回显 起始字节=9", text(st) == "9", "text=%r" % text(st))
    tcur = user32.SendMessageW(tl, 0x0147, 0, 0)
    chk("A3 帧尾档位回显合法（0..4）", 0 <= tcur <= 4, "cursel=%d" % tcur)
else:
    user32.SendMessageW(cs, 0x014E, 7, 0)             # CB_SETCURSEL 7=CRC32（静默不通知）
    win32api.PostMessage(main, 0x0111, 3055 | (1 << 16), cs)   # WM_COMMAND CBN_SELCHANGE
    time.sleep(0.3)
    # 跨进程 SetWindowText 不产生 EN_CHANGE（控件不报），按真实用户编辑补投通知
    user32.SendMessageW(st, 0x000C, 0, ctypes.c_wchar_p("4"))  # WM_SETTEXT
    win32api.PostMessage(main, 0x0111, 3051 | (1 << 16), st)   # WM_COMMAND EN_CHANGE
    time.sleep(0.3)

win32api.PostMessage(main, 0x0010, 0, 0)              # WM_CLOSE → SaveConfig + DestroyWindow
try:
    proc.wait(timeout=10)
except Exception:
    proc.kill()
    raise

saved = {}
for _ in range(40):
    try:
        with open(CFG, "r", encoding="utf-8-sig") as f:
            saved = json.load(f)
        break
    except Exception:
        time.sleep(0.25)
if PHASE == "A":
    chk("A4 无改动存盘保真 kind=add8", saved.get("checksum_kind") == "add8", repr(saved.get("checksum_kind")))
    chk("A5 无改动存盘保真 start=9", saved.get("checksum_start") == 9, repr(saved.get("checksum_start")))
else:
    chk("B1 运行期改 kind → 存盘=crc32", saved.get("checksum_kind") == "crc32", repr(saved.get("checksum_kind")))
    chk("B2 运行期改 start → 存盘=4", saved.get("checksum_start") == 4, repr(saved.get("checksum_start")))

shutil.rmtree(BIN, ignore_errors=True)
fails = [n for n, ok in res if not ok]
print("\nSUMMARY[%s] %d/%d skip=%d %s" % (PHASE, len(res) - len(fails), len(res), len(skips),
                                          ("FAIL: " + "; ".join(fails)) if fails else "ALL PASS"))
# §124（审查二十轮 P3-3）：补通用格式汇总行——批跑解析器只认 `total=N pass=N fail=N`，
# 此前相位格式 `SUMMARY[A] 5/5 skip=0` 会被显示为 no summary（绿态误读为空白）。
print("==== SUMMARY ==== total=%d pass=%d fail=%d skip=%d" % (
    len(res), sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok), len(skips)))
for _s in skips:
    print("SKIPPED: " + _s)
