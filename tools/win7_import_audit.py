# -*- coding: utf-8 -*-
# win7_import_audit.py — Win7 兼容性导入审计（§105，build.bat 内置门禁）：
# 解析 PE 导入表，报出「仅在 Windows 8 及以上存在的 API」的**静态导入**——这类导入
# 会让 exe 在老系统上**加载期**就弹「无法定位程序输入点」而根本起不来。
# 背景：§105 实测 MSVC 14.51 的静态 STL（libcpmt 的 xtime.obj/winapisupp.obj）
# 静态导入 GetSystemTimePreciseAsFileTime（Win8+），x86 单 exe 在 Win7 SP1 上直接
# 启动失败（用户实拍）；src/platform/win7_shim.{cpp,asm} 提供同名符号消掉该导入。
# 本脚本既是当时的红绿判据，也是长期守卫：换工具链/换 STL/新增 STL 用法导致
# Win8+ 导入回流时，build.bat 会在链接后立刻报红并失败。
# §108 加固（codex 第十五轮 P3-3）：新增**数据驱动白名单**判据——除黑名单外，任何
#   不在 win7_import_allowlist.txt（本目录数据文件，条数随人工过审增减；文件缺失=空表
#   → 全判新，fail-closed）里的
#   导入名都判红。换工具链/升 STL/新 API 用法会立刻触发复核，比 30 条黑名单更难过。
#   白名单文件缺失 = 空表 → 全部导入判新（fail-closed，不会静默放行）。
# 用法：python -X utf8 win7_import_audit.py <exe 路径> [--allow NAME]... [--print-imports]
#   退出码 0 = 导入表合规；1 = 黑名单命中 或 出现未审批的新导入（构建应失败）；
#   2 = 用法错误。--print-imports = 只打印导入表（重生成白名单数据文件用）。
# 维护：审出一条合法新导入 → 追加 "DLL!Func" 到白名单文件；命中黑名单 → 修根因
#   （如 §105 的垫片/换实现），勿把 Win8+ API 放进白名单。
import os
import struct
import sys

# Win8+ 才提供的常见函数（按需追加；宁少勿错——误报会阻塞正常构建，
# 每条都必须能在文档中确证最低 OS 版本）
WIN8_PLUS = {
    "GetSystemTimePreciseAsFileTime": "Win8+",
    "SetThreadDescription": "Win10 1607+",
    "GetThreadDescription": "Win10 1607+",
    "WaitOnAddress": "Win8+",
    "WakeByAddressSingle": "Win8+",
    "WakeByAddressAll": "Win8+",
    "GetDpiForWindow": "Win10 1607+",
    "GetDpiForSystem": "Win10 1607+",
    "GetSystemMetricsForDpi": "Win10 1607+",
    "AdjustWindowRectExForDpi": "Win10 1607+",
    "SetProcessDpiAwarenessContext": "Win10 1703+",
    "GetDpiForMonitor": "Win8.1+",
    "SetThreadDpiAwarenessContext": "Win10 1607+",
    "CreateFile2": "Win8+",
    "GetOverlappedResultEx": "Win8+",
    "WaitForDebugEventEx": "Win10+",
    "GetTempPath2W": "Win10 2004+",
    "GetPackageFullName": "Win8+",
    "GetCurrentPackageFullName": "Win8+",
    "GetApplicationUserModelId": "Win8+",
    "GetThreadInformation": "Win8+",
    "SetThreadInformation": "Win8+",
    "PrefetchVirtualMemory": "Win8+",
    "DiscardVirtualMemory": "Win8.1+",
    "OfferVirtualMemory": "Win8.1+",
    "ReclaimVirtualMemory": "Win8.1+",
    "QueryInterruptTimePrecise": "Win10 1809+",
    "GetSystemCpuSetInformation": "Win10+",
    "CreateFileMapping2": "Win10 1703+",
    "VirtualAlloc2": "Win10 1803+",
    "MapViewOfFile3": "Win10 1803+",
    "IsWow64Process2": "Win10 1803+",
}
# Win7 上不存在的“伪 DLL”（api-set / ext-ms）；它们只在装有 UCRT/应用框架的系统上
# 才可解析——单文件静态 CRT 的产品绝不能依赖（§105 导入表实测无一条）
FAKE_DLL_PREFIX = ("API-MS-WIN-", "EXT-MS-")

# §108 白名单数据（同目录 win7_import_allowlist.txt，每行 "DLL大写!函数名"，# 开头为注释）
_ALLOW_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "win7_import_allowlist.txt")


def load_approved():
    try:
        with open(_ALLOW_FILE, encoding="utf-8") as f:
            return frozenset(l.strip() for l in f if l.strip() and not l.startswith("#"))
    except OSError:
        return frozenset()          # fail-closed：缺失/不可读 = 全部导入判新


APPROVED = load_approved()


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def parse_imports(path):
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        raise SystemExit("not a PE: %s" % path)
    e = u32(d, 0x3C)
    assert d[e:e + 4] == b"PE\0\0", "no PE header"
    nsec = u16(d, e + 6)
    opt = e + 24
    magic = u16(d, opt)
    dd = opt + (96 if magic == 0x10B else 112)
    imp_rva = u32(d, dd + 8)
    secs = []
    so = opt + u16(d, e + 20)
    for i in range(nsec):
        o = so + i * 40
        vsz, va, rsz, ra = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vsz, ra, rsz))

    def r2o(rva):
        for va, vsz, ra, rsz in secs:
            if va <= rva < va + vsz:
                return ra + (rva - va)
        return None

    def cstr(off):
        end = d.index(b"\0", off)
        return d[off:end].decode("latin-1")

    out = {}
    if not imp_rva:
        return out
    o = r2o(imp_rva)
    i = 0
    while True:
        ent = d[o + i * 20: o + i * 20 + 20]
        if len(ent) < 20 or ent == b"\0" * 20:
            break
        oft, ts, fc, name_rva, ft = struct.unpack("<IIIII", ent)
        dll = cstr(r2o(name_rva)).upper()
        t = r2o(oft if oft else ft)
        names = []
        j = 0
        while True:
            th = u32(d, t + j * 4)
            if th == 0:
                break
            if not (th & 0x80000000):
                # 名字条目 = {WORD Hint; CHAR Name[]} —— 必须跳过 2 字节 Hint，
                # 否则名字首字节被 Hint 低位污染（§105 自查踩过：精确匹配全失效）
                names.append(cstr(r2o(th) + 2))
            j += 1
        out[dll] = names
        i += 1
    return out


def main():
    if len(sys.argv) < 2:
        print("usage: win7_import_audit.py <exe> [--allow NAME]... [--print-imports]")
        return 2
    exe = sys.argv[1]
    print_mode = "--print-imports" in sys.argv
    allow = set(a.upper() for a in sys.argv[2:]
                if a not in ("--allow", "--print-imports"))
    imports = parse_imports(exe)
    total = sum(len(v) for v in imports.values())
    if print_mode:
        for dll in sorted(imports):
            for n in sorted(imports[dll]):
                print("%s!%s" % (dll, n))
        return 0
    hits = []
    new = []
    for dll, names in imports.items():
        if dll.startswith(FAKE_DLL_PREFIX):
            hits.append((dll, "(whole dll)", "Win7 has no such pseudo DLL"))
            continue
        for n in names:
            if n in WIN8_PLUS and n.upper() not in allow:
                hits.append((dll, n, WIN8_PLUS[n]))
            if dll + "!" + n not in APPROVED:
                new.append(dll + "!" + n)
    print("imports: %d dlls / %d functions (approved set: %d)" %
          (len(imports), total, len(APPROVED)))
    # §123（十九轮 P3-2）：白名单陈旧条目提示（INFO，不判红）——exe 已不再引用、白名单还留着的
    # 条目长期会掩盖"某条导入已消失"的信息（本条 GetPixel 即 §121 改 DIB 后的残留）。
    stale = sorted(k for k in APPROVED
                   if k.split("!", 1)[0] not in imports
                   or k.split("!", 1)[1] not in imports.get(k.split("!", 1)[0], set()))
    for k in stale:
        print("  [STALE] %s  (allowlisted but not imported — consider removing)" % k)
    for dll, n, ver in hits:
        print("  [WIN8+] %-20s %s  (%s)" % (dll, n, ver))
    for k in new:
        print("  [NEW]   %s" % k)
    if hits or new:
        if new:
            print("  -> new import: verify Win7 SP1 availability, then add it to the")
            print("     allowlist data file next to this script (%d-entry baseline)." % len(APPROVED))
            print("     A Win8+ hit instead -> fix the root cause (e.g. the S105 shim).")
            print("     Regenerate the whole allowlist by running this script with the")
            print("     --print-imports switch against the exe.")
        print("RESULT FAIL (%d Win8+ imports, %d unapproved imports)" % (len(hits), len(new)))
        return 1
    print("RESULT PASS (no Win8+ static imports; all %d imports approved)" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
