# -*- coding: utf-8 -*-
"""SNCom ↔ PyCom 三层对拍比较器。由 tools\\runcross.bat 调用，无命令行参数。
输入（均为固定路径）：hexself 结果、Python 基准 JSON、dump_native 行文件。
比较口径：行文本逐行 + nbytes/start_abs 元数据；时间戳取真实时钟不对齐，prefix 不比。
"""
import json
import os
import sys

HEX_CASES = [
    "01020A", "0102", "010203", "0102", "0102", "0102",
    "AB", "0102", "", "FAIL", "0120", "FAIL",
    "FAIL", "000000", "FFFE0A", "313233", "3130", "ABCD",
]
CS_FIXED = {
    "modbus": "4B37", "ccitt": "31C3", "crc8": "F4",
    "add8": "DD", "add16": "01DD", "xor8": "31",
    "crc32": "CBF43926", "add8_0": "23",           # §77：ISO-HDLC 标准向量 / 0-ADD（0-0xDD）
    "insert": "31323334ca39", "append": "313233343971a1",
}
TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)))


def check_hex(result_file):
    ok = True
    with open(result_file, "r", encoding="ascii") as fh:
        for ln in fh.read().splitlines():
            parts = ln.split()
            if not parts or parts[0] == "SUMMARY":
                if parts and parts[0] == "SUMMARY":
                    print("  hexself:", " ".join(parts))
                continue
            if parts[0].isdigit():
                idx = int(parts[0])
                if len(parts) >= 3:
                    got, st = parts[1], parts[2]
                else:
                    got, st = "", parts[1]      # 空串用例：fprintf 产出两列
                if st != "ok" or got != HEX_CASES[idx]:
                    print(f"  hex 用例 {idx}: got={got} want={HEX_CASES[idx]}")
                    ok = False
            else:
                tag, got, st = parts[0], parts[1], parts[2]
                if st != "ok" or got.lower() != CS_FIXED[tag].lower():
                    print(f"  checksum {tag}: got={got} want={CS_FIXED[tag]}")
                    ok = False
    print("① hexutil/checksum:", "PASS" if ok else "FAIL")
    return ok


def check_display(base_file, dump_dir):
    with open(base_file, "r", encoding="utf-8") as fh:
        base = json.load(fh)
    all_ok = True
    for dat in sorted(base):
        b = base[dat]
        if not isinstance(b, dict) or "lines" not in b:
            continue                                 # "enc" 等非行表键
        name = os.path.basename(dat)
        stem = os.path.splitext(name)[0]
        lines_file = os.path.join(dump_dir, stem + ".lines")
        meta_file = lines_file + ".meta"
        if not (os.path.exists(lines_file) and os.path.exists(meta_file)):
            print(f"③ display {name}: FAIL 缺少 dump 输出")
            all_ok = False
            continue
        with open(lines_file, "r", encoding="utf-8", newline="") as fh:
            native = fh.read().split("\n")
        if native and native[-1] == "":
            native.pop()
        with open(meta_file, "r", encoding="ascii") as fh:
            native_meta = []
            for ln in fh.read().splitlines():
                xy = ln.split()
                native_meta.append((int(xy[0]), int(xy[1])))
        py_lines = b["lines"]
        py_meta = []
        for m in b["meta"]:
            py_meta.append((m[0], m[1]))
        if len(native) != len(py_lines):
            print(f"③ display {name}: FAIL 行数 {len(native)} vs {len(py_lines)}")
            all_ok = False
            continue
        bad = 0
        i = 0
        for a in native:
            if a != py_lines[i]:
                if bad < 3:
                    print(f"   行 {i} 差异: native={a!r} py={py_lines[i]!r}")
                bad += 1
            i += 1
        mbad = 0
        j = 0
        for a in native_meta:
            if a != py_meta[j]:
                mbad += 1
            j += 1
        if bad == 0 and mbad == 0:
            print(f"③ display {name}: PASS ({len(py_lines)} 行)")
        else:
            print(f"③ display {name}: FAIL 文本差异 {bad} / 元数据差异 {mbad}")
            all_ok = False
    print("③ display:", "PASS" if all_ok else "FAIL")
    return all_ok


def check_txt(base_file, dump_dir):
    """④ 保存 txt 整文件字节对拍：native dump（anno+CRLF+open 行）vs PyCom plain_text
    （\\n join 归一化为 SNCom 的 CRLF+尾行口径——EOL 差异是已知有意分歧）。"""
    with open(base_file, "r", encoding="utf-8") as fh:
        base = json.load(fh)
    all_ok = True
    n_cmp = 0
    for dat in sorted(base):
        b = base[dat]
        if not isinstance(b, dict) or "txt" not in b:
            continue
        n_cmp += 1
        name = os.path.basename(dat)
        stem = os.path.splitext(name)[0]
        p = os.path.join(dump_dir, stem + ".savetxt")
        if not os.path.exists(p):
            print(f"④ txt {name}: FAIL 缺少 dump 输出")
            all_ok = False
            continue
        with open(p, "rb") as fh:
            native = fh.read()
        want = b["txt"].encode("utf-8")
        if native == want:
            continue
        nl = native.decode("utf-8", "replace").split("\r\n")
        wl = b["txt"].split("\r\n")
        shown = False
        for i, (x, y) in enumerate(zip(nl, wl)):
            if x != y:
                print(f"④ txt {name}: FAIL 行 {i} 差异\n    native={x!r}\n    py    ={y!r}")
                shown = True
                break
        if not shown:
            print(f"④ txt {name}: FAIL 行数 native={len(nl)} py={len(wl)}")
        all_ok = False
    print(f"④ save txt:", "PASS" if all_ok else "FAIL", f"({n_cmp} 语料)")
    return all_ok


def check_enc(base_file, dump_dir):
    """⑤ 扩编码对拍：向量（仅 Python 可干净解码的序列）→ 两侧增量解码码点一致。"""
    with open(base_file, "r", encoding="utf-8") as fh:
        base = json.load(fh)
    enc_res = base.get("enc", {})
    all_ok = True
    for enc in sorted(enc_res):
        fn = ("%s.dec.txt" % enc) if enc.startswith("mix_") else ("enc_%s.dec.txt" % enc)
        p = os.path.join(dump_dir, fn)
        if not os.path.exists(p):
            print(f"⑤ enc {enc}: FAIL 缺少 dump 输出")
            all_ok = False
            continue
        with open(p, "rb") as fh:
            native = fh.read()
        want = enc_res[enc].encode("utf-8")
        if native == want:
            print(f"⑤ enc {enc}: PASS ({len(want)} B)")
        else:
            nl = native.decode("utf-8", "replace")
            wl = enc_res[enc]
            at = 0
            for i, (x, y) in enumerate(zip(nl, wl)):
                if x != y:
                    at = i
                    break
            print(f"⑤ enc {enc}: FAIL 首差异 codepoint {at}: "
                  f"native={nl[at:at + 4]!r} py={wl[at:at + 4]!r} "
                  f"(len {len(nl)} vs {len(wl)})")
            all_ok = False
    print("⑤ encodings:", "PASS" if all_ok else "FAIL")
    return all_ok


def check_msini(base_file, dump_dir):
    """⑥ 多条 ini 导入对拍：解析核（真实 sscom51.ini + 畸形用例）vs PyCom _parse_ms_rows。"""
    with open(base_file, "r", encoding="utf-8") as fh:
        exp = json.load(fh)
    all_ok = True
    for tag in ("real", "edge"):
        p = os.path.join(dump_dir, "ms_%s.json" % tag)
        if not os.path.exists(p):
            print(f"⑥ msini {tag}: FAIL 缺少 dump 输出")
            all_ok = False
            continue
        with open(p, "r", encoding="utf-8") as fh:
            native = json.load(fh)
        want = exp.get(tag) or []
        if native == want:
            print(f"⑥ msini {tag}: PASS ({len(want)} 行)")
            continue
        print(f"⑥ msini {tag}: FAIL native={len(native)} 行 py={len(want)} 行")
        for i in range(min(len(native), len(want))):
            if native[i] != want[i]:
                print(f"    行 {i}:\n      native={native[i]}\n      py    ={want[i]}")
                break
        all_ok = False
    print("⑥ multisend ini:", "PASS" if all_ok else "FAIL")
    return all_ok


def main():
    hex_file = os.path.join(TOOLS, "_cc_hex.txt")
    base_file = os.path.join(TOOLS, "_cc_base.json")
    ms_file = os.path.join(TOOLS, "_cc_ms.json")
    dump_dir = os.path.join(TOOLS, "_cc_dump")
    ok = check_hex(hex_file)
    ok = check_display(base_file, dump_dir) and ok
    ok = check_txt(base_file, dump_dir) and ok
    ok = check_enc(base_file, dump_dir) and ok
    if os.path.exists(ms_file):
        ok = check_msini(ms_file, dump_dir) and ok
    print("== 总结果 ==", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
