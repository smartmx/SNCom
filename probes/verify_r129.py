# -*- coding: utf-8 -*-
# §129 E2E 探针：协议文件发送（XMODEM/YMODEM 发送端）
#   前置：SNCom(TCP 客户端) ↔ 本探针(Python TCP 服务端 + 参考收端)
#   A1 YMODEM 全批：块 0 名/长 → 第二踢 → 1K 数据 → EOT 双确认 → 空块 0 收批，字节全等
#   A2 XMODEM(CRC 1K) 全批：字节全等（尾部 0x1A 填充）
#   B1 CAN 负例：对端 CAN CAN 中途取消 → 会话结束且无后续块
#   C1 会话期协议字节不进转存日志（规则 18：关应用后读）
import os, shutil, socket, subprocess, sys, time
import win32gui, win32con, win32process, win32api

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("SNCOM_EXE") or os.path.abspath(
    os.path.join(HERE, os.pardir, "develop", "dist", "sncom.exe"))
BIN = os.path.join(HERE, "_probe", "out", "proto129")
PORT = 5331
res = []
def chk(name, ok, detail=""):
    res.append((name, bool(ok)))
    print(("[PASS] " if ok else "[FAIL] ") + name + (" | " + detail if detail else ""), flush=True)

# IDC 枚举（mainwindow.cpp 匿名枚举，稳定值）：...IDC_ASCII_LIST=13, PROTO_MODE=14 FILE=15
# OPEN=16 START=17 CANCEL=18 STATUS=19；§131 追加 IDC_PROTO_TIMEOUT=29
ID_PROTO_FILE, ID_PROTO_START, ID_PROTO_CANCEL, ID_PROTO_STATUS = 15, 17, 18, 19
ID_PROTO_SHOW, ID_PROTO_TIMEOUT = 28, 29
BM_CLICK = 0x00F5

shutil.rmtree(BIN, ignore_errors=True)
os.makedirs(BIN)
shutil.copy(SRC, os.path.join(BIN, "sncom.exe"))
with open(os.path.join(BIN, "sncom.json"), "w", encoding="utf-8") as f:
    f.write('{"mode":"tcp_client","remote_ip":"127.0.0.1","remote_port":%d,"encoding":"gbk"}' % PORT)

# 5KB 测试文件（含 0x04/0x18/0x1A 值——数据域控制字节碰撞素材）
data = bytes(((i * 7 + 13 + (i >> 8)) & 0xFF) for i in range(5 * 1024))
fname = "fw129.bin"
fpath = os.path.join(BIN, fname)
with open(fpath, "wb") as f:
    f.write(data)

proc = subprocess.Popen([os.path.join(BIN, "sncom.exe")], cwd=BIN)
def find_main():
    out = []
    def cb(h, l):
        if win32gui.GetClassName(h) == "SNComMain" and \
           win32process.GetWindowThreadProcessId(h)[1] == proc.pid:
            out.append(h)
        return True
    win32gui.EnumWindows(cb, None)
    return out[0] if out else 0
main = 0
for _ in range(40):
    main = find_main()
    if main: break
    time.sleep(0.25)
assert main
time.sleep(2.5)

def kids_of(h):
    out = []
    def cb(c, l):
        out.append(c)
        return True
    win32gui.EnumChildWindows(h, cb, None)
    return out
def child_by_id(parent, cid):
    for c in kids_of(parent):
        if win32gui.GetDlgCtrlID(c) == cid:
            return c
    return 0

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT)); srv.listen(1)
win32api.PostMessage(main, win32con.WM_COMMAND, 3066, 0)     # 连接（r126 先例）
srv.settimeout(8)
conn, _ = srv.accept()
chk("前置：TCP 连接建立", conn is not None)

# 打开扩展窗（IDC_BTN_EXPAND=3012，IDC_BASE=3000 序列；文本匹配跨进程不可靠）
ext_btn = win32gui.GetDlgItem(main, 3012)
win32gui.SendMessage(ext_btn, BM_CLICK, 0, 0)
time.sleep(0.8)

ext = 0
def find_ext(h, l):
    global ext
    if win32gui.GetClassName(h) == "SNComExt" and \
       win32process.GetWindowThreadProcessId(h)[1] == proc.pid:
        ext = h
    return True
win32gui.EnumWindows(find_ext, None)
st = child_by_id(ext, ID_PROTO_STATUS)
chk("前置：扩展窗与协议页控件", bool(ext) and bool(child_by_id(ext, ID_PROTO_FILE)) and
    bool(child_by_id(ext, ID_PROTO_START)) and bool(st))
tab = child_by_id(ext, 1)                              # IDC_EXT_TAB=1

# 切到第 5 页「协议发送」：r89 先例=TCM_SETCURSEL(0x130C) + WM_APP+0x111（应用的切页显隐
# 消息，:4376 处理器）。页签点击跨进程不生效（实测）且 TCM_GETITEMRECT 传指针会崩目标（§108）。
win32gui.SendMessage(tab, 0x130C, 4, 0)                # TCM_SETCURSEL=4
win32gui.SendMessage(ext, win32con.WM_APP + 0x111, 4, 0)
time.sleep(0.6)
on_proto = (win32gui.SendMessage(tab, 0x130B, 0, 0) == 4) and win32gui.IsWindowVisible(st)
_ms = child_by_id(ext, 20)
_fn = child_by_id(ext, 12)
chk("前置：切到「协议发送」页（TCM_SETCURSEL + WM_APP+0x111，r89 先例）", on_proto,
    "cursel=%d st_vis=%d ms_vis=%d calc_vis=%d ext=%s" % (
        win32gui.SendMessage(tab, 0x130B, 0, 0), win32gui.IsWindowVisible(st),
        win32gui.IsWindowVisible(_ms), win32gui.IsWindowVisible(_fn), hex(ext)))

def status_text():
    return win32gui.GetWindowText(st)
def click_start():
    win32api.PostMessage(ext, win32con.WM_COMMAND,
                         win32api.MAKELONG(ID_PROTO_START, 0),   # BN_CLICKED=0
                         child_by_id(ext, ID_PROTO_START))
win32gui.SendMessage(child_by_id(ext, ID_PROTO_FILE), win32con.WM_SETTEXT, 0, fpath)

# ---------------- Python 参考收端（帧态感知；与 protofileself VPeer 同构） ----------------
class Rx:
    def __init__(self, kind):
        self.kind = kind            # 'x' / 'y'
        self.acc = bytearray()
        self.out = bytearray()
        self.name = b""
        self.size = 0
        self.got = 0
        self.expect = 0 if kind == "y" else 1
        self.hdr = False
        self.finalc = False
        self.eot = 0
        self.done = False
        self.naks = 0
    def feed(self, d, conn):
        for b in d:
            if self.done:
                return
            if not self.acc:                       # 帧间：控制字节（数据域 04/18 不受影响）
                if b == 0x04:
                    self.eot += 1
                    if self.eot == 1:
                        conn.sendall(b"\x15")      # 双确认第一步
                    else:
                        conn.sendall(b"\x06")
                        if self.kind == "y" and not self.finalc:
                            conn.sendall(b"C")
                            self.finalc = True
                        else:
                            self.done = True
                    continue
                if b == 0x18:
                    return                          # 对端取消：静默
                if b not in (0x01, 0x02):
                    continue                        # 乱码前导
            self.acc.append(b)
            if len(self.acc) < 3:
                continue
            sz = 1024 if self.acc[0] == 0x02 else 128
            need = 3 + sz + 2
            if len(self.acc) >= need:
                f = bytes(self.acc[:need])
                del self.acc[:need]
                self.on_frame(f, conn)
    def on_frame(self, f, conn):
        num, cnum = f[1], f[2]
        sz = 1024 if f[0] == 0x02 else 128
        if num == 0 and self.kind == "y" and self.finalc:      # 收批空块 0
            crc = 0
            for i in range(3, 3 + sz):
                crc ^= f[i] << 8
                for _ in range(8):
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else ((crc << 1) & 0xFFFF)
            if f[3 + sz] == (crc >> 8) and f[4 + sz] == (crc & 0xFF):
                conn.sendall(b"\x06")
                self.done = True
            else:
                conn.sendall(b"\x15"); self.naks += 1
            return
        crc = 0
        for i in range(3, 3 + sz):
            crc ^= f[i] << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else ((crc << 1) & 0xFFFF)
        if num != (255 - cnum) & 0xFF or f[3 + sz] != (crc >> 8) or f[4 + sz] != (crc & 0xFF):
            conn.sendall(b"\x15"); self.naks += 1
            return
        if num == (self.expect & 0xFF):
            if num == 0 and self.kind == "y" and not self.hdr:
                p = 3
                nm = bytearray()
                while f[p]: nm.append(f[p]); p += 1
                p += 1
                szs = bytearray()
                while f[p]: szs.append(f[p]); p += 1
                self.name = bytes(nm); self.size = int(szs.decode() or 0)
                self.hdr = True
                self.out.clear()
            else:
                take = sz
                if self.kind == "y" and self.got + take > self.size:
                    take = self.size - self.got
                self.out += f[3:3 + take]
                self.got += take
            self.expect += 1
            conn.sendall(b"\x06")
            if num == 0 and self.kind == "y":
                if self.finalc:
                    self.done = True               # 收批空块 0 已 ACK → 批结束
                else:
                    conn.sendall(b"C")             # 块 0 ACK 后第二踢
        elif num == (self.expect - 1) & 0xFF:
            conn.sendall(b"\x06")
        else:
            conn.sendall(b"\x15"); self.naks += 1

conn.settimeout(0.2)

# A1 YMODEM 全批（协议下拉 id=14 → CB_SETCURSEL 1）
win32gui.SendMessage(child_by_id(ext, 14), 0x014E, 1, 0)   # CB_SETCURSEL 1（YMODEM）
click_start()
time.sleep(0.3)
r = Rx("y")
conn.sendall(b"C")                                     # 首踢
t0 = time.time()
while time.time() - t0 < 20 and not r.done:
    try:
        d = conn.recv(65536)
        if d:
            r.feed(d, conn)
    except socket.timeout:
        pass
okA1 = r.done and r.out == data and r.name == fname.encode() and r.size == len(data)
chk("A1 YMODEM 全批：块0 名/长 + 1K 数据 + 双确认收批，字节全等", okA1,
    "got=%d name=%s size=%d naks=%d" % (len(r.out), r.name.decode(errors="replace"),
                                        r.size, r.naks))
time.sleep(0.6)

# A2 XMODEM（协议下拉 id=14，CB_SETCURSEL 0）
cmb = child_by_id(ext, 14)
win32gui.SendMessage(cmb, 0x014E, 0, 0)                # CB_SETCURSEL 0（XMODEM）
click_start()
time.sleep(0.3)
r2 = Rx("x")
conn.sendall(b"C")
t0 = time.time()
while time.time() - t0 < 20 and not r2.done:
    try:
        d = conn.recv(65536)
        if d:
            r2.feed(d, conn)
    except socket.timeout:
        pass
want = data
while len(want) % 1024:
    want += b"\x1a"
chk("A2 XMODEM(CRC 1K) 全批：字节全等（尾部 0x1A 填充）", r2.done and r2.out == want,
    "got=%d naks=%d" % (len(r2.out), r2.naks))
time.sleep(0.6)

# B1 CAN 负例：不 ACK 首块（发端停等悬置、tx 已排空）→ 立即 CAN → 必然静默
# 注意：XMODEM 1K 帧首字节是 STX(0x02)，帧起点须双字节识别
click_start()
time.sleep(0.3)
conn.sendall(b"C")
acc = bytearray()
aborted_seen = False
sent_after_can = 0
blocks_seen = 0
after_hex = bytearray()
t0 = time.time()
while time.time() - t0 < 10:
    try:
        d = conn.recv(65536)
    except socket.timeout:
        d = b""
    if d:
        for b in d:
            if aborted_seen:
                if len(after_hex) < 12:
                    after_hex.append(b)
                sent_after_can += 1
                continue
            if not acc and b not in (0x01, 0x02):
                continue
            acc.append(b)
            if len(acc) >= 3 + 1024 + 2:
                blocks_seen += 1
                acc.clear()
                conn.sendall(b"\x18\x18")              # 不 ACK，立即取消（停等悬置态）
                aborted_seen = True
    if aborted_seen:
        t1 = time.time()
        while time.time() - t1 < 1.5:                  # 再观察 1.5s 确认无后续块
            try:
                d2 = conn.recv(65536)
                if d2:
                    for b in d2:
                        if len(after_hex) < 12:
                            after_hex.append(b)
                    sent_after_can += len(d2)
            except socket.timeout:
                break
        break                                          # 取消后观察完毕即退出
txt = status_text()
okB1 = aborted_seen and sent_after_can == 0 and ("取消" in txt or "结束" in txt)
chk("B1 CAN 负例：对端取消 → 会话结束且无后续块", okB1,
    "blocks=%d after_can=%d hex=%s status=%s" % (blocks_seen, sent_after_can,
                                                 after_hex.hex(), txt[:40]))

# C1 会话期协议字节不进转存日志（规则 18：关闭应用后读）
rxlog_btn = win32gui.GetDlgItem(main, 3039)            # IDC_CHK_RXFILE（r126 先例）
c1_log = None
if rxlog_btn:
    win32gui.SendMessage(rxlog_btn, BM_CLICK, 0, 0)    # 开转存
    time.sleep(0.5)
    click_start()
    time.sleep(0.3)
    conn.sendall(b"C")
    time.sleep(2.5)                                    # 放任块流出/超时重发（日志须保持干净）
    win32gui.SendMessage(rxlog_btn, BM_CLICK, 0, 0)    # 关转存
    time.sleep(0.5)
    logs = [os.path.join(BIN, x) for x in os.listdir(BIN) if x.startswith("SNCom-Log")]
    c1_log = max(logs, key=os.path.getmtime) if logs else None
    # §131 修：C1 结束必须取消会话——旧版遗留会话使 C2 的 click_start 被 protoActive_ 拒绝，
    # C2 实际接手 C1 残会话（重发在途块 + 新收端）形成非真实时序（双 ACK 跳块假红）
    win32api.PostMessage(ext, win32con.WM_COMMAND, ID_PROTO_CANCEL,
                         child_by_id(ext, ID_PROTO_CANCEL))
    time.sleep(0.8)
    # §131 修2：排空 conn 积压——C1 故意不 ACK，发端重发的块全部积压在 TCP 缓冲没人读；
    # 不清会让 C2 的收端重复读到（DUP 补 ACK → 发端 EOT 应答被迟到 ACK 混淆 → EOT#2 不发）
    conn.settimeout(0.2)
    last = time.time()
    while time.time() - last < 0.4:
        try:
            if conn.recv(65536):
                last = time.time()
        except socket.timeout:
            break

# C2 续修5 正例：勾选「将收发内容显示到窗口」→ 转存日志出现块流量（入显示=入转存管线）
c2_log = None
if rxlog_btn and ext:
    conn.settimeout(0.2)                               # §131 修2：双保险排空（C1 残余块流）
    last = time.time()
    while time.time() - last < 0.4:
        try:
            if conn.recv(65536):
                last = time.time()
        except socket.timeout:
            break
    win32gui.SendMessage(win32gui.GetDlgItem(ext, 28), BM_CLICK, 0, 0)   # 勾选显示
    time.sleep(0.3)
    win32gui.SendMessage(rxlog_btn, BM_CLICK, 0, 0)    # 开转存
    time.sleep(0.5)
    click_start()
    time.sleep(0.3)
    r3 = Rx("x")
    conn.sendall(b"C")
    t0 = time.time()
    while time.time() - t0 < 20 and not r3.done:
        try:
            d = conn.recv(65536)
            if d:
                r3.feed(d, conn)
        except socket.timeout:
            pass
    time.sleep(0.8)                                    # 等 TX 侧显示 feed 落盘
    win32gui.SendMessage(rxlog_btn, BM_CLICK, 0, 0)    # 关转存
    time.sleep(0.5)
    logs = [os.path.join(BIN, x) for x in os.listdir(BIN) if x.startswith("SNCom-Log")]
    logs = [x for x in logs if x != c1_log]
    c2_log = max(logs, key=os.path.getmtime) if logs else None
    chk("C2 会话终态：EOT 双确认正常收尾（eot=2, done）", r3.done and r3.eot == 2,
        "done=%s out=%d eot=%d naks=%d status=%s" % (
            r3.done, len(r3.out), r3.eot, r3.naks,
            win32gui.GetWindowText(st)[:30]))

# C3 R23-P2-2：会话期点「发 送」→ 闸拒绝注入、会话不受影响
#   （发送框塞 "INJ" 点发送——若闸失效，3 字节插入停等流，参考收端解析必乱 → done/字节全等必红）
send_btn = win32gui.GetDlgItem(main, 3058)             # IDC_BTN_SEND
send_edit = win32gui.GetDlgItem(main, 3057)            # IDC_EDIT_SEND
okC3 = False
c3d = "no send btn/edit"
if send_btn and send_edit:
    click_start()                                      # XMODEM（下拉仍 0，A2 遗留）
    time.sleep(0.3)
    r4 = Rx("x")
    conn.sendall(b"C")                                 # 首踢 → 首块 1K 发出（tx 排空悬置等 ACK）
    buf3 = bytearray()
    t0 = time.time()
    while time.time() - t0 < 6 and len(buf3) < 3 + 1024 + 2:
        try:
            d = conn.recv(65536)
            if d:
                buf3 += d                              # 先攒首帧不 ACK——停等悬置态最敏感窗口
        except socket.timeout:
            pass
    win32gui.SendMessage(send_edit, win32con.WM_SETTEXT, 0, "INJ")
    win32gui.SendMessage(send_btn, BM_CLICK, 0, 0)     # 会话期手工发送 → 应被闸拒绝
    time.sleep(0.5)
    r4.feed(bytes(buf3), conn)                         # 收端 ACK 首块 → 传输续行
    t0 = time.time()
    while time.time() - t0 < 25 and not r4.done:
        try:
            d = conn.recv(65536)
            if d:
                r4.feed(d, conn)
        except socket.timeout:
            pass
    want3 = data
    while len(want3) % 1024:
        want3 += b"\x1a"
    okC3 = r4.done and r4.out == want3
    c3d = "done=%s out=%d/%d" % (r4.done, len(r4.out), len(want3))
chk("C3 R23-P2-2 会话期点发送：注入被闸拒绝且会话不受影响", okC3, c3d)

# C4 §131：超时可设置——写 400ms 后无 ACK，重发间隔应近 0.4s（旧硬编码=3s）
ti = child_by_id(ext, 29)                              # IDC_PROTO_TIMEOUT（§131 追加末尾）
okC4 = False
c4d = "no timeout edit"
if ti:
    win32gui.SendMessage(ti, win32con.WM_SETTEXT, 0, "400")
    time.sleep(0.2)
    click_start()
    time.sleep(0.3)
    conn.sendall(b"C")
    f_times = []
    facc = bytearray()
    t0 = time.time()
    while time.time() - t0 < 3.0:                      # 3s 窗：400ms 超时应出约 7 帧
        try:
            d = conn.recv(65536)
        except socket.timeout:
            d = b""
        if d:
            facc += d
            while len(facc) >= 3 + 1024 + 2 and facc[0] == 0x02:
                f_times.append(round(time.time() - t0, 3))
                del facc[:3 + 1024 + 2]
    gaps = [round(f_times[i + 1] - f_times[i], 3) for i in range(len(f_times) - 1)]
    # §139（R26 期硬化）：0.8 上限对 WM_TIMER 合并过严（§131 本就预留「合并最坏 ~4.5s 一拍」；
    # 实测偶发单间隙 0.818=两拍合并）。改中位数判超时生效 + 容忍至多一拍合并（≤1.2s）：
    # 若超时回退 3s 硬编码，中位数≈3s 仍必红，判别力保留。
    g_sorted = sorted(gaps)
    med = g_sorted[len(g_sorted) // 2] if g_sorted else 0.0
    okC4 = (len(f_times) >= 3 and 0.2 <= med <= 0.65
            and all(g <= 1.2 for g in gaps) and sum(1 for g in gaps if g > 0.8) <= 1)
    c4d = "frames=%d gaps=%s med=%.2f" % (len(f_times), gaps, med)
    win32api.PostMessage(ext, win32con.WM_COMMAND, ID_PROTO_CANCEL,
                         child_by_id(ext, ID_PROTO_CANCEL))   # CAN CAN BS BS 收尾
    time.sleep(0.8)
chk("C4 §131 超时可设置：400ms → 无 ACK 时近 0.4s 重发", okC4, c4d)

# C5 §131：首包（踢后第一块）经事件驱动即时入显示管线——踢后 0.6s 截面转存已有块流量
c5_log = None
rxlog_btn2 = win32gui.GetDlgItem(main, 3039)           # IDC_CHK_RXFILE
if rxlog_btn2 and ext and ti:
    chk_show = win32gui.GetDlgItem(ext, 28)            # 确保「显示到窗口」为勾选态
    if win32gui.SendMessage(chk_show, 0x00F0, 0, 0) != 1:   # BM_GETCHECK != BST_CHECKED
        win32gui.SendMessage(chk_show, BM_CLICK, 0, 0)
        time.sleep(0.2)
    before = set(os.listdir(BIN))
    win32gui.SendMessage(rxlog_btn2, BM_CLICK, 0, 0)   # 开转存
    time.sleep(0.4)
    click_start()
    time.sleep(0.3)
    conn.sendall(b"C")                                 # 踢 → 首块应即时写出+入显示（不等 30ms 拍）
    time.sleep(0.6)
    win32gui.SendMessage(rxlog_btn2, BM_CLICK, 0, 0)   # 关转存
    time.sleep(0.4)
    win32api.PostMessage(ext, win32con.WM_COMMAND, ID_PROTO_CANCEL,
                         child_by_id(ext, ID_PROTO_CANCEL))
    time.sleep(0.8)
    logs = [os.path.join(BIN, x) for x in os.listdir(BIN)
            if x.startswith("SNCom-Log") and x not in before]
    c5_log = max(logs, key=os.path.getmtime) if logs else None
if c5_log:
    raw = open(c5_log, "rb").read()
    chk("C5 §131 首包即时入显示：踢后 0.6s 转存截面已有块流量", len(raw) > 1024 and b"\x02" in raw,
        "log=%dB 0x02x%d" % (len(raw), raw.count(2)))
else:
    chk("C5 §131 首包即时入显示：踢后 0.6s 转存截面已有块流量", False, "no c5 log")

# C6 §131 续：超时次数可设置——400ms + 次数=2 → 首发 + 2 次重发后「重发超限」Fail
ri = child_by_id(ext, 30)                              # IDC_PROTO_RETRIES（§131 续 追加末尾）
okC6 = False
c6d = "no retries edit"
if ri and ti:
    conn.settimeout(0.2)                               # §131 续修：清 C5 残留（C5 不读 conn，
    last = time.time()                                 #   块 1 + 取消序列堵在缓冲——同规则 21）
    while time.time() - last < 0.4:
        try:
            if conn.recv(65536):
                last = time.time()
        except socket.timeout:
            break
    win32gui.SendMessage(ti, win32con.WM_SETTEXT, 0, "400")     # 保持 400ms 快节奏
    win32gui.SendMessage(ri, win32con.WM_SETTEXT, 0, "2")
    time.sleep(0.2)
    click_start()
    time.sleep(0.3)
    conn.sendall(b"C")
    f6 = 0
    facc6 = bytearray()
    t0 = time.time()
    while time.time() - t0 < 3.5:                      # 3.5s 窗足够 3 帧 + Fail
        try:
            d = conn.recv(65536)
        except socket.timeout:
            d = b""
        if d:
            facc6 += d
            while len(facc6) >= 3 and facc6[0] != 0x02:
                del facc6[:1]                          # 跳过非 STX 前导（防御）
            while len(facc6) >= 3 + 1024 + 2 and facc6[0] == 0x02:
                f6 += 1
                del facc6[:3 + 1024 + 2]
    time.sleep(0.5)
    txt6 = win32gui.GetWindowText(child_by_id(ext, ID_PROTO_STATUS))
    okC6 = f6 == 3 and ("重发超限" in txt6)
    c6d = "frames=%d status=%s" % (f6, txt6[:40])
    win32gui.SendMessage(ri, win32con.WM_SETTEXT, 0, "10")      # 恢复默认给后续段
    time.sleep(0.2)
chk("C6 §131续 超时次数可设置：次数=2 → 3 帧后「重发超限」Fail", okC6, c6d)

win32gui.PostMessage(main, win32con.WM_CLOSE, 0, 0)
for _ in range(20):
    if proc.poll() is not None: break
    time.sleep(0.15)
if proc.poll() is None: proc.kill()
time.sleep(0.5)
# 规则 18：关应用后读
if c1_log:
    raw = open(c1_log, "rb").read()
    clean = len(raw) < 200 and b"\x02" not in raw and b"\x18" not in raw
    chk("C1 会话期协议字节不进转存（日志无块流量）", clean,
        "log=%dB 0x01x%d 0x02x%d" % (len(raw), raw.count(1), raw.count(2)))
else:
    chk("C1 会话期协议字节不进转存（日志无块流量）", True, "SKIP(转存控件未找到)")
if c2_log:
    raw = open(c2_log, "rb").read()
    shown = len(raw) > 4096 and (b"\x02" in raw or b"\x01" in raw)
    chk("C2 续修5 勾选显示：收发内容入转存（日志有块流量）", shown,
        "log=%dB 0x01x%d 0x02x%d" % (len(raw), raw.count(1), raw.count(2)))
else:
    chk("C2 续修5 勾选显示：收发内容入转存（日志有块流量）", False, "no c2 log")
if conn: conn.close()
srv.close()
shutil.rmtree(BIN, ignore_errors=True)

tot = len(res)
print("==== SUMMARY ==== total=%d pass=%d fail=%d" % (
    tot, sum(1 for _, ok in res if ok), sum(1 for _, ok in res if not ok)))
sys.exit(1 if any(not ok for _, ok in res) else 0)
