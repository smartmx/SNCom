# -*- coding: utf-8 -*-
"""从 assets/sncom-icon.svg 生成 assets/sncom.ico（打包用）。

纯标准库写 ICO（Vista+ 支持内嵌 PNG 帧），渲染用 QtSvg，不依赖 Pillow。
用法: python tools/make_ico.py
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import QBuffer, QByteArray, QRect, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

SIZES = (16, 24, 32, 48, 64, 128, 256)


def render_pngs(svg_path: Path) -> list[tuple[int, bytes]]:
    renderer = QSvgRenderer(str(svg_path))
    if not renderer.isValid():
        raise SystemExit("SVG 无效: %s" % svg_path)
    pngs = []
    for size in SIZES:
        img = QImage(size, size, QImage.Format.Format_ARGB32)
        img.fill(Qt.GlobalColor.transparent)
        p = QPainter(img)
        renderer.render(p, QRect(0, 0, size, size))
        p.end()
        ba = QByteArray()
        buf = QBuffer(ba)
        buf.open(QBuffer.OpenModeFlag.WriteOnly)
        img.save(buf, "PNG")
        buf.close()
        pngs.append((size, bytes(ba)))
    return pngs


def write_ico(path: Path, pngs: list[tuple[int, bytes]]) -> None:
    # ICO 头：6 字节目录 + 每帧 16 字节；PNG 帧数据紧随其后
    header = struct.pack("<HHH", 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    directory = b""
    body = b""
    for size, png in pngs:
        directory += struct.pack(
            "<BBBBHHII",
            size if size < 256 else 0, size if size < 256 else 0,
            0, 0, 1, 32, len(png), offset,
        )
        body += png
        offset += len(png)
    path.write_bytes(header + directory + body)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    svg = root / "assets" / "sncom-icon.svg"
    ico = root / "assets" / "sncom.ico"
    app = QGuiApplication(sys.argv[:1])
    pngs = render_pngs(svg)
    write_ico(ico, pngs)
    print("written:", ico, ico.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
