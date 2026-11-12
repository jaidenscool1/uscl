"""Loopback-only Nintendo NAT-check wire adapter; not an internet NAT service.

Packet layout: https://github.com/kinnay/NintendoClients/wiki/NAT-Check-Server
Replies use the actual source address/port observed on our local test network.
"""
import asyncio
import ipaddress
import struct
import time


class NatEndpoint(asyncio.DatagramProtocol):
    def __init__(self, group, host, port):
        self.group, self.host, self.port = group, host, port
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, address):
        if not ipaddress.ip_address(address[0]).is_loopback or not self.group.admit():
            return
        if len(data) != 16:
            self.group.report("nat_rejected", observed_bytes=len(data))
            return
        kind = struct.unpack_from(">I", data)[0]
        if kind not in (1, 2, 3, 4, 5, 101, 102, 103):
            return
        host, port = self.host, self.port
        if kind == 2:
            host = self.group.hosts[1] if host == self.group.hosts[0] else self.group.hosts[0]
        if kind in (2, 3, 102):
            port = self.group.ports[1] if port == self.group.ports[0] else self.group.ports[0]
        target = self.group.endpoints.get((host, port))
        if target:
            response = struct.pack(">IIII", kind, address[1], int(ipaddress.ip_address(address[0])), int(ipaddress.ip_address(host)))
            target.transport.sendto(response, address)
            # The game's peer-to-peer socket sends these checks; its port is what other players must dial.
            self.group.observed_port = address[1]
            self.group.report("nat_check", message_type=kind, alternate_address=kind == 2, alternate_port=kind in (2, 3, 102), source_port=address[1])


class NatGroup:
    def __init__(self, report, hosts=("127.0.0.1", "127.0.0.2"), ports=(10025, 10125)):
        self.report = report
        self.hosts, self.ports = hosts, ports
        self.endpoints = {}
        self.observed_port = None
        self.tokens, self.last = 128.0, time.monotonic()

    def admit(self):
        now = time.monotonic()
        self.tokens = min(128.0, self.tokens + (now - self.last) * 64)
        self.last = now
        if self.tokens < 1:
            return False
        self.tokens -= 1
        return True

    async def start(self):
        loop = asyncio.get_running_loop()
        try:
            selected_ports = []
            for index, host in enumerate(self.hosts):
                for port in self.ports:
                    _, endpoint = await loop.create_datagram_endpoint(lambda h=host, p=port: NatEndpoint(self, h, p), local_addr=(host, port))
                    actual_port = endpoint.transport.get_extra_info("sockname")[1]
                    endpoint.port = actual_port
                    self.endpoints[(host, actual_port)] = endpoint
                    if index == 0:
                        selected_ports.append(actual_port)
                if index == 0:
                    self.ports = tuple(selected_ports)
        except BaseException:
            self.close()
            raise

    def close(self):
        for endpoint in self.endpoints.values():
            endpoint.transport.close()
        self.endpoints.clear()
