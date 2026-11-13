#!/usr/bin/env python3
"""Legacy TLS front for the Blaze redirector, for games whose ProtoSSL only speaks SSLv3 + RC4 (e.g. NHL 15).

Why this exists: the normal tlsproxy.py terminates with the system OpenSSL, but OpenSSL 3.x removed SSLv3 and
RC4 entirely -- an NHL-style ClientHello (record/client_version 0x0300, cipher 0x0005 RC4-SHA) is rejected with
[SSL: VERSION_TOO_LOW] / "no cipher". We terminate instead with tlslite-ng, a pure-Python TLS stack that still
implements SSLv3 and RC4, so nothing on the host (and no future OpenSSL update) can take those away. Decrypted
Blaze frames are forwarded plaintext to the C# server, same contract as tlsproxy.py.

usage: tlsproxy_legacy.py LISTEN_PORT CERTDIR BACKEND_HOST BACKEND_PORT
  (CERTDIR holds server.crt + server.key, the forged EA redirector cert.)
Needs tlslite-ng importable (USCL keeps it at ~/.local/share/uscl/pylib; add that to PYTHONPATH).
"""
import os, socket, sys, threading

sys.path.insert(0, os.path.expanduser("~/.local/share/uscl/pylib"))
from tlslite import TLSConnection, X509, X509CertChain, parsePEMKey, HandshakeSettings
from tlslite.errors import TLSAbruptCloseError
from tlslite.constants import AlgorithmOID, HashAlgorithm, SignatureAlgorithm

# EA's forged redirector cert declares its signature-algorithm OID as bare rsaEncryption (1.2.840.113549.1.1.1),
# which tlslite's table doesn't list -> KeyError on parse. The value is irrelevant to serving (a server never
# verifies its own cert's signature), so register it. We keep serving the byte-identical cert the game expects.
AlgorithmOID.oid.setdefault(bytes.fromhex("06092a864886f70d010101"), (HashAlgorithm.md5, SignatureAlgorithm.rsa))

lport = int(sys.argv[1]); certdir = sys.argv[2]; bhost = sys.argv[3]; bport = int(sys.argv[4])

cert_pem = open(certdir + "/server.pem").read()
key_pem = open(certdir + "/server.key").read()
chain = X509CertChain([X509().parse(cert_pem)])
key = parsePEMKey(key_pem, private=True)

settings = HandshakeSettings()
settings.minVersion = (3, 0)                 # SSLv3
settings.maxVersion = (3, 3)                 # ...up to TLS1.2, let the client pick
settings.cipherNames = ["rc4", "aes256", "aes128", "3des"]
settings.macNames = ["sha", "md5"]
settings.rsaSigHashes = settings.rsaSigHashes or ["sha1", "md5"]
settings.keyExchangeNames = ["rsa", "dhe_rsa", "ecdhe_rsa"]


def pump_to_backend(conn, back):
    try:
        while True:
            d = conn.read(max=4096)
            if not d:
                break
            back.sendall(d)
    except (TLSAbruptCloseError, OSError, ValueError):
        pass
    finally:
        try: back.shutdown(socket.SHUT_WR)
        except OSError: pass


def pump_to_client(conn, back):
    try:
        while True:
            d = back.recv(4096)
            if not d:
                break
            conn.write(d)
    except (TLSAbruptCloseError, OSError, ValueError):
        pass
    finally:
        try: conn.close()
        except Exception: pass


def handle(raw, addr):
    conn = TLSConnection(raw)
    try:
        conn.handshakeServer(certChain=chain, privateKey=key, settings=settings)
    except Exception as e:
        print(f"legacy TLS handshake failed from {addr}: {e}", flush=True)
        raw.close(); return
    print(f"legacy TLS up from {addr} ({conn.version} {conn.getCipherName()}) -> {bhost}:{bport}", flush=True)
    try:
        back = socket.create_connection((bhost, bport))
    except OSError as e:
        print(f"backend connect failed: {e}", flush=True); conn.close(); return
    t = threading.Thread(target=pump_to_backend, args=(conn, back), daemon=True); t.start()
    pump_to_client(conn, back)
    t.join(timeout=2)
    back.close()


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", lport)); srv.listen(8)
print(f"legacy (SSLv3/RC4) TLS redirector on 127.0.0.1:{lport} -> {bhost}:{bport}", flush=True)
while True:
    raw, addr = srv.accept()
    threading.Thread(target=handle, args=(raw, addr), daemon=True).start()
