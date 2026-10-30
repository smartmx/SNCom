import codecs, io, os, subprocess, sys
# 路径约定（§60 审查后定式）：本目录探针使用绝对字面量路径（机器本地；Mimosa
# CWE-22 规则对 __file__ 衍生/chdir 污点路径一律判高危，字面量可根治）。
# 换机时需同步修改下面的 D。
D = r"F:\projects\sscom二次分析开发\SNCom\probes"
data = bytearray()
for lead in range(0x81, 0xFF):
    for trail in range(0x100):
        data += bytes([lead, trail])
data += bytes([0x80, 0xFF, 0x41])
data += bytes([0x81])                      # 末尾悬空 lead
open(os.path.join(D, "gbk_all.bin"), "wb").write(bytes(data))
print("corpus bytes:", len(data))

def py_ref(chunk):
    dec = codecs.getincrementaldecoder("gbk")("replace")
    out = []
    for off in range(0, len(data), chunk):
        out.append(dec.decode(bytes(data[off:off+chunk]), False))
    out.append(dec.decode(b"", True))
    return " ".join("%04X" % ord(c) for c in "".join(out))

fails = 0
for chunk in (0, 1, 3, 7, 4096, 65536):
    r = subprocess.run([os.path.join(D, "decfuzz.exe"), os.path.join(D, "gbk_all.bin"), "gbk", str(chunk)],
                       capture_output=True, text=True)
    cpp = r.stdout.strip()
    ref = py_ref(chunk if chunk else len(data))
    same = (cpp == ref)
    print("chunk=%-6s %s" % (chunk, "MATCH" if same else "DIFF"))
    if not same:
        fails += 1
        a = cpp.split(); b = ref.split()
        for i,(x,y) in enumerate(zip(a,b)):
            if x != y:
                print("   first diff at index %d: cpp=%s py=%s" % (i, x, y)); break
        print("   len cpp=%d py=%d" % (len(a), len(b)))
print("RESULT:", "ALL MATCH" if fails == 0 else "%d chunk-size mismatches" % fails)
