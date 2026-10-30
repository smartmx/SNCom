import socket, sys, time
port = int(sys.argv[1]); mode = sys.argv[2]
s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
if mode == "marker":
    s.sendall(b"MARKERFIRSTLINE-0000000\r\n")
    n = 0
    buf = []
    for k in range(300000):
        buf.append(b"FILLER-%07d-padding-padding\r\n" % k)
        if len(buf) == 1000:
            s.sendall(b"".join(buf)); buf = []
    if buf: s.sendall(b"".join(buf))
    n = 300000
    time.sleep(0.5)
    print("sent marker + %d unique filler lines" % n)
else:
    s.sendall(b"".join(b"TAILLINE-%06d-end\r\n" % i for i in range(400)))
    time.sleep(0.3)
    print("sent tail 400")
s.close()