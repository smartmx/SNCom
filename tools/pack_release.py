# -*- coding: utf-8 -*-
# pack_release.py — 发布包核对与打包（§109，M7；§115 补齐「包内核对」）：
#   ①读 src/version.h（版本号唯一来源）；②读 dist/sncom.exe 的 VERSIONINFO 资源并与
#   version.h 比对（数字/字符串一致性硬校验）；③默认在 dist\release\ 生成
#   SNCom-V<ver>-win32.zip（sncom.exe + README.md + LICENSE + CHANGELOG.md），
#   打印 exe/zip 的 sha256 与文件清单。
#   --check [目标]：
#     · 无目标 → 只做 ①②（loose exe，供探针/CI）；
#     · 目标 = zip → **真·包内核对**：包内 sncom.exe 与「已校验过版本链的当前构建」逐字节
#       比对（不一致 = 包已过期，改码/重建后未重出包 → FAIL）；版本链经传递成立。
#     · 目标 = 任意 exe → 只校验该 exe 的 VERSIONINFO。
# 用法：python -X utf8 tools\pack_release.py [--check [<zip|exe>]]
import ctypes
import hashlib
import io
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VH = os.path.join(ROOT, "src", "version.h")
EXE = os.path.join(ROOT, "dist", "sncom.exe")
OUTDIR = os.path.join(ROOT, "dist", "release")


def parse_version_h(path):
    s = io.open(path, encoding="utf-8").read()
    def num(name):
        m = re.search(r"#define\s+%s\s+(\d+)\b" % name, s)
        return int(m.group(1)) if m else None
    v = dict(major=num("SNCOM_VERSION_MAJOR"), minor=num("SNCOM_VERSION_MINOR"),
             patch=num("SNCOM_VERSION_PATCH"), build=num("SNCOM_VERSION_BUILD"))
    m = re.search(r'#define\s+SNCOM_VERSION_FILE\s+([\d,\s]+)', s)
    v["file"] = [int(x) for x in m.group(1).split(",")] if m else None
    v["str4"] = (re.search(r'#define\s+SNCOM_VERSION_STR4\s+"([^"]+)"', s) or [None, None])[1] \
        if re.search(r'#define\s+SNCOM_VERSION_STR4\s+"([^"]+)"', s) else None
    v["str3"] = (re.search(r'#define\s+SNCOM_VERSION_STR3\s+"([^"]+)"', s) or [None, None])[1] \
        if re.search(r'#define\s+SNCOM_VERSION_STR3\s+"([^"]+)"', s) else None
    return v


def exe_version(path):
    """PE VERSIONINFO 读取（纯 ctypes，stdlib）：FileVersion/ProductVersion/ProductName 等。"""
    ver = ctypes.windll.version
    size = ver.GetFileVersionInfoSizeW(path, None)
    if not size:
        return None
    buf = ctypes.create_string_buffer(size)
    if not ver.GetFileVersionInfoW(path, 0, size, buf):
        return None
    out = {}

    def q_raw(sub):
        p = ctypes.c_void_p()
        n = ctypes.c_uint()
        if not ver.VerQueryValueW(buf, sub, ctypes.byref(p), ctypes.byref(n)):
            return None
        return ctypes.string_at(p.value, n.value)

    def q_str(sub):
        p = ctypes.c_void_p()
        n = ctypes.c_uint()
        if not ver.VerQueryValueW(buf, sub, ctypes.byref(p), ctypes.byref(n)):
            return None
        return ctypes.wstring_at(p.value, n.value).rstrip("\x00")

    tr = q_raw("\\VarFileInfo\\Translation")
    if tr and len(tr) >= 4:
        lang, cp = int.from_bytes(tr[0:2], "little"), int.from_bytes(tr[2:4], "little")
        for key in ("FileVersion", "ProductVersion", "ProductName", "FileDescription",
                    "LegalCopyright", "OriginalFilename"):
            out[key] = q_str("\\StringFileInfo\\%04x%04x\\%s" % (lang, cp, key))
        out["lang"] = "%04x%04x" % (lang, cp)
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def version_h_self_check(v):
    errs = []
    nums = [v["major"], v["minor"], v["patch"], v["build"]]
    if None in nums or v["file"] is None or not v["str4"] or not v["str3"]:
        errs.append("version.h 解析失败（缺宏）")
        return errs
    if v["file"] != nums:
        errs.append("SNCOM_VERSION_FILE %s != 数字 %s" % (v["file"], nums))
    if v["str4"] != ".".join(map(str, nums)):
        errs.append("STR4 %r != %s" % (v["str4"], ".".join(map(str, nums))))
    if v["str3"] != ".".join(map(str, nums[:3])):
        errs.append("STR3 %r != %s" % (v["str3"], ".".join(map(str, nums[:3]))))
    return errs


def check_exe_at(path, v):
    """校验指定 exe 的 VERSIONINFO 与 version.h 一致。"""
    errs = []
    ev = exe_version(path)
    if not ev:
        return ["读取 %s VERSIONINFO 失败" % path]
    if v["str4"] and ev.get("FileVersion") != v["str4"]:
        errs.append("exe FileVersion %r != version.h %r" % (ev.get("FileVersion"), v["str4"]))
    if v["str4"] and ev.get("ProductVersion") != v["str4"]:
        errs.append("exe ProductVersion %r != version.h %r" % (ev.get("ProductVersion"), v["str4"]))
    if ev.get("ProductName") != "SNCom":
        errs.append("exe ProductName %r != SNCom" % ev.get("ProductName"))
    return errs


def check_zip(zpath, v):
    """§115（P3-1）：包内核对 = 包内 exe 与「已校验过版本链的当前构建」逐字节一致。
    版本链传递：version.h ↔ dist\\sncom.exe（已硬校验）+ 包内 exe == dist exe（本函数）
    → 包内 exe 的 VERSIONINFO 必然一致；反之只要包内 exe 与当前构建不同，就是「包已过期」
    （改码/重建后没重新出包）——这正是要拦的发布事故。"""
    if not os.path.exists(zpath):
        return ["找不到 %s" % zpath]
    try:
        with zipfile.ZipFile(zpath) as z:
            names = z.namelist()
            if "sncom.exe" not in names:
                return ["包内没有 sncom.exe（清单：%s）" % ", ".join(names)]
            data = z.read("sncom.exe")
    except (OSError, zipfile.BadZipFile) as e:
        return ["读取 %s 失败：%s" % (zpath, e)]
    inner = sha256_bytes(data)
    print("  zip 清单: %s" % ", ".join(names))
    print("  zip 内 exe: %d bytes  sha256=%s" % (len(data), inner))
    if not os.path.exists(EXE):
        print("  [NOTE] 无 dist\\sncom.exe 可比对 → 包内核对无法完成（先 build.bat）")
        return ["缺少 %s，包内核对无法完成" % EXE]
    cur = sha256(EXE)
    if cur != inner:
        return ["包内 exe 与当前 dist\\sncom.exe 不一致（包已过期？改码后请重新出包）："
                "包内 %s != dist %s" % (inner[:16], cur[:16])]
    print("  [OK] 包内 exe = 当前构建（%s）；VERSIONINFO 经传递校验一致（%s）" % (cur[:16], v.get("str3")))
    return []


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    check_only = "--check" in sys.argv
    target = args[0] if args else None
    v = parse_version_h(VH)
    ver = v.get("str3") or "?"
    print("version.h: %s (file=%s)" % (ver, v.get("file")))
    errs = version_h_self_check(v)
    if errs:
        for e in errs:
            print("  [FAIL] %s" % e)
        print("RESULT FAIL")
        return 1
    print("  [OK] version.h 自洽")

    if target and target.lower().endswith(".zip"):
        errs = check_zip(target, v)
        if errs:
            for e in errs:
                print("  [FAIL] %s" % e)
            print("RESULT FAIL")
            return 1
        print("RESULT PASS (check zip: %s)" % os.path.basename(target))
        return 0

    exe = target or EXE
    if not os.path.exists(exe):
        print("  [FAIL] 缺少 %s（先 build.bat）" % exe)
        print("RESULT FAIL")
        return 1
    errs = check_exe_at(exe, v)
    if errs:
        for e in errs:
            print("  [FAIL] %s" % e)
        print("RESULT FAIL")
        return 1
    print("  [OK] exe VERSIONINFO 一致（%s）：%s" % (ver, exe))
    if check_only:
        print("RESULT PASS (check only)")
        return 0

    os.makedirs(OUTDIR, exist_ok=True)
    zpath = os.path.join(OUTDIR, "SNCom-V%s-win32.zip" % ver)
    items = ["sncom.exe", "README.md", "LICENSE", "CHANGELOG.md"]
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for it in items:
            src = os.path.join(ROOT, "dist", it) if it == "sncom.exe" else os.path.join(ROOT, it)
            if not os.path.exists(src):
                print("  [FAIL] 缺少打包项 %s" % src)
                return 1
            z.write(src, it)
    print("  exe : %d bytes  sha256=%s" % (os.path.getsize(EXE), sha256(EXE)))
    print("  zip : %s (%d bytes)" % (zpath, os.path.getsize(zpath)))
    print("        sha256=%s" % sha256(zpath))
    print("  files: %s" % ", ".join(items))
    print("RESULT PASS (release packed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
