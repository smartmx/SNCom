# -*- coding: utf-8 -*-
# verify_r105.py — §105 Win7 导入红线回归（A 绿 + B 红对照 + C 直断言）：
#   A 当前构建 develop/dist/sncom.exe 经 tools/win7_import_audit.py 必须 PASS
#     （exe 不得含仅在 Win8+ 存在的静态导入——含则 Win7 加载期弹「无法定位程序输入点」）；
#   B 红对照：修复前留存的旧构建 probes/_probe/out/r104_old/sncom.exe 必须 FAIL
#     （其导入表恰含 GetSystemTimePreciseAsFileTime——即用户 Win7 实拍的那一条），
#     证明审计判据有判别力（无此对照则可能"永远绿"）；
#   C 直接断言：当前 exe 的全部导入名里不得出现 GetSystemTimePreciseAsFileTime
#     （绕过审计脚本的名单，防名单/解析双重失效）。
# 背景：MSVC 14.51 静态 STL（libcpmt 的 xtime.obj）由 <thread> 触发静态导入该 Win8+ API；
# 修复=src/platform/win7_shim.{cpp,asm} 提供同名符号，build.bat 第 [5/5] 步即为本审计。
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIT = os.path.join(HERE, "..", "develop", "tools", "win7_import_audit.py")
NEW = os.path.join(HERE, "..", "develop", "dist", "sncom.exe")
OLD = os.path.join(HERE, "_probe", "out", "r104_old", "sncom.exe")
SYM = "GetSystemTimePreciseAsFileTime"
res = []


def run_audit(exe):
    p = subprocess.run([sys.executable, "-X", "utf8", AUDIT, exe],
                       capture_output=True, text=True, errors="replace")
    return p.returncode, ((p.stdout or "") + (p.stderr or "")).strip().replace("\n", " | ")


rcn, outn = run_audit(NEW)
okA = rcn == 0 and "RESULT PASS" in outn
res.append(("A 当前构建无 Win8+ 静态导入（审计 PASS）", okA,
            "rc=%d %s" % (rcn, outn)))

if os.path.exists(OLD):
    rco, outo = run_audit(OLD)
    okB = rco == 1 and SYM in outo
    res.append(("B 红对照：修复前构建必须判红（%s）" % SYM, okB,
                "rc=%d %s" % (rco, outo)))
else:
    res.append(("B 红对照：修复前构建必须判红", True, "SKIP: 无 %s" % OLD))

sys.path.insert(0, os.path.dirname(AUDIT))
import win7_import_audit as A                                     # noqa: E402
imp = A.parse_imports(NEW)
flat = [n for names in imp.values() for n in names]
okC = SYM not in flat and not any(d.startswith(A.FAKE_DLL_PREFIX) for d in imp)
res.append(("C 直断言：导入名无该符号、无 api-ms-win/Ext-MS 伪 DLL", okC,
            "%d imports / %d dlls" % (len(flat), len(imp))))

print("==== verify_r105 (§105) ====", flush=True)
for name, ok, det in res:
    print("[%s] %s | %s" % ("PASS" if ok else "FAIL", name, det), flush=True)
fails = [n for n, ok, _ in res if not ok]
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    len(res), sum(1 for _, ok, _ in res if ok), len(fails)), flush=True)
if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
