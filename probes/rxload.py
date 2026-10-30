# rxload.py -- TCP load generator: connect, send N MB of 30-byte lines.
# usage: rxload.py PORT MB [BYTES_PER_SEC]
import socket, sys, time

port = int(sys.argv[1]); mb = float(sys.argv[2])
rate = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
line = b"0123456789ABCDEF0123456789ABC\r\n"      # 30 bytes
block = line * (65536 // len(line))             # ~64 KiB of whole lines
total = int(mb * 1024 * 1024)
sent = 0
t0 = time.time()
s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
try:
    while sent < total:
        n = s.send(block)
        sent += n
        if rate > 0:
            want = sent / rate
            dt = want - (time.time() - t0)
            if dt > 0:
                time.sleep(dt)
except Exception as ex:
    print("send error after %d bytes: %r" % (sent, ex))
t1 = time.time()
print("sent=%d bytes in %.2fs (%.2f MB/s) lines=%d" % (sent, t1 - t0, sent / 1048576.0 / max(t1 - t0, 1e-9), sent // len(line)))
s.close()