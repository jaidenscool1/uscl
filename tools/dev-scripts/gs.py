# gs.py: minimal GameSpy stand-in for probing: answers the "available" check (UDP 27900), logs anything else
import socket, select, time
def log(m): print(time.strftime("%H:%M:%S"), m, flush=True)
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(("127.0.0.1", 27900))
tcps = []
for p in (29900, 29901, 29920, 28910, 80, 443):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try: s.bind(("127.0.0.1", p)); s.listen(); tcps.append(s)
    except OSError as e: log(f"tcp {p}: {e}")
conns = []
while True:
    r, _, _ = select.select([u] + tcps + conns, [], [], 60)
    for s in r:
        if s is u:
            d, a = u.recvfrom(2048); log(f"udp27900 {a} {d!r}")
            if d[:1] == b"\x09": u.sendto(b"\xfe\xfd\x09\x00\x00\x00\x00", a); log("  -> available")
        elif s in tcps:
            c, a = s.accept(); conns.append(c); log(f"tcp {s.getsockname()[1]} connect from {a}")
            if s.getsockname()[1] == 29900: c.sendall(b"\\lc\\1\\challenge\\ABCDEFGHIJ\\id\\1\\final\\"); log("  -> gpcm challenge")
        else:
            d = s.recv(4096)
            if not d: conns.remove(s); s.close(); log("closed"); continue
            log(f"tcp {s.getsockname()[1]} data {d[:300]!r}")
