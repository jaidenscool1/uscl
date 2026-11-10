from pathlib import Path
import struct
import subprocess
import unittest

from auth import Authority, Reader, string, structure
from rooms import ControlPlane, MatchSession, NativeRooms, GAME


def sample():
    tail = structure(struct.pack("<I", 0)) + struct.pack("<Q", 0) + string("") + struct.pack("<I", 0) + b"\0\0" + string("")
    return MatchSession(0, 999, 999, 1, 12, 0, 0, 0, 0, "", 1, [0] * 6,
                        1, 0, b"game-metadata", 0, 0, b"", 0, tail)


def create_request(session):
    return structure(session.encode() + struct.pack("<III", 0, 0, 0) + string("") + struct.pack("<H", 1))


class RoomTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = Path(__file__).resolve().parents[2] / "build/switch-online-target/debug/uscl-switch-online"
        if not binary.exists():
            raise unittest.SkipTest("build the Rust control plane first")
        cls.backend = subprocess.Popen([str(binary), "--port", "0"], stdout=subprocess.PIPE, text=True)
        line = cls.backend.stdout.readline().strip()
        if not line.startswith("USCL_SWITCH_ONLINE_READY "):
            cls.backend.terminate()
            raise RuntimeError("control plane failed readiness")
        cls.port = int(line.split()[1])

    @classmethod
    def tearDownClass(cls):
        cls.backend.terminate()
        cls.backend.wait(timeout=5)
        cls.backend.stdout.close()

    def test_native_creation_is_real_private_backend_membership(self):
        control = ControlPlane([123, 456], self.port)
        authority = Authority([(123, "a" * 64), (456, "b" * 64)])
        authority.registrations[123] = (1, "prudp:/address=127.0.0.1;port=30001")
        rooms = NativeRooms(authority, control)
        result = Reader(rooms.create(123, create_request(sample())))
        match = MatchSession.read(result)
        result.finish()
        self.assertNotEqual(match.gid, 0)
        code = rooms.rooms[match.gid].code
        self.assertEqual(rooms.resolve_code(456, string(code)), struct.pack("<I", match.gid))
        self.assertEqual(rooms.resolve_code(456, string("000000000000")), struct.pack("<I", 0))
        self.assertEqual((match.owner, match.host, match.count), (123, 123, 1))
        self.assertEqual(len(match.key), 32)
        self.assertEqual(match.application, b"game-metadata")
        self.assertEqual(match.tail[:-8], sample().tail[:-3])
        self.assertEqual(len(match.tail), len(sample().tail) + 5)
        backend_room = control.call(op="room", token=control.credentials[123])
        self.assertEqual(backend_room["room"], rooms.rooms[match.gid].backend_id)
        self.assertEqual(len(backend_room["members"]), 1)
        discovery = control.call(op="list_rooms", token=control.credentials[456], game=GAME)
        self.assertFalse(discovery["rooms"], "native private room leaked into public discovery")
        with self.assertRaises(PermissionError):
            rooms.unregister(456, struct.pack("<I", match.gid))
        self.assertEqual(rooms.unregister(123, struct.pack("<I", match.gid)), b"\x01")
        self.assertNotIn(123, authority.playing_sessions)

    def test_match_object_bounds(self):
        raw = sample().encode()
        for data in (raw[:4], raw[:-1]):
            with self.assertRaises(ValueError):
                MatchSession.read(Reader(data))


if __name__ == "__main__":
    unittest.main()
