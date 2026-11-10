#!/usr/bin/env python3
"""Real socket checks for the metadata-only observer; no game compatibility claim."""
import argparse
import importlib.util
import json
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eden-source", type=Path, help="also compile and exercise Eden's route allowlist")
    parser.add_argument("--tls-cert", type=Path, help="optional localhost certificate for a real TLS socket check")
    parser.add_argument("--tls-key", type=Path)
    args = parser.parse_args()
    if bool(args.tls_cert) != bool(args.tls_key):
        parser.error("--tls-cert and --tls-key must be supplied together")
    scripts = Path(__file__).resolve().parent
    observer = scripts / "observe-switch-native.py"
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("native_observer", observer)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    request = (b"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
               b"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n")
    for valid in (True, False):
        server, client = socket.socketpair()
        result = []
        def serve():
            try:
                result.append(module.websocket_observation(server, request[:8], time.monotonic() + 2))
            except ValueError:
                result.append("rejected")
            finally:
                server.close()
        thread = threading.Thread(target=serve)
        thread.start()
        try:
            client.settimeout(3)
            client.sendall(request[8:])
            response = client.recv(1024)
            assert b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in response
            payload = b"\x80\x00\x00\x00\x11\x01\x01\x00\x00\x00\x00\x00"
            mask = b"abcd"
            if valid:
                frame = bytes([0x82, 0x80 | len(payload)]) + mask + bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
            else:
                frame = bytes([0x82, len(payload)]) + payload
            client.sendall(frame)
        finally:
            client.close()
            thread.join(timeout=4)
        assert not thread.is_alive()
        if valid:
            assert result[0]["framing"] == "prudp-lite-header" and result[0]["packet_type"] == 0
        else:
            assert result == ["rejected"]
    print("PASS: split WebSocket upgrade, RFC accept digest, masked Lite framing, unmasked rejection")
    class Sink:
        def sendall(self, data):
            assert data.startswith(b"HTTP/1.1 101 ")
    short = request.replace(b"dGhlIHNhbXBsZSBub25jZQ==", b"abc")
    mask = b"abcd"
    payload = b"\x80\0\0\0\xaa\x1f\x01\0\0\0\0\0"
    frame = bytes([0x82, 0x80 | len(payload)]) + mask + bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    try:
        module.websocket_observation(Sink(), short + frame, time.monotonic() + 2)
        raise AssertionError("nonstandard nonce accepted without explicit option")
    except ValueError:
        pass
    result = module.websocket_observation(Sink(), short + frame, time.monotonic() + 2, mk8_short_nonce=True)
    assert result["framing"] == "prudp-lite-header"
    print("PASS: observed short nonce requires explicit diagnostic option")
    process = subprocess.Popen([sys.executable, str(observer), "--port", "0", "--limit", "3", "--duration", "5"], stdout=subprocess.PIPE, text=True)
    try:
        ready = json.loads(process.stdout.readline())
        assert ready["native_online_play"] is False
        port = ready["port"]
        with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
            client.sendall(b"POST /private-token HTTP/1.1\r\nAuthorization: secret\r\n\r\n")
        with socket.socket(type=socket.SOCK_DGRAM) as client:
            client.sendto(b"private-credential" * 300, ("127.0.0.1", port))
        with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
            client.sendall(b"\x16\x03\x03\x00\x20private-handshake")
        output, _ = process.communicate(timeout=8)
        assert process.returncode == 0
        assert not any(secret in output for secret in ("secret", "private", "credential", "handshake", "Authorization"))
        rows = [json.loads(line) for line in output.splitlines()]
        chunks = [row for row in rows if row["event"] == "first-chunk"]
        assert len(chunks) == 3
        assert {row["framing"] for row in chunks} == {"unknown", "http-request-prefix", "tls-record-header"}
        assert any(row["transport"] == "udp" and row["truncated"] for row in chunks)
        assert rows[-1] == {"event": "finished", "observations": 3}
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    with tempfile.TemporaryDirectory() as directory:
        result = subprocess.run([sys.executable, str(observer), "--port", "0", "--duration", "0.1"], cwd=directory, capture_output=True, text=True, timeout=3)
        assert result.returncode == 0
        assert json.loads(result.stdout.splitlines()[-1])["observations"] == 0
        assert not list(Path(directory).iterdir())
    print("PASS: TCP/UDP observation, bounded payloads, metadata-only output, finite idle lifetime")
    if args.tls_cert:
        process = subprocess.Popen([sys.executable, str(observer), "--port", "0", "--limit", "1", "--duration", "5", "--cert", str(args.tls_cert), "--key", str(args.tls_key)], stdout=subprocess.PIPE, text=True)
        try:
            port = json.loads(process.stdout.readline())["port"]
            context = ssl.create_default_context(cafile=str(args.tls_cert))
            with socket.create_connection(("127.0.0.1", port), timeout=2) as raw:
                with context.wrap_socket(raw, server_hostname="localhost") as client:
                    client.sendall(b"POST /nex/private-token?credential=secret HTTP/1.1\r\n\r\n")
            output, _ = process.communicate(timeout=8)
            assert process.returncode == 0
            row = json.loads(output.splitlines()[0])
            assert row["transport"] == "tls" and row["method"] == "POST"
            assert row["path_shape"] == "nex/<segment>"
            assert "secret" not in output and "private-token" not in output
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        print("PASS: TLS termination with verified local certificate; request values redacted")
    if args.eden_source:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "route-test"
            source = r'''
#include "core/hle/service/sockets/uscl_native_route.h"
#include "core/hle/service/acc/uscl_native_identity.h"
#include <cassert>
int main() {
    using Service::Account::ParseUsclNativeIdentity;
    const std::string valid = "USCL_NATIVE_ID_V1\n123\n" + std::string(64, 'a') + "\n";
    const auto identity = ParseUsclNativeIdentity(valid);
    assert(identity && identity->id == 123 && identity->token == std::string(64, 'a'));
    assert(!ParseUsclNativeIdentity(valid + "extra"));
    assert(!ParseUsclNativeIdentity("USCL_NATIVE_ID_V1\n0\n" + std::string(64, 'a') + "\n"));
    assert(!ParseUsclNativeIdentity("USCL_NATIVE_ID_V1\n18446744073709551616\n" + std::string(64, 'a') + "\n"));
    assert(!ParseUsclNativeIdentity("USCL_NATIVE_ID_V1\n123\n" + std::string(64, 'z') + "\n"));
    using Service::Sockets::UsclNativeDiagnosticRoute;
    constexpr auto title = 0x0100152000022000ULL;
    constexpr auto host = "g2b309e01-%.s.n.srv.nintendo.net";
    assert(UsclNativeDiagnosticRoute(host, title, true) == "127.0.0.1");
    assert(UsclNativeDiagnosticRoute("nncs1-%.n.n.srv.nintendo.net", title, true) == "127.0.0.1");
    assert(UsclNativeDiagnosticRoute("nncs2-%.n.n.srv.nintendo.net", title, true) == "127.0.0.2");
    assert(UsclNativeDiagnosticRoute("nncs1-%.n.n.srv.nintendo.net", title, false) == "nncs1-%.n.n.srv.nintendo.net");
    assert(UsclNativeDiagnosticRoute("nncs1-%.n.n.srv.nintendo.net", title + 1, true) == "nncs1-%.n.n.srv.nintendo.net");
    assert(UsclNativeDiagnosticRoute(host, title, false) == host);
    assert(UsclNativeDiagnosticRoute(host, title + 1, true) == host);
    for (auto other : {"example.com", "srv.nintendo.net", "127.0.0.1",
                       "g2b309e01-%.s.n.srv.nintendo.net.attacker.invalid",
                       "g2b309e01-lp1.s.n.srv.nintendo.net"}) {
        assert(UsclNativeDiagnosticRoute(other, title, true) == other);
    }
}
'''
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(args.eden_source.resolve() / "src"), "-x", "c++", "-", "-o", str(binary)], input=source, text=True, check=True)
            subprocess.run([str(binary)], check=True)
        print("PASS: exact endpoint/title allowlist, disabled and unrelated routes preserved")


if __name__ == "__main__":
    main()
