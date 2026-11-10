#!/usr/bin/env python3
"""Bounded loopback request observer. No payloads, addresses or credentials logged.

This is not an authentication or NEX service. Optional TLS/WebSocket termination
exposes framing metadata only, then closes. UDP receives no reply. A game error
is expected. Use only an isolated diagnostic profile.
"""

import argparse
import base64
import hashlib
import json
import selectors
import socket
import ssl
import time


def describe(data):
    """Describe framing only; never return arbitrary bytes supplied by a peer."""
    result = {"observed_bytes": len(data), "framing": "unknown"}
    if len(data) >= 5 and data[0] in (20, 21, 22, 23) and data[1] == 3 and data[2] <= 4:
        result.update(framing="tls-record-header", declared_bytes=int.from_bytes(data[3:5], "big"))
    elif any(data.startswith(method + b" ") for method in (b"GET", b"POST", b"HEAD", b"PUT", b"DELETE", b"OPTIONS")):
        result["framing"] = "http-request-prefix"
        # Only emit fixed protocol words. Arbitrary path segments may be secrets.
        words = {b"nex", b"auth", b"account", b"token", b"v1", b"api", b"login", b"ac"}
        method, target, *_ = data.split(b" ", 2)
        result["method"] = method.decode("ascii")
        path = target.split(b"?", 1)[0]
        result["path_shape"] = "/".join(segment.decode("ascii") if segment in words else "<segment>" for segment in path.split(b"/") if segment)
        headers = {}
        for line in data.split(b"\r\n")[1:]:
            name, separator, value = line.partition(b":")
            if separator:
                headers[name.lower()] = value.strip().lower()
        result["websocket_upgrade"] = headers.get(b"upgrade") == b"websocket"
        result["websocket_version13"] = headers.get(b"sec-websocket-version") == b"13"
        result["websocket_connection_upgrade"] = b"upgrade" in [part.strip() for part in headers.get(b"connection", b"").split(b",")]
        result["websocket_key_characters"] = len(headers.get(b"sec-websocket-key", b""))
    return result


def websocket_observation(channel, initial, deadline):
    """One strict, bounded RFC6455 upgrade and binary frame; no NEX replies."""
    buffer = bytearray(initial)

    def receive():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError
        channel.settimeout(remaining)
        data = channel.recv(4096)
        if not data:
            raise ValueError("incomplete")
        return data

    while b"\r\n\r\n" not in buffer:
        if len(buffer) >= 4096:
            raise ValueError("header_size")
        buffer.extend(receive())
    raw, rest = bytes(buffer).split(b"\r\n\r\n", 1)
    if len(raw) + 4 > 4096:
        raise ValueError("header_size")
    lines = raw.split(b"\r\n")
    if lines[0] != b"GET / HTTP/1.1":
        raise ValueError("request")
    headers = {}
    for line in lines[1:]:
        name, separator, value = line.partition(b":")
        name = name.lower()
        if not separator or name in headers:
            raise ValueError("headers")
        headers[name] = value.strip()
    if (headers.get(b"upgrade", b"").lower() != b"websocket" or
            b"upgrade" not in [part.strip().lower() for part in headers.get(b"connection", b"").split(b",")] or
            headers.get(b"sec-websocket-version") != b"13"):
        raise ValueError("upgrade")
    key = headers.get(b"sec-websocket-key", b"")
    try:
        if len(base64.b64decode(key, validate=True)) != 16:
            raise ValueError("key")
    except ValueError:
        raise ValueError("key") from None
    digest = hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest()
    channel.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + base64.b64encode(digest) + b"\r\n\r\n")
    buffer = bytearray(rest)

    def take(length):
        while len(buffer) < length:
            buffer.extend(receive())
        result = bytes(buffer[:length])
        del buffer[:length]
        return result

    first, second = take(2)
    if first != 0x82 or not second & 0x80:
        raise ValueError("frame")
    length = second & 0x7f
    if length == 126:
        length = int.from_bytes(take(2), "big")
        if length < 126:
            raise ValueError("frame_size")
    elif length == 127:
        raise ValueError("frame_size")
    if length > 4096:
        raise ValueError("frame_size")
    mask = take(4)
    payload = bytes(value ^ mask[index % 4] for index, value in enumerate(take(length)))
    result = {"event": "websocket-frame", "observed_bytes": length, "framing": "binary"}
    if len(payload) >= 12 and payload[0] == 0x80:
        options = payload[1]
        body = int.from_bytes(payload[2:4], "little")
        if len(payload) == 12 + options + body:
            result.update(framing="prudp-lite-header", packet_type=int.from_bytes(payload[8:10], "little") & 15, options_bytes=options, payload_bytes=body)
    return result


def observe(port, duration, limit, tls=None, websocket=False):
    with selectors.DefaultSelector() as selector, socket.socket() as tcp, socket.socket(type=socket.SOCK_DGRAM) as udp:
        tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        tcp.bind(("127.0.0.1", port))
        port = tcp.getsockname()[1]
        udp.bind(("127.0.0.1", port))
        tcp.listen(8)
        for listener in (tcp, udp):
            listener.setblocking(False)
            selector.register(listener, selectors.EVENT_READ)
        print(json.dumps({"event": "ready", "port": port, "native_online_play": False}), flush=True)
        deadline = time.monotonic() + duration
        count = 0
        pending = {}
        try:
            while count < limit and time.monotonic() < deadline:
                for key, _ in selector.select(min(0.2, max(0, deadline - time.monotonic()))):
                    channel = key.fileobj
                    extra = None
                    if channel is tcp:
                        connection, _ = tcp.accept()
                        connection.setblocking(False)
                        if len(pending) >= 16:
                            connection.close()
                        else:
                            pending[connection] = time.monotonic() + 2
                            selector.register(connection, selectors.EVENT_READ)
                        continue
                    if channel is udp:
                        data, _ = udp.recvfrom(4097)
                        transport = "udp"
                    else:
                        selector.unregister(channel)
                        pending.pop(channel)
                        try:
                            if tls:
                                channel.settimeout(min(2, max(0.01, deadline - time.monotonic())))
                                channel = tls.wrap_socket(channel, server_side=True)
                            data = channel.recv(4097)
                            if websocket:
                                extra = websocket_observation(channel, data, min(deadline, time.monotonic() + 2))
                        except ValueError as error:
                            reason = str(error)
                            extra = {"event": "websocket-rejected", "reason": reason if reason in {"incomplete", "header_size", "request", "headers", "upgrade", "key", "frame", "frame_size"} else "invalid"}
                        except (ConnectionError, ssl.SSLError, TimeoutError):
                            data = b""
                        finally:
                            channel.close()
                        transport = "tls" if tls else "tcp"
                    report = describe(data[:4096])
                    report.update(event="first-chunk", transport=transport, truncated=len(data) > 4096)
                    print(json.dumps(report), flush=True)
                    if extra:
                        print(json.dumps(extra), flush=True)
                    count += 1
                    if count >= limit:
                        break
                for connection, expiry in list(pending.items()):
                    if time.monotonic() >= expiry:
                        selector.unregister(connection)
                        connection.close()
                        del pending[connection]
        finally:
            for connection in pending:
                connection.close()
        print(json.dumps({"event": "finished", "observations": count}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, type=int, help="guest destination port, or 0 for socket tests")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--limit", type=int, default=16)
    parser.add_argument("--cert", help="optional local PEM certificate for diagnostic TLS termination")
    parser.add_argument("--key", help="matching private key; never commit or publish it")
    parser.add_argument("--websocket", action="store_true", help="upgrade GET / and observe one binary frame; requires TLS")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or not 0 < args.duration <= 120 or not 1 <= args.limit <= 64:
        parser.error("port must be 0..65535, duration (0,120], limit 1..64")
    if bool(args.cert) != bool(args.key):
        parser.error("--cert and --key must be supplied together")
    if args.websocket and not args.cert:
        parser.error("--websocket requires --cert and --key")
    tls = None
    if args.cert:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(args.cert, args.key)
    observe(args.port, args.duration, args.limit, tls, args.websocket)


if __name__ == "__main__":
    main()
