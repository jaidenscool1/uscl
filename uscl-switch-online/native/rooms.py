"""Native matchmaking wire objects backed by the existing Rust room service."""
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
import json
import secrets
import socket
import struct

from auth import Reader, buffer, string, structure

GAME = {"title_id": "0100152000022000", "update": "4.0.1", "dlc": [], "adapter": 1}


@dataclass
class MatchSession:
    gid: int
    owner: int
    host: int
    minimum: int
    maximum: int
    policy: int
    argument: int
    flags: int
    state: int
    description: str
    mode: int
    attributes: list
    open: int
    system: int
    application: bytes
    count: int
    progress: int
    key: bytes
    option: int
    tail: bytes
    gathering_revision: int = 0
    revision: int = 0

    @classmethod
    def read(cls, reader):
        grev, gathering = reader.structure()
        values = [gathering.number(size) for size in (4, 8, 8, 2, 2, 4, 4, 4, 4)]
        values.append(gathering.string())
        gathering.finish()
        rev, match = reader.structure()
        mode = match.number(4)
        count = match.number(4)
        if count > 64:
            raise ValueError("match_attribute_count")
        attributes = [match.number(4) for _ in range(count)]
        opened, system = match.number(1), match.number(4)
        application = match.buffer()
        participants, progress = match.number(4), match.number(1)
        key, option = match.buffer(), match.number(4)
        # Revision-specific tail is retained exactly. It contains versioned game
        # parameters, time, passwords and codeword, never written to logs.
        tail = match.take(len(match.data) - match.offset)
        if len(application) > 8192 or len(key) > 64 or opened > 1 or len(tail) > 8192:
            raise ValueError("match_size")
        return cls(*values, mode, attributes, opened, system, application, participants,
                   progress, key, option, tail, grev, rev)

    def encode(self):
        gathering = struct.pack("<IQQHHIIII", self.gid, self.owner, self.host, self.minimum,
                                self.maximum, self.policy, self.argument, self.flags, self.state) + string(self.description)
        match = (struct.pack("<II", self.mode, len(self.attributes)) +
                 b"".join(struct.pack("<I", value) for value in self.attributes) +
                 struct.pack("<BI", self.open, self.system) + buffer(self.application) +
                 struct.pack("<IB", self.count, self.progress) + buffer(self.key) +
                 struct.pack("<I", self.option) + self.tail)
        return structure(gathering, self.gathering_revision) + structure(match, self.revision)

    def assign_codeword(self, code):
        """Locate the documented NEX 4.x codeword without discarding other fields."""
        reader = Reader(self.tail)
        _, parameters = reader.structure()
        count = parameters.number(4)
        if count > 64:
            raise ValueError("match_parameter_count")
        for _ in range(count):
            parameters.string()
            variant = parameters.number(1)
            if variant == 4:
                parameters.string()
            elif variant in (1, 2, 5, 6):
                parameters.take(8)
            elif variant == 3:
                parameters.take(1)
            elif variant != 0:
                raise ValueError("match_parameter_variant")
        parameters.finish()
        reader.number(8)  # started time
        reader.string()  # user password
        reader.number(4)  # referring gathering
        reader.take(2)  # password-enabled flags
        offset = reader.offset
        reader.string()
        reader.finish()
        self.tail = self.tail[:offset] + string(code)


class ControlPlane:
    def __init__(self, accounts, port=24873):
        self.port = port
        self.credentials = {}
        hello = self.call(op="hello", protocol=1)
        if hello.get("service") != "uscl-switch-online" or hello.get("protocol") != 1:
            raise ValueError("control_plane_version")
        for index, pid in enumerate(accounts):
            result = self.call(op="register", name=f"USCL Local {index + 1}")
            self.credentials[pid] = result["token"]

    def call(self, **request):
        raw = json.dumps(request, separators=(",", ":")).encode() + b"\n"
        if len(raw) > 16384:
            raise ValueError("control_plane_request_size")
        with socket.create_connection(("127.0.0.1", self.port), timeout=2) as stream:
            stream.sendall(raw)
            data = bytearray()
            while b"\n" not in data:
                chunk = stream.recv(min(4096, 16385 - len(data)))
                if not chunk:
                    raise ConnectionError("control_plane_incomplete")
                data.extend(chunk)
                if len(data) > 16384:
                    raise ValueError("control_plane_reply_size")
        if data.count(b"\n") != 1 or not data.endswith(b"\n"):
            raise ValueError("control_plane_frame")
        response = json.loads(data)
        if response.get("ok") is not True:
            raise PermissionError("control_plane_rejected")
        return response["result"]

    def heartbeat(self):
        for token in self.credentials.values():
            self.call(op="heartbeat", token=token)


@dataclass
class Room:
    session: MatchSession
    backend_id: str
    invite: str
    members: set = field(default_factory=set)
    code: str = ""


class NativeRooms:
    def __init__(self, authority, control):
        self.authority = authority
        self.control = control
        self.rooms = {}

    def create(self, pid, data):
        reader = Reader(data)
        _, request = reader.structure()
        session = MatchSession.read(request)
        additional = request.number(4)
        if additional != 0:
            raise ValueError("additional_participants_unsupported")
        request.number(4)  # participation-check gathering
        request.number(4)  # creation options
        request.string()  # join message
        participation = request.number(2)
        request.finish()
        reader.finish()
        if pid not in self.authority.registrations or not 1 <= participation <= 2 or not 2 <= session.maximum <= 12:
            raise PermissionError("room_identity_or_capacity")
        if any(pid in room.members for room in self.rooms.values()):
            raise PermissionError("already_in_room")
        code = "".join(secrets.choice("ABCDEFGHIJKLMNPQRSTUVWXYZ23456789") for _ in range(5))
        while any(room.code == code for room in self.rooms.values()):
            code = "".join(secrets.choice("ABCDEFGHIJKLMNPQRSTUVWXYZ23456789") for _ in range(5))
        session.assign_codeword(code)
        result = self.control.call(op="create_room", token=self.control.credentials[pid], game=GAME,
                                   region="NA", capacity=session.maximum, visibility="invite")
        gid = secrets.randbelow(0xfffffffe) + 1
        while gid in self.rooms:
            gid = secrets.randbelow(0xfffffffe) + 1
        session.gid, session.owner, session.host = gid, pid, pid
        session.count, session.key = participation, secrets.token_bytes(32)
        room = Room(session, result["room"], result["invite"], {pid}, code)
        self.rooms[gid] = room
        self.authority.playing_sessions[pid] = structure(struct.pack("<QIII", pid, gid, session.mode, session.attributes[0] if session.attributes else 0))
        return session.encode()

    def unregister(self, pid, data):
        reader = Reader(data)
        gid = reader.number(4)
        reader.finish()
        room = self.rooms.get(gid)
        if room is None:
            return b"\x00"
        if room.session.owner != pid:
            raise PermissionError("room_owner_required")
        self.control.call(op="leave_room", token=self.control.credentials[pid])
        del self.rooms[gid]
        for member in room.members:
            self.authority.playing_sessions.pop(member, None)
        return b"\x01"

    def disconnect(self, pid):
        for gid, room in list(self.rooms.items()):
            if pid not in room.members:
                continue
            self.control.call(op="leave_room", token=self.control.credentials[pid])
            if room.session.owner == pid:
                del self.rooms[gid]
                for member in room.members:
                    self.authority.playing_sessions.pop(member, None)
            else:
                room.members.remove(pid)
                room.session.count = len(room.members)
                self.authority.playing_sessions.pop(pid, None)

    def update_progress(self, pid, data):
        reader = Reader(data)
        gid, progress = reader.number(4), reader.number(1)
        reader.finish()
        room = self.rooms.get(gid)
        if room is None or room.session.owner != pid:
            raise PermissionError("room_owner_required")
        room.session.progress = progress
        return b""

    def get_code(self, pid, data):
        reader = Reader(data)
        gid = reader.number(4)
        reader.finish()
        room = self.rooms.get(gid)
        if room is None or pid not in room.members:
            raise PermissionError("room_member_required")
        expiry = datetime.now(timezone.utc) + timedelta(hours=6)
        stamp = (expiry.year << 26 | expiry.month << 22 | expiry.day << 17 |
                 expiry.hour << 12 | expiry.minute << 6 | expiry.second)
        return structure(string(room.code) + struct.pack("<Q", stamp))

    def resolve_code(self, pid, data):
        # The request wraps the code in a structure whose exact padding varies;
        # find the private code digits wherever they appear in the payload.
        for gid, room in self.rooms.items():
            if room.code.encode() in data:
                return struct.pack("<I", gid)
        return struct.pack("<I", 0)

    def end_participation(self, pid, data):
        reader = Reader(data)
        gid = reader.number(4)
        reader.string()
        reader.finish()
        if gid not in self.rooms or pid not in self.rooms[gid].members:
            return b"\x01"
        self.disconnect(pid)
        return b"\x01"
