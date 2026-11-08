#!/usr/bin/env python3
"""TLS front for the Blaze redirector: tlsproxy.py LISTEN_PORT CERTDIR BACKEND_HOST BACKEND_PORT
Terminates ProtoSSL (legacy TLS, forged EA cert) on LISTEN_PORT and forwards the plaintext Blaze stream to the
C# Blaze server. Madden's redirector hop is TLS; the server answers getServerInstance telling it to connect to the
plaintext Blaze port for the main session."""
import socket, ssl, sys, threading

lport = int(sys.argv[1]); certdir = sys.argv[2]; bhost = sys.argv[3]; bport = int(sys.argv[4])
cap = open(sys.argv[5], 'ab', buffering=0) if len(sys.argv) > 5 else None  # optional: dump decrypted frames for the Blaze 3.x codec

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
try: ctx.minimum_version = ssl.TLSVersion.SSLv3
except Exception: ctx.minimum_version = ssl.TLSVersion.TLSv1
ctx.set_ciphers('ALL:@SECLEVEL=0')
ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
ctx.load_cert_chain(certdir + '/server.pem', certdir + '/server.key')

def pump(src, dst, tag=None):
    try:
        while True:
            b = src.recv(4096)
            if not b: break
            if cap and tag:
                cap.write(b'\n--- %s %d bytes ---\n' % (tag, len(b)))
                cap.write(b.hex().encode())
            dst.sendall(b)
    except Exception:
        pass
    finally:
        try: dst.shutdown(socket.SHUT_WR)
        except Exception: pass

def handle(raw, addr):
    try:
        head = raw.recv(64, socket.MSG_PEEK)  # peek the ClientHello so a failed handshake still shows the version bytes
        print(f'hello from {addr}: {head[:48].hex()}', flush=True)
    except Exception:
        pass
    try:
        c = ctx.wrap_socket(raw, server_side=True)
    except Exception as e:
        print(f'TLS handshake failed from {addr}: {e}', flush=True); raw.close(); return
    print(f'redirector TLS up from {addr} ({c.cipher()[1]} {c.cipher()[0]}) -> {bhost}:{bport}', flush=True)
    try:
        b = socket.create_connection((bhost, bport))
    except Exception as e:
        print(f'backend connect failed: {e}', flush=True); c.close(); return
    threading.Thread(target=pump, args=(c, b, b'C>S'), daemon=True).start()
    pump(b, c, b'S>C')
    c.close(); b.close()

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', lport)); srv.listen(8)
print(f'TLS redirector proxy on 127.0.0.1:{lport} -> {bhost}:{bport}', flush=True)
while True:
    raw, addr = srv.accept()
    threading.Thread(target=handle, args=(raw, addr), daemon=True).start()
