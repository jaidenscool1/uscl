#!/usr/bin/env python3
"""Exercise the real Eden private-service probe without a game or player profile."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eden", required=True, type=Path)
    parser.add_argument("--backend", type=Path, default=Path("build/switch-online-target/debug/uscl-switch-online"))
    args = parser.parse_args()
    eden, binary = args.eden.resolve(), args.backend.resolve()
    with tempfile.TemporaryDirectory(prefix="uscl-online-probe-") as directory:
        env = dict(os.environ)
        for variable, child in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache")]:
            env[variable] = str(Path(directory) / child)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)

        def probe(endpoint, expected):
            result = subprocess.run([str(eden), "--uscl-online-probe", endpoint], env=env, capture_output=True, text=True, timeout=8)
            response = json.loads(result.stdout)
            assert (result.returncode == 0) == response["ok"], "Exit code disagrees with reply"
            if expected == "ok":
                assert response["ok"] and response["result"]["native_online_play"] is False
            else:
                assert not response["ok"] and response["error"] == expected, response

        backend = subprocess.Popen([str(binary), "--port", "0"], stdout=subprocess.PIPE, text=True)
        try:
            line = backend.stdout.readline().strip()
            assert line.startswith("USCL_SWITCH_ONLINE_READY "), "Missing backend readiness"
            port = int(line.split()[1])
            probe(f"127.0.0.1:{port}", "ok")
        finally:
            backend.terminate()
            backend.wait(timeout=5)
            backend.stdout.close()
        print("PASS: real Eden/backend negotiation; native Online Play reported unsupported")

        for payload, expected in [
            (b'{"ok":true,"result":{"service":"other","protocol":1,"native_online_play":false}}\n', "incompatible_backend"),
            (b'{"ok":true,"result":{"service":"uscl-switch-online","protocol":2,"native_online_play":false}}\n', "incompatible_backend"),
            (b'not-json\n', "invalid_backend_response"),
            (b'{"ok":true}\n{}\n', "invalid_backend_response"),
            (b'{' + b'x' * 17000 + b'\n', "response_too_large"),
            (b'{"ok":true}', "backend_incomplete_response"),
            (None, "backend_incomplete_response"),
        ]:
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(8)
                port = listener.getsockname()[1]

                def respond():
                    with listener.accept()[0] as connection:
                        connection.settimeout(5)
                        connection.recv(16384)
                        if payload is None:
                            time.sleep(3.5)
                        else:
                            connection.sendall(payload)

                thread = threading.Thread(target=respond)
                thread.start()
                try:
                    started = time.monotonic()
                    probe(f"127.0.0.1:{port}", expected)
                    if payload is None:
                        assert time.monotonic() - started < 3.4, "Client exceeded its three-second deadline"
                finally:
                    thread.join(timeout=8)
                assert not thread.is_alive(), "Test responder did not exit"
            print(f"PASS: {expected}")

        for endpoint in ["example.com:443", "0.0.0.0:24873", "127.0.0.1:0", "127.0.0.1:65536", ""]:
            probe(endpoint, "invalid_loopback_endpoint")
        print("PASS: invalid/non-loopback endpoints rejected without connecting")
        probe(f"127.0.0.1:{port}", "backend_unavailable")
        print("PASS: unavailable backend fails")
        assert not list(Path(directory).rglob("*.ini")), "Probe unexpectedly wrote emulator settings"
        print("PASS: headless probe leaves emulator settings untouched")


if __name__ == "__main__":
    main()
