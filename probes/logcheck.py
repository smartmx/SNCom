import io, glob, os
f = sorted(glob.glob("dist/SNCom-Log-*.txt"), key=os.path.getmtime)[-1]
raw = io.open(f, "rb").read().decode("gbk", "replace")
lines = raw.split("\r\n")
tx = sum(1 for l in lines if "\u53d1\u2192\u25c7" in l)
rx = sum(1 for l in lines if "\u6536\u2190\u25c6" in l)
print("log=%s bytes=%d lines=%d  TX=%d RX=%d" % (os.path.basename(f), os.path.getsize(f), len(lines), tx, rx))
print("first:", lines[0][:80])
print("sample loopback:", [l[:70] for l in lines[:3]])
print("has spike render lines:", any("spike" in l for l in lines))