# -*- coding: utf-8 -*-
"""PyCom 基准生成（对拍 ⑥ 多条 ini 导入）。由 tools\\runcross.bat 调用，无命令行参数。
用 Settings._read_ini_keys + _parse_ms_rows 解析 真实 sscom51.ini 与 畸形用例 ini → 基准 JSON。
"""
import json
import os
import sys

sys.path.insert(0, r"F:\projects\sscom二次分析开发\Develop\win64")
from app.core.settings import Settings          # noqa: E402


def rows_of(path):
    keys = Settings._read_ini_keys(None, path)   # self 未用，None 直调
    if keys is None:
        return None
    return Settings._parse_ms_rows(None, keys)


def main():
    real = r"F:\projects\sscom二次分析开发\sscom5131\sscom51.ini"
    edge = r"F:\projects\sscom二次分析开发\sncom\develop\tools\_cc_dump\ms_edge.ini"
    result = {"real": rows_of(real), "edge": rows_of(edge)}
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
    print("msbaseline: real=%s edge=%s" % (
        len(result["real"] or []), len(result["edge"] or [])), file=sys.stderr)


if __name__ == "__main__":
    main()
