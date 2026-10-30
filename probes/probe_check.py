# -*- coding: utf-8 -*-
# probe_check.py - 探针门禁（§94/W2 机制化）：批跑或提交前先跑本脚本。
#   背景（八轮审查 W2）：verify_r87 文件头重写时整条 import 被吃掉，脚本一跑就
#   NameError，批跑显示 no summary，而"自述清单仍写 3/3"——守卫已死却看着是绿的。
#   py_compile 只查语法、查不出 NameError，故再加一层 AST 级"用了却没 import"检测
#   （覆盖 ctypes/win32*/PIL 这类探针常用模块根名）。退出码非 0 = 有坏探针。
import ast
import builtins
import py_compile
import sys
from pathlib import Path

ROOT = Path(__file__).parent
# 探针里常以裸根名出现的外部模块——用到却没 import 必然 NameError（W2 同类）
WATCH = {"ctypes", "win32gui", "win32api", "win32ui", "win32con", "win32process",
         "win32com", "Image", "ImageDraw", "ImageFont", "numpy", "requests"}
BUILTINS = set(dir(builtins)) | {"__name__", "__file__", "__doc__", "__spec__",
                                 "__loader__", "__package__"}


def imported_roots(tree):
    out = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Import):
            for a in n.names:
                out.add((a.asname or a.name).split(".")[0])
        elif isinstance(n, ast.ImportFrom):
            for a in n.names:
                if a.name != "*":
                    out.add(a.asname or a.name)
    return out


def defined_names(tree):
    out = set()
    for n in ast.walk(tree):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            out.add(n.name)
        elif isinstance(n, ast.Assign):
            for t in n.targets:
                out |= {x.id for x in ast.walk(t) if isinstance(x, ast.Name)}
        elif isinstance(n, (ast.AugAssign, ast.AnnAssign)):
            out |= {x.id for x in ast.walk(n.target) if isinstance(x, ast.Name)}
        elif isinstance(n, (ast.For, ast.AsyncFor, ast.comprehension)):
            t = n.target
            out |= {x.id for x in ast.walk(t) if isinstance(x, ast.Name)}
        elif isinstance(n, ast.With):
            for item in n.items:
                if item.optional_vars:
                    out |= {x.id for x in ast.walk(item.optional_vars)
                            if isinstance(x, ast.Name)}
        elif isinstance(n, ast.ExceptHandler) and n.name:
            out.add(n.name)
        elif isinstance(n, ast.NamedExpr):
            out.add(n.target.id)
    return out


def used_roots(tree):
    out = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name):
            out.add(n.value.id)
        elif isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load):
            out.add(n.id)
    return out


def all_params(tree):
    out = set()
    for n in ast.walk(tree):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
            a = n.args
            for arg in list(a.posonlyargs) + list(a.args) + list(a.kwonlyargs):
                out.add(arg.arg)
            if a.vararg:
                out.add(a.vararg.arg)
            if a.kwarg:
                out.add(a.kwarg.arg)
    return out


def main():
    files = sorted(p for p in ROOT.glob("*.py")
                   if p.name not in ("probe_check.py", "docs_check.py")
                   and not p.name.startswith("_"))
    bad = []
    for p in files:
        try:
            py_compile.compile(str(p), doraise=True)
        except py_compile.参照实现pileError as e:
            bad.append("%s: 语法错误 %s" % (p.name, e))
            continue
        try:
            tree = ast.parse(p.read_text(encoding="utf-8"))
        except SyntaxError as e:                      # py_compile 已拦，双保险
            bad.append("%s: 语法错误 %s" % (p.name, e))
            continue
        used = used_roots(tree)
        missing = (used & WATCH) - imported_roots(tree) - defined_names(tree)
        if missing:
            bad.append("%s: 用了却没 import -> %s（W2 同类，一跑必 NameError）"
                       % (p.name, ", ".join(sorted(missing))))
        # §97 升级：通用未定义全局名（如 _bbox_diff——S8 抖动路径才触发，静默潜伏多轮）
        unknown = used - imported_roots(tree) - defined_names(tree) - all_params(tree) \
            - BUILTINS
        # 属性根 + 名字都查；排除常见动态注入白名单（本库无，留空）
        unknown = {u for u in unknown if not u.startswith("__")}
        if unknown:
            bad.append("%s: 未定义的名字 -> %s（NameError 潜伏；若为动态注入请改用显式赋值）"
                       % (p.name, ", ".join(sorted(unknown))))
    if bad:
        print("probe_check: FAIL（%d/%d）" % (len(bad), len(files)))
        for b in bad:
            print("  " + b)
        return 1
    print("probe_check: OK（%d 个 .py 语法+import+未定义名 全过）" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
