# relay.py LISTEN_IP PORT TARGET_IP: UDP relay that logs packet types; one upstream socket per client
import socket, select, sys, time
lip, port, tip = sys.argv[1], int(sys.argv[2]), sys.argv[3]
front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); front.bind((lip, port))
ups = {}  # client addr -> upstream socket
def log(m): print(time.strftime("%H:%M:%S"), m, flush=True)
while True:
    r, _, _ = select.select([front] + list(ups.values()), [], [], 60)
    for s in r:
        if s is front:
            d, src = front.recvfrom(4096)
            if src not in ups:
                u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind((lip, 0)); ups[src] = u
            log(f"{src} -> A  {len(d):4} {d.hex()}")
            ups[src].sendto(d, (tip, port))
        else:
            d, a = s.recvfrom(4096)
            cl = next(c for c, u in ups.items() if u is s)
            log(f"{cl} <- A  {len(d):4} {d.hex()}")
            front.sendto(d, cl)
