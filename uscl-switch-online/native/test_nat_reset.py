import asyncio, socket, struct, sys, unittest

from nat import NatGroup


class NatResetTest(unittest.TestCase):
    def test_keeps_answering_after_reply_to_closed_port(self):
        async def run():
            group = NatGroup(lambda *a, **k: None, hosts=("127.0.0.1", "127.0.0.2"), ports=(0, 0))
            await group.start()
            server = ("127.0.0.1", group.ports[0])
            for _ in range(3):  # each reply lands on an already-closed port
                gone = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                gone.sendto(struct.pack(">IIII", 101, 0, 0, 0), server)
                gone.close()
            await asyncio.sleep(0.3)
            client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            client.settimeout(0)
            client.sendto(struct.pack(">IIII", 101, 0, 0, 0), server)
            reply = None
            for _ in range(40):
                await asyncio.sleep(0.05)
                try:
                    reply = client.recv(64)
                    break
                except BlockingIOError:
                    pass
            client.close()
            group.close()
            return reply
        reply = asyncio.run(run())
        self.assertIsNotNone(reply)
        self.assertEqual(struct.unpack(">I", reply[:4])[0], 101)


if __name__ == "__main__":
    unittest.main()
