import struct
import unittest

from auth import Authority, Reader, buffer, decrypt, encrypt, login_parameters, rc4, string, structure


def login_request(pid=123, token="a" * 64):
    info = structure(b"") + structure(string(token) + struct.pack("<IBI", 4, 2, 20500))
    return string(f"{pid:016x}") + string("AuthenticationInfo") + struct.pack("<I", len(info) + 4) + buffer(info)


class AuthTests(unittest.TestCase):
    def test_rc4_published_vector_and_integrity(self):
        # RFC 6229, 40-bit key, first 16 octets at offset zero.
        self.assertEqual(rc4(bytes([1, 2, 3, 4, 5]), bytes(16)).hex(), "b2396305f03dc027ccc3524a0a1118a8")
        sealed = encrypt(b"synthetic-test-key", b"ticket payload")
        with self.assertRaises(ValueError):
            decrypt(b"synthetic-test-key", sealed[:-1] + bytes([sealed[-1] ^ 1]))

    def test_private_credentials_and_secure_connection(self):
        authority = Authority([(123, "a" * 64)])
        pid, response = authority.login(login_request())
        self.assertEqual(pid, 123)
        parsed = Reader(response)
        self.assertEqual(parsed.number(4), 0x10001)
        self.assertEqual(parsed.number(8), 123)
        ticket = parsed.buffer()
        _, connection = parsed.structure()
        self.assertIn("address=0.0.0.1", connection.string())
        self.assertEqual(connection.number(4), 0)
        self.assertEqual(connection.string(), "")
        connection.number(8)
        connection.finish()
        parsed.string()  # build name
        source_key = bytes.fromhex(parsed.string())
        parsed.finish()
        decrypted = Reader(decrypt(source_key, ticket))
        session = decrypted.take(32)
        self.assertEqual(decrypted.number(8), 2)
        internal = decrypted.buffer()
        decrypted.finish()
        request = buffer(internal) + buffer(encrypt(session, struct.pack("<QII", 123, 4, 0xffffffff)))
        identity, connection_id, reply = authority.connect(request)
        self.assertEqual((identity, connection_id), (123, 4))
        self.assertEqual(reply, buffer(bytes(4)))
        with self.assertRaises(PermissionError):
            authority.connect(buffer(internal) + buffer(encrypt(session, struct.pack("<QII", 124, 4, 1))))

    def test_unprovisioned_and_expired_credentials_fail(self):
        now = [0.0]
        authority = Authority([(123, "a" * 64)], clock=lambda: now[0])
        for data in (login_request(token="b" * 64), login_request(pid=124), login_request(token="")):
            with self.assertRaises(PermissionError):
                authority.login(data)
        authority.login(login_request())
        self.assertEqual(len(authority.tickets), 1)
        now[0] = 121
        authority.cleanup()
        self.assertFalse(authority.tickets)

    def test_request_bounds(self):
        request = login_request()
        for truncated in (request[:5], request[:-1], request + b"trailing"):
            with self.assertRaises(ValueError):
                login_parameters(truncated)

    def test_empty_friend_and_competition_queries_and_bounds(self):
        authority = Authority()
        self.assertEqual(authority.playing(struct.pack("<I", 0) + b"\0\0"), bytes(4))
        query = structure(b"\0" + structure(struct.pack("<II", 0, 100)))
        self.assertEqual(authority.competition_info(query), bytes(4))
        with self.assertRaises(ValueError):
            authority.playing(struct.pack("<I", 257))
        with self.assertRaises(ValueError):
            authority.competition_info(query + b"trailing")

    def test_station_port_follows_latest_nat_check_from_that_console(self):
        from nat import NatGroup
        authority = Authority()
        authority.nat = NatGroup(lambda *a, **k: None)
        authority.addresses[7] = "127.0.0.11"
        authority.p2p_ports[7] = 60102  # first session
        authority.nat.ports_by_address.update({"127.0.0.11": 57283, "127.0.0.12": 52945})
        url = authority.observed_station(7, "prudp:/address=127.0.0.1;port=1;PID=7;natm=0")
        self.assertIn("address=127.0.0.11;port=57283;", url)

    def test_registration_uses_authenticated_identity_and_loopback(self):
        authority = Authority([(123, "a" * 64)])
        request = struct.pack("<I", 1) + string("prudps:/address=example.invalid;port=30001;PID=999;CID=999")
        result = Reader(authority.register(123, request))
        self.assertEqual(result.number(4), 0x10001)
        self.assertEqual(result.number(4), 1)
        url = result.string()
        self.assertRegex(url, r"address=127.0.0.(1[0-9]|[2-9][0-9]|1[0-9]{2}|2[0-4][0-9]);")
        self.assertIn("PID=123", url)
        self.assertNotIn("example.invalid", url)
        self.assertNotIn("PID=999", url)
        self.assertIn("CID=999", url, "client connection nonce must survive public URL registration")
        with self.assertRaises(PermissionError):
            authority.register(999, request)


if __name__ == "__main__":
    unittest.main()
