#!/usr/bin/env python3
"""Original, loopback-only MK8D native protocol adapter (development).

TLS/WebSocket and PRUDP Lite are separate from USCL's JSON control plane.
Protocol layouts: https://github.com/kinnay/NintendoClients/wiki/PRUDP-Protocol
No request bodies, identities, nonces, tickets or credentials are logged.
"""
import argparse
import asyncio
import base64
from dataclasses import dataclass, field
import hashlib
import json
import secrets
import ssl
import struct

from auth import Authority, Reader, datetime_wire, string, structure
from identity import load_identity
from rooms import ControlPlane, NativeRooms
from nat import NatGroup

MAX_FRAME = 65536
HEADER = struct.Struct("<BBHBBBBHH")


def event(name, **metadata):
    print(json.dumps({"event": name, **metadata}), flush=True)


def options_decode(data):
    result = {}
    while data:
        if len(data) < 2 or len(data) < 2 + data[1] or data[0] in result:
            raise ValueError("options")
        result[data[0]] = data[2:2 + data[1]]
        data = data[2 + data[1]:]
    return result


def packet(streams, source, destination, kind, flags, sequence, options=b"", payload=b"", fragment=0):
    if len(options) > 255 or len(payload) > 65535:
        raise ValueError("packet_size")
    return HEADER.pack(0x80, len(options), len(payload), streams, source, destination,
                       fragment, flags << 4 | kind, sequence) + options + payload


def rmc_error(protocol, call, code=0x80010002):
    # Core::NotImplemented; do not return fabricated successful operations.
    proto = bytes([protocol]) if protocol < 127 else b"\x7f" + struct.pack("<H", protocol)
    body = proto + b"\0" + struct.pack("<II", code, call)
    return struct.pack("<I", len(body)) + body


def rmc_success(protocol, call, method, result):
    proto = bytes([protocol]) if protocol < 127 else b"\x7f" + struct.pack("<H", protocol)
    body = proto + b"\x01" + struct.pack("<II", call, method | 0x8000) + result
    return struct.pack("<I", len(body)) + body


async def websocket_upgrade(reader, writer):
    raw = await reader.readuntil(b"\r\n\r\n")
    if len(raw) > 4096:
        raise ValueError("http_size")
    lines = raw[:-4].split(b"\r\n")
    if lines[0] != b"GET / HTTP/1.1":
        raise ValueError("http_request")
    headers = {}
    for line in lines[1:]:
        name, separator, value = line.partition(b":")
        name = name.lower()
        if not separator or name in headers:
            raise ValueError("http_headers")
        headers[name] = value.strip()
    if (headers.get(b"upgrade", b"").lower() != b"websocket" or
            b"upgrade" not in [v.strip().lower() for v in headers.get(b"connection", b"").split(b",")] or
            headers.get(b"sec-websocket-version") != b"13"):
        raise ValueError("websocket_upgrade")
    nonce = headers.get(b"sec-websocket-key", b"")
    if len(nonce) == 3:
        valid = len(base64.b64decode(nonce + b"=", validate=True)) == 2
    elif len(nonce) == 24:
        valid = len(base64.b64decode(nonce, validate=True)) == 16
    else:
        valid = False
    if not valid:
        raise ValueError("websocket_nonce")
    accept = base64.b64encode(hashlib.sha1(nonce + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    await writer.drain()
    event("websocket_connected", short_nonce=len(nonce) == 3)


async def frame_read(reader):
    first, second = await reader.readexactly(2)
    if first & 0x70 or not second & 0x80:
        raise ValueError("websocket_frame")
    opcode = first & 15
    size = second & 127
    if size == 126:
        size = int.from_bytes(await reader.readexactly(2), "big")
        if size < 126:
            raise ValueError("websocket_length")
    elif size == 127:
        raise ValueError("websocket_length")
    if size > MAX_FRAME or opcode not in (0, 2, 8, 9, 10):
        raise ValueError("websocket_size_or_opcode")
    if opcode >= 8 and (not first & 0x80 or size > 125):
        raise ValueError("websocket_control")
    mask = await reader.readexactly(4)
    data = await reader.readexactly(size)
    return bool(first & 0x80), opcode, bytes(v ^ mask[i % 4] for i, v in enumerate(data))


async def frame_write(writer, data, opcode=2):
    if len(data) > MAX_FRAME:
        raise ValueError("websocket_size")
    prefix = bytes([0x80 | opcode, len(data)]) if len(data) < 126 else bytes([0x80 | opcode, 126]) + struct.pack(">H", len(data))
    writer.write(prefix + data)
    await writer.drain()


@dataclass
class Connection:
    phase: str = "new"
    minor: int = 0
    signature: bytes = field(default_factory=lambda: secrets.token_bytes(16))
    next_sequence: int = 1
    fragment: int = 0
    fragments: bytearray = field(default_factory=bytearray)
    last_sequence: int | None = None
    cached: list = field(default_factory=list)
    last_kind: int | None = None
    pid: int | None = None
    registration: int | None = None


class Session:
    def __init__(self, authority=None):
        self.connections = {}
        self.authority = authority or Authority()
        self.notifications = []
        self.notification_call = 1

    def notify(self, pid, source, gid, participant, count):
        """Queue a NotificationEvent (protocol 14, method 1) for pid's own secure connection."""
        target = self.authority.sessions.get(pid)
        if target is None:
            return
        notification = structure(struct.pack("<QIQQ", source, 3001, gid, participant) + string("") + struct.pack("<Q", count))
        request = b"\x8e" + struct.pack("<II", target.notification_call, 1) + notification
        target.notifications.append(struct.pack("<I", len(request)) + request)
        target.notification_call += 1

    def close(self):
        for pid in [pid for pid, session in self.authority.sessions.items() if session is self]:
            del self.authority.sessions[pid]
        for state in self.connections.values():
            registration = self.authority.registrations.get(state.pid)
            if state.registration is not None and registration and registration[0] == state.registration:
                rooms = getattr(self.authority, "rooms", None)
                if rooms:
                    rooms.disconnect(state.pid)
                self.authority.registrations.pop(state.pid, None)
                self.authority.playing_sessions.pop(state.pid, None)

    def receive(self, data):
        replies = []
        while data:
            if len(data) < HEADER.size:
                raise ValueError("lite_header")
            magic, olen, size, streams, source, destination, fragment, tf, seq = HEADER.unpack_from(data)
            total = HEADER.size + olen + size
            if magic != 0x80 or len(data) < total or streams != 0xaa or destination not in (1, 2) or not 0 < source <= 31:
                raise ValueError("lite_packet")
            opts = options_decode(data[12:12 + olen])
            payload = data[12 + olen:total]
            data = data[total:]
            kind, flags = tf & 15, tf >> 4
            if flags & (1 | 0x200):  # ACK or aggregate ACK, never an RPC request.
                continue
            key = source, destination
            if key not in self.connections:
                if kind != 0 or len(self.connections) >= 8:
                    raise ValueError("lite_connection")
                self.connections[key] = Connection()
            state = self.connections[key]
            event("lite_packet", packet_type=kind, options_bytes=olen, payload_bytes=size, server_port=destination)
            if state.last_kind == kind and state.last_sequence == seq and state.cached:
                replies.extend(state.cached)
                continue
            current = []

            def reply(kind_, flags_, sequence_, opts_=b"", body=b""):
                current.append(packet(0xaa, destination, source, kind_, flags_, sequence_, opts_, body))

            def flush():
                # shortcut: other players' events wait for this client's next DATA or PING; push from the server loop if that lags in game.
                for notification in self.notifications:
                    reply(2, 6, state.next_sequence, body=notification)
                    state.next_sequence = (state.next_sequence + 1) & 65535
                self.notifications.clear()

            if kind == 0:
                if state.phase != "new" or len(opts.get(0, b"")) != 4 or payload:
                    raise ValueError("lite_syn")
                state.minor = min(int.from_bytes(opts[0], "little") & 255, 6)
                supported = b"\0\x04" + struct.pack("<I", state.minor)
                reply(0, 1, seq, supported + b"\x01\x10" + state.signature)
                state.phase = "syn"
            elif kind == 1:
                if state.phase != "syn":
                    raise ValueError("lite_connect")
                response = b""
                if destination == 1:
                    if payload:
                        raise ValueError("lite_auth_connect")
                else:
                    state.pid, _, response = self.authority.connect(payload)
                    self.authority.sessions[state.pid] = self
                    event("private_secure_connected")
                reply(1, 1, seq, b"\0\x04" + struct.pack("<I", state.minor), response)
                state.phase = "connected"
            elif kind == 2:
                if state.phase != "connected":
                    raise ValueError("lite_data_before_connect")
                if flags & 4:
                    reply(2, 1, seq)
                if fragment:
                    if fragment != state.fragment + 1:
                        raise ValueError("lite_fragment_order")
                    state.fragment = fragment
                state.fragments.extend(payload)
                if len(state.fragments) > MAX_FRAME:
                    raise ValueError("lite_fragment_size")
                if not fragment:
                    body = self.rpc(bytes(state.fragments), state, destination)
                    state.fragments.clear()
                    state.fragment = 0
                    if body:
                        reply(2, 6, state.next_sequence, body=body)
                        state.next_sequence = (state.next_sequence + 1) & 65535
                    flush()
            elif kind in (3, 4):
                reply(kind, 1, seq)
                if kind == 4 and state.phase == "connected" and destination == 2:
                    flush()
                if kind == 3:
                    state.phase = "closed"
            else:
                raise ValueError("lite_type")
            state.last_kind, state.last_sequence, state.cached = kind, seq, current
            replies.extend(current)
        return replies

    def rpc(self, data, state, destination):
        if len(data) < 13 or int.from_bytes(data[:4], "little") != len(data) - 4 or not data[4] & 0x80:
            raise ValueError("rmc_request")
        protocol = data[4] & 127
        offset = 5
        if protocol == 127:
            protocol = int.from_bytes(data[5:7], "little")
            offset = 7
        if len(data) < offset + 8:
            raise ValueError("rmc_size")
        call, method = struct.unpack_from("<II", data, offset)
        event("rmc_request", protocol=protocol, method=method, parameter_bytes=len(data) - offset - 8)
        if destination == 1 and protocol == 10 and method == 2:
            try:
                state.pid, result = self.authority.login(data[offset + 8:])
                event("private_login_ticket_issued")
                return rmc_success(protocol, call, method, result)
            except PermissionError:
                event("private_login_denied")
                return rmc_error(protocol, call, 0x80010006)
            except ValueError:
                event("private_login_format_unsupported")
                return rmc_error(protocol, call)
        if destination == 2 and state.pid is not None and protocol == 11:
            if method == 1:
                result = self.authority.register(state.pid, data[offset + 8:])
                state.registration = int.from_bytes(result[4:8], "little")
                event("private_station_registered")
                return rmc_success(protocol, call, method, result)
            if method == 5 and len(data) == offset + 8:
                return rmc_success(protocol, call, method, b"")
        if destination == 2 and state.pid is not None and protocol == 110 and method == 7:
            if len(data) != offset + 12:
                raise ValueError("utility_settings_request")
            # This private service has no integer-setting overrides; client defaults apply.
            event("integer_settings", index=int.from_bytes(data[offset + 8:], "little"), overrides=0)
            return rmc_success(protocol, call, method, struct.pack("<I", 0))
        if destination == 2 and state.pid is not None:
            rooms = getattr(self.authority, "rooms", None)
            if rooms and protocol == 109 and method == 38:
                result = rooms.create(state.pid, data[offset + 8:])
                gid = Reader(result).structure()[1].number(4)
                self.notify(state.pid, state.pid, gid, state.pid, 1)
                event("native_private_room_created", room_code=rooms.rooms[gid].code)
                return rmc_success(protocol, call, method, result)
            if rooms and protocol == 109 and method == 39:
                try:
                    result = rooms.join(state.pid, data[offset + 8:])
                except PermissionError as error:
                    event("native_room_join_denied", reason=str(error))
                    return rmc_error(protocol, call, 0x80030064)  # RendezVous::PermissionDenied
                gid = Reader(result).structure()[1].number(4)
                room = rooms.rooms[gid]
                for member in room.members:
                    self.notify(member, state.pid, gid, state.pid, room.session.count)
                event("native_room_joined", members=len(room.members))
                return rmc_success(protocol, call, method, result)
            if rooms and protocol == 21 and method == 41:
                result = rooms.session_urls(state.pid, data[offset + 8:])
                event("native_session_urls", count=int.from_bytes(result[:4], "little"))
                return rmc_success(protocol, call, method, result)
            if rooms and protocol == 21 and method == 2:
                result = rooms.unregister(state.pid, data[offset + 8:])
                return rmc_success(protocol, call, method, result)
            if rooms and protocol == 109 and method == 34:
                return rmc_success(protocol, call, method, rooms.update_progress(state.pid, data[offset + 8:]))
            if rooms and protocol == 109 and method == 68:
                return rmc_success(protocol, call, method, rooms.get_code(state.pid, data[offset + 8:]))
            if rooms and protocol == 109 and method == 69:
                event("resolve_request_hex", hex=data[offset + 8:].hex())
                result = rooms.resolve_code(state.pid, data[offset + 8:])
                event("native_room_code_resolved", gid=int.from_bytes(result, "little"))
                return rmc_success(protocol, call, method, result)
            if rooms and protocol == 50 and method == 1:
                return rmc_success(protocol, call, method, rooms.end_participation(state.pid, data[offset + 8:]))
            if protocol == 3 and method == 5:
                return rmc_success(protocol, call, method, self.authority.report_nat(state.pid, data[offset + 8:]))
            if protocol == 3 and method == 6 and len(data) == offset + 8:
                # GetRelaySignatureKey: advertise no relay so the game proceeds on its
                # direct mesh. Layout follows the NEX reference server.
                event("private_relay_signature_key")
                result = (struct.pack("<I", 0) + struct.pack("<Q", datetime_wire()) + string("") +
                          struct.pack("<H", 0) + struct.pack("<I", 0) + struct.pack("<I", 0))
                return rmc_success(protocol, call, method, result)
            if protocol == 112 and method == 4:
                result = self.authority.upload_common(state.pid, data[offset + 8:])
                return rmc_success(protocol, call, method, result)
            if protocol == 112 and method == 18:
                return rmc_success(protocol, call, method, self.authority.competition_info(data[offset + 8:]))
            if protocol == 109 and method == 60:
                result = self.authority.playing(data[offset + 8:])
                return rmc_success(protocol, call, method, result)
            if protocol == 109 and method == 65 and len(data) == offset + 8:
                # No favorite competitions exist for these private accounts.
                return rmc_success(protocol, call, method, struct.pack("<I", 0))
        return rmc_error(protocol, call)


class Server:
    def __init__(self, accounts=(), control=None):
        self.clients = 0
        self.authority = Authority(accounts)
        if control:
            self.authority.rooms = NativeRooms(self.authority, control)

    async def client(self, reader, writer):
        if self.clients >= 32:
            writer.close()
            return
        self.clients += 1
        session = None
        try:
            async with asyncio.timeout(None):
                await asyncio.wait_for(websocket_upgrade(reader, writer), 10)
                session = Session(self.authority)
                fragments = bytearray()
                assembling = False
                while True:
                    final, opcode, body = await asyncio.wait_for(frame_read(reader), 120)
                    if opcode == 8:
                        await frame_write(writer, b"", 8)
                        break
                    if opcode == 9:
                        await frame_write(writer, body, 10)
                        continue
                    if opcode == 10:
                        continue
                    if (opcode == 0) != assembling:
                        raise ValueError("websocket_fragment")
                    fragments.extend(body)
                    if len(fragments) > MAX_FRAME:
                        raise ValueError("websocket_fragment_size")
                    assembling = not final
                    if final:
                        for reply in session.receive(bytes(fragments)):
                            await frame_write(writer, reply)
                        fragments.clear()
        except (ValueError, PermissionError, asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError, TimeoutError, ssl.SSLError) as error:
            # Never emit peer-controlled exception text; reason identifiers are allowlisted.
            reason = str(error)
            known = {"station_scheme", "station_fields", "station_port", "station_count", "nex_bounds", "nex_string", "nex_trailing", "lite_connect", "lite_data_before_connect", "lite_syn", "lite_fragment_order", "rmc_request", "rmc_size", "ticket_integrity"}
            event("connection_ended", category=type(error).__name__, reason=reason if reason in known else "unspecified")
        finally:
            self.clients -= 1
            if session:
                try:
                    session.close()
                except (ValueError, OSError, PermissionError):
                    event("private_session_cleanup_failed")
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, ssl.SSLError):
                pass


async def run(args):
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(args.cert, args.key)
    accounts = [load_identity(path) for path in args.account]
    if len({account[0] for account in accounts}) != len(accounts) or len({account[1] for account in accounts}) != len(accounts):
        raise ValueError("duplicate private identity")
    control = ControlPlane([account[0] for account in accounts], args.control_port)
    instance = Server(accounts, control)
    server = await asyncio.start_server(instance.client, "127.0.0.1", args.port, ssl=tls, limit=8192, ssl_handshake_timeout=3, ssl_shutdown_timeout=1)
    nat = NatGroup(event)
    if args.nat:
        await nat.start()
    async with server:
        event("ready", port=server.sockets[0].getsockname()[1], native_online_play=False)
        async def leases():
            while True:
                await asyncio.sleep(30)
                try:
                    await asyncio.to_thread(control.heartbeat)
                except (OSError, ValueError, PermissionError):
                    event("control_plane_lease_failed")
        heartbeat = asyncio.create_task(leases())
        try:
            await server.serve_forever()
        finally:
            heartbeat.cancel()
            await asyncio.gather(heartbeat, return_exceptions=True)
            nat.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--account", action="append", default=[], help="private identity file (repeat for local players)")
    parser.add_argument("--control-port", type=int, default=24873)
    parser.add_argument("--nat", action="store_true", help="serve local NAT checks on loopback UDP 10025/10125")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or not 1 <= args.control_port <= 65535:
        parser.error("invalid port")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
