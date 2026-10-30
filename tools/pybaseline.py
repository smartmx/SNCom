# -*- coding: utf-8 -*-
"""PyCom 基准生成（对拍 ③ display 行表 + ④ 保存 txt + ⑤ 扩编码）。
由 tools\\runcross.bat 调用，无命令行参数。
职责：枚举 DAT 语料 → LineBuilder（cwd=PyCom 工程，app 包可直接导入）→ 基准 JSON
（行表/meta + 保存txt口径 + 编码向量解码文本）。编码向量 .bin 由 gen_fixtures.txt 先行生成。
"""
import codecs
import json
import os
import sys

sys.path.insert(0, r"F:\projects\sscom二次分析开发\Develop\win64")
from app.core.display import LineBuilder, dir_marker          # noqa: E402

DUMP_DIR = r"F:\projects\sscom二次分析开发\sncom\develop\tools\_cc_dump"
ENCS = ["gbk", "big5", "gb18030", "shift_jis", "euc_jp", "euc_kr", "cp1252"]


def main():
    dat_dir = r"F:\projects\sscom二次分析开发\sscom5131"
    dats = []
    for f in os.listdir(dat_dir):
        p = os.path.join(dat_dir, f)
        if f.lower().endswith(".dat") and os.path.getsize(p) > 0:
            dats.append(p)
    result = {}
    for dat in sorted(dats):
        with open(dat, "rb") as fh:
            data = fh.read()
        b = LineBuilder(encoding="gbk", hex_mode=False, split=False, split_ms=20)
        step = 1024
        for off in range(0, len(data), step):      # 1KB 分块：跨包半截序列口径同 native
            b.feed("rx", data[off:off + step], 1000.0, off)
            b.maybe_flush(1001.0)
        b.flush()
        lines = []
        meta = []
        i = 0
        while i < b.line_count():
            l = b.line_at(i)
            lines.append(l.text)
            meta.append([l.nbytes, l.start_abs])
            i += 1
        entry = {"lines": lines, "meta": meta}
        # ④ 保存 txt 口径（大文件跳过：与 native 侧 LSS 1000000 守卫对称）
        if len(data) < 1000000:
            bt = LineBuilder(encoding="gbk", hex_mode=False, split=True, split_ms=20)
            for off in range(0, len(data), step):
                bt.feed("rx", data[off:off + step], 1000.0, off)
                bt.maybe_flush(1001.0)
            bt.flush()
            rows = [l.prefix + (dir_marker(l.dir) if l.prefix else "") + l.text
                    for l in bt.visible()]         # visible() = 行表 + 未收尾行（与保存口径一致）
            entry["txt"] = "\r\n".join(rows) + ("\r\n" if rows else "")
        result[dat] = entry
    # ⑤ 编码向量解码（Python 增量解码器，1KB 分块同 native）
    enc_res = {}
    for enc in ENCS:
        vp = os.path.join(DUMP_DIR, "enc_%s.bin" % enc)
        if not os.path.exists(vp):
            continue
        with open(vp, "rb") as fh:
            vdata = fh.read()
        dec = codecs.getincrementaldecoder(enc)(errors="replace")
        out = []
        for off in range(0, len(vdata), 1024):
            out.append(dec.decode(vdata[off:off + 1024]))
        out.append(dec.decode(b"", final=True))
        enc_res[enc] = "".join(out)
    # ⑤b 混合向量（H-3）：非法序列注入，1 字节分块喂（最苛刻边界），errors="replace"
    for enc in ["gbk", "big5", "shift_jis", "euc_jp", "euc_kr", "cp1252", "gb18030"]:
        vp = os.path.join(DUMP_DIR, "mix_%s.bin" % enc)
        if not os.path.exists(vp):
            continue
        with open(vp, "rb") as fh:
            vdata = fh.read()
        dec = codecs.getincrementaldecoder(enc)(errors="replace")
        out = []
        for off in range(0, len(vdata), 1):     # 1 字节分块
            out.append(dec.decode(vdata[off:off + 1]))
        out.append(dec.decode(b"", final=True))
        enc_res["mix_" + enc] = "".join(out)
    result["enc"] = enc_res
    # 基准 JSON 走 stdout（runcross.bat 重定向落盘）——.py 内零写文件；状态行走 stderr
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
    print("baseline:", len(dats), "files +", len(enc_res), "encodings", file=sys.stderr)


if __name__ == "__main__":
    main()
