"""Protocol tests; actual game compatibility is recorded separately."""
import contextlib
import io
import struct
import unittest

from adapter import HEADER, Session, options_decode, packet


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.output = io.StringIO()
        self.quiet = contextlib.redirect_stdout(self.output)
        self.quiet.__enter__()
        self.session = Session()

    def tearDown(self):
        self.quiet.__exit__(None, None, None)

    def connect(self):
        syn = packet(0xaa, 31, 1, 0, 4, 0, b"\0\x04\x06\0\0\0")
        response = self.session.receive(syn)
        header = HEADER.unpack_from(response[0])
        self.assertEqual(header[4:6], (1, 31))
        self.assertEqual(header[7] & 15, 0)
        options = options_decode(response[0][12:])
        self.assertEqual(options[0], b"\x06\0\0\0")
        self.assertEqual(len(options[1]), 16)
        self.assertEqual(self.session.receive(syn), response, "retransmission must be idempotent")
        reply = self.session.receive(packet(0xaa, 31, 1, 1, 4, 1, b"\0\x04\x06\0\0\0"))[0]
        self.assertEqual(HEADER.unpack_from(reply)[7], 0x11)

    def test_connection_and_rpc_error(self):
        self.connect()
        body = b"\x8a" + struct.pack("<II", 1, 2) + b"private-token"
        rpc = struct.pack("<I", len(body)) + body
        replies = self.session.receive(packet(0xaa, 31, 1, 2, 6, 2, payload=rpc))
        self.assertEqual(len(replies), 2)
        self.assertEqual(HEADER.unpack_from(replies[0])[7], 0x12)
        self.assertNotIn("private-token", self.output.getvalue())
        self.assertIn('"protocol": 10', self.output.getvalue())
        self.assertEqual(replies[1][12 + 5], 0, "unsupported RPC must fail")

    def test_reject_data_before_connect_and_bad_options(self):
        for raw in (b"\x80", packet(0xaa, 31, 1, 2, 6, 1, payload=b"bad"),
                    packet(0xaa, 31, 1, 0, 4, 0, b"\0\x04\x01")):
            with self.assertRaises(ValueError):
                Session().receive(raw)

    def test_fragments_and_duplicate_rpc(self):
        self.connect()
        body = b"\x8a" + struct.pack("<II", 1, 2)
        rpc = struct.pack("<I", len(body)) + body
        self.assertEqual(len(self.session.receive(packet(0xaa, 31, 1, 2, 6, 2, payload=rpc[:5], fragment=1))), 1)
        final = packet(0xaa, 31, 1, 2, 6, 3, payload=rpc[5:])
        response = self.session.receive(final)
        self.assertEqual(self.session.receive(final), response)
        self.assertEqual(self.output.getvalue().count('"rmc_request"'), 1)


if __name__ == "__main__":
    unittest.main()
