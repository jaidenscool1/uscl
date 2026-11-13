import asyncio
import ipaddress
import socket
import struct
import unittest

from nat import NatGroup


class NatTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_loopback_nat_observations_and_alternate_source(self):
        reports = []
        group = NatGroup(lambda name, **fields: reports.append((name, fields)), ports=(0, 0))
        await group.start()
        try:
            with socket.socket(type=socket.SOCK_DGRAM) as client:
                client.bind(("127.0.0.1", 0))
                client.setblocking(False)
                loop = asyncio.get_running_loop()
                for kind in (1, 2, 3, 4, 5, 101, 102, 103):
                    await loop.sock_sendto(client, struct.pack(">IIII", kind, 0, 0, 0), (group.hosts[0], group.ports[0]))
                    data, source = await asyncio.wait_for(loop.sock_recvfrom(client, 1024), 1)
                    type_, port, external, server = struct.unpack(">IIII", data)
                    self.assertEqual(type_, kind)
                    self.assertEqual(port, client.getsockname()[1])
                    self.assertEqual(external, int(ipaddress.ip_address("127.0.0.1")))
                    self.assertEqual(server, int(ipaddress.ip_address(source[0])))
                    self.assertEqual(source[0], group.hosts[1] if kind == 2 else group.hosts[0])
                    self.assertEqual(source[1], group.ports[1] if kind in (2, 3, 102) else group.ports[0])
                await loop.sock_sendto(client, b"short", (group.hosts[0], group.ports[0]))
                with self.assertRaises(TimeoutError):
                    await asyncio.wait_for(loop.sock_recvfrom(client, 1024), 0.05)
            self.assertEqual(sum(name == "nat_check" for name, _ in reports), 8)
        finally:
            group.close()


if __name__ == "__main__":
    unittest.main()
