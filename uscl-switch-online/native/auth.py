"""Original local ticket authority and bounded NEX serialization.

Wire layouts: NintendoClients wiki (Authentication-Protocol,
NEX-Common-Types and Kerberos-Authentication). Credentials belong to USCL only.
"""
from datetime import datetime, timezone
import hashlib
import hmac
import secrets
import struct
import time


class Reader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, size):
        if not 0 <= size <= len(self.data) - self.offset:
            raise ValueError("nex_bounds")
        value = self.data[self.offset:self.offset + size]
        self.offset += size
        return value

    def number(self, size):
        return int.from_bytes(self.take(size), "little")

    def string(self):
        data = self.take(self.number(2))
        if not data or data[-1] != 0 or b"\0" in data[:-1]:
            raise ValueError("nex_string")
        return data[:-1].decode("utf-8")

    def buffer(self):
        return self.take(self.number(4))

    def structure(self):
        revision = self.number(1)
        return revision, Reader(self.buffer())

    def finish(self):
        if self.offset != len(self.data):
            raise ValueError("nex_trailing")


def string(value):
    data = value.encode("utf-8") + b"\0"
    if len(data) > 65535:
        raise ValueError("nex_string_size")
    return struct.pack("<H", len(data)) + data


def buffer(data):
    return struct.pack("<I", len(data)) + data


def structure(data, revision=0):
    return bytes([revision]) + buffer(data)


def datetime_wire():
    now = datetime.now(timezone.utc)
    return (now.year << 26 | now.month << 22 | now.day << 17 |
            now.hour << 12 | now.minute << 6 | now.second)


def rc4(key, data):
    if not key:
        raise ValueError("empty_key")
    state = list(range(256))
    position = 0
    for index in range(256):
        position = (position + state[index] + key[index % len(key)]) & 255
        state[index], state[position] = state[position], state[index]
    left = right = 0
    result = bytearray()
    for value in data:
        left = (left + 1) & 255
        right = (right + state[left]) & 255
        state[left], state[right] = state[right], state[left]
        result.append(value ^ state[(state[left] + state[right]) & 255])
    return bytes(result)


def encrypt(key, data):
    ciphertext = rc4(key, data)
    return ciphertext + hmac.digest(key, ciphertext, "md5")


def decrypt(key, data):
    if len(data) < 16 or not hmac.compare_digest(data[-16:], hmac.digest(key, data[:-16], "md5")):
        raise ValueError("ticket_integrity")
    return rc4(key, data[:-16])


def login_parameters(data):
    reader = Reader(data)
    username = reader.string()
    name = reader.string()
    outer_size = reader.number(4)
    raw = reader.buffer()
    if outer_size != len(raw) + 4 or name != "AuthenticationInfo":
        raise ValueError("auth_type")
    reader.finish()
    any_data = Reader(raw)
    _, base = any_data.structure()
    base.finish()
    _, info = any_data.structure()
    token = info.string()
    ngs = info.number(4)
    token_type = info.number(1)
    server_version = info.number(4)
    info.finish()
    any_data.finish()
    if ngs != 4 or token_type != 2:
        raise ValueError("auth_version")
    return username, token, server_version


class Authority:
    def __init__(self, accounts=(), clock=time.monotonic):
        self.accounts = dict(accounts)
        self.tickets = {}
        self.clock = clock
        self.registrations = {}
        self.next_connection = 1
        self.common_data = {}
        self.playing_sessions = {}
        self.nat_properties = {}
        self.sessions = {}
        self.nat = None  # NatGroup, when the adapter serves NAT checks
        self.p2p_ports = {}  # pid -> game peer-to-peer port seen by the NAT responder  # pid -> adapter Session, for notifications to other players

    def cleanup(self):
        now = self.clock()
        self.tickets = {key: ticket for key, ticket in self.tickets.items() if ticket[2] > now}

    def login(self, data):
        username, token, version = login_parameters(data)
        if len(token) != 64 or any(ch not in "0123456789abcdef" for ch in token):
            raise PermissionError("private_credentials_required")
        candidates = [pid for pid, expected in self.accounts.items() if hmac.compare_digest(token, expected)]
        if len(candidates) != 1:
            raise PermissionError("private_credentials_required")
        pid = candidates[0]
        if username.lower() not in {str(pid), f"{pid:x}", f"{pid:016x}"}:
            raise PermissionError("private_identity_mismatch")
        self.cleanup()
        if len(self.tickets) >= 256:
            raise PermissionError("ticket_capacity")
        session = secrets.token_bytes(32)
        internal = secrets.token_bytes(32)
        source = secrets.token_bytes(16)
        self.tickets[internal] = (pid, session, self.clock() + 120)
        ticket = encrypt(source, session + struct.pack("<Q", 2) + buffer(internal))
        station = "prudps:/address=0.0.0.1;port=1;stream=10;sid=2;type=2;PID=2;CID=1"
        connection = structure(string(station) + struct.pack("<I", 0) + string("") + struct.pack("<Q", datetime_wire()), 1)
        response = (struct.pack("<IQ", 0x10001, pid) + buffer(ticket) + connection +
                    string(f"branch:uscl build:4_0_0_{version}_0") + string(source.hex()))
        return pid, response

    def connect(self, data):
        reader = Reader(data)
        ticket, encrypted = reader.buffer(), reader.buffer()
        reader.finish()
        self.cleanup()
        if ticket not in self.tickets:
            raise PermissionError("private_ticket_required")
        pid, session, _ = self.tickets[ticket]
        request = Reader(decrypt(session, encrypted))
        if request.number(8) != pid:
            raise PermissionError("private_ticket_identity")
        connection = request.number(4)
        check = request.number(4)
        request.finish()
        return pid, connection, buffer(struct.pack("<I", (check + 1) & 0xffffffff))

    def register(self, pid, data):
        if pid not in self.accounts:
            raise PermissionError("private_identity_required")
        reader = Reader(data)
        count = reader.number(4)
        if not 0 < count <= 8:
            raise ValueError("station_count")
        urls = [reader.string() for _ in range(count)]
        reader.finish()
        parsed = []
        for url in urls:
            scheme, separator, content = url.partition(":/")
            if scheme not in ("prudp", "prudps") or not separator or len(url) > 1024:
                raise ValueError("station_scheme")
            fields = {}
            for part in content.split(";"):
                key, separator, value = part.partition("=")
                if not separator or key in fields:
                    raise ValueError("station_fields")
                fields[key] = value
            port = fields.get("port", "")
            if not port.isascii() or not port.isdigit() or not 0 < int(port) <= 65535:
                raise ValueError("station_port")
            numeric = {}
            for name in ("port", "stream", "sid", "CID", "natf", "natm", "upnp", "pmp", "Pl", "Tpt"):
                if name in fields:
                    value = fields[name]
                    if not value.isascii() or not value.isdigit() or int(value) >= 2 ** 64:
                        raise ValueError("station_fields")
                    numeric[name] = int(value)
            parsed.append((scheme, numeric))
        connection = self.next_connection
        self.next_connection += 1
        # Local-only development route: never advertise a peer-supplied address.
        fields = {"address": "127.0.0.1", **parsed[0][1], "type": 2, "PID": pid, "RVCID": connection}
        public = parsed[0][0] + ":/" + ";".join(f"{name}={value}" for name, value in fields.items())
        self.registrations[pid] = (connection, public)
        return struct.pack("<II", 0x10001, connection) + string(public)

    def upload_common(self, pid, data):
        reader = Reader(data)
        common = reader.buffer()
        unique = reader.number(8)
        reader.finish()
        if len(common) > 8192:
            raise ValueError("common_data_size")
        self.common_data[pid] = (unique, common)
        return b""

    def report_nat(self, pid, data):
        reader = Reader(data)
        values = tuple(reader.number(4) for _ in range(3))
        reader.finish()
        self.nat_properties[pid] = values
        # shortcut: the latest NAT check is attributed to whoever reports NAT next; fine for one local client at a time.
        if self.nat is not None and self.nat.observed_port:
            self.p2p_ports[pid] = self.nat.observed_port
        return b""

    def playing(self, data):
        reader = Reader(data)
        count = reader.number(4)
        if count > 256:
            raise ValueError("friend_count")
        peers = [reader.number(8) for _ in range(count)]
        reader.take(2)  # MK8D's two filter selectors; retained by the caller.
        reader.finish()
        entries = [self.playing_sessions[pid] for pid in peers if pid in self.playing_sessions]
        return struct.pack("<I", len(entries)) + b"".join(entries)

    def competition_info(self, data):
        # No competitions are configured on this private service.
        reader = Reader(data)
        _, request = reader.structure()
        request.number(1)
        _, result_range = request.structure()
        result_range.number(4)
        if result_range.number(4) > 1000:
            raise ValueError("competition_range")
        result_range.finish()
        request.finish()
        reader.finish()
        return struct.pack("<I", 0)
