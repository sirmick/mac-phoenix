#!/usr/bin/env python3
"""Integration tests for net-bridge using Scapy.

Spawns the Rust net-bridge process, connects via Unix socket,
and tests networking at the ethernet frame level.
"""

import os
import socket
import struct
import subprocess
import sys
import time
import unittest

from scapy.all import (
    ARP, BOOTP, DHCP, DNS, DNSQR, Ether, ICMP, IP, Raw, TCP, UDP, raw,
)

BRIDGE_BIN = os.path.join(os.path.dirname(__file__), "..", "net-bridge", "target", "release", "net-bridge")
SOCK_PATH = "/tmp/mac-ether-test.sock"

MAC_ADDR = "02:50:48:58:00:01"
GW_MAC = "02:50:48:58:00:02"
MAC_IP = "10.0.2.15"
GW_IP = "10.0.2.1"


class NetBridgeTest(unittest.TestCase):
    """Test suite for the Rust net-bridge."""

    bridge_proc = None
    sock = None

    @classmethod
    def setUpClass(cls):
        # Clean up stale socket
        try:
            os.unlink(SOCK_PATH)
        except FileNotFoundError:
            pass

        # Start bridge
        env = dict(os.environ, RUST_LOG="info")
        cls.bridge_proc = subprocess.Popen(
            [BRIDGE_BIN, "--socket", SOCK_PATH],
            env=env,
            stderr=subprocess.PIPE,
        )

        # Wait for socket to appear
        for _ in range(50):
            if os.path.exists(SOCK_PATH):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Bridge didn't create socket")

        # Connect
        cls.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        cls.sock.connect(SOCK_PATH)
        cls.sock.setblocking(False)
        time.sleep(0.2)  # Let bridge initialize

    @classmethod
    def tearDownClass(cls):
        if cls.sock:
            cls.sock.close()
        if cls.bridge_proc:
            cls.bridge_proc.terminate()
            cls.bridge_proc.wait(timeout=5)
        try:
            os.unlink(SOCK_PATH)
        except FileNotFoundError:
            pass

    def send_frame(self, pkt):
        """Send a Scapy packet as a length-prefixed ethernet frame."""
        data = raw(pkt)
        header = struct.pack(">I", len(data))
        self.sock.sendall(header + data)

    def recv_frame(self, timeout=3.0):
        """Receive a length-prefixed ethernet frame, return as Scapy Ether."""
        self.sock.setblocking(True)
        self.sock.settimeout(timeout)
        try:
            header = b""
            while len(header) < 4:
                header += self.sock.recv(4 - len(header))
            frame_len = struct.unpack(">I", header)[0]
            data = b""
            while len(data) < frame_len:
                data += self.sock.recv(frame_len - len(data))
            return Ether(data)
        except (socket.timeout, BlockingIOError):
            return None
        finally:
            self.sock.setblocking(False)

    def recv_until(self, predicate, timeout=3.0):
        """Receive frames until one matches predicate or timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            pkt = self.recv_frame(timeout=remaining)
            if pkt is None:
                break
            if predicate(pkt):
                return pkt
        return None

    # ---- Tests ----

    def test_01_arp(self):
        """ARP: resolve gateway MAC address."""
        pkt = Ether(src=MAC_ADDR, dst="ff:ff:ff:ff:ff:ff") / ARP(
            pdst=GW_IP, psrc=MAC_IP, hwsrc=MAC_ADDR, op="who-has"
        )
        self.send_frame(pkt)

        reply = self.recv_until(
            lambda p: ARP in p and p[ARP].op == 2  # is-at
        )
        self.assertIsNotNone(reply, "No ARP reply received")
        self.assertEqual(reply[ARP].psrc, GW_IP)
        self.assertEqual(reply[ARP].hwsrc, GW_MAC)
        print(f"  ARP: {GW_IP} is at {reply[ARP].hwsrc}")

    def test_02_ping_gateway(self):
        """ICMP: ping the gateway (local, no internet needed)."""
        pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=GW_IP)
            / ICMP(type=8, id=0x5678, seq=1)
            / Raw(b"\xaa" * 8)
        )
        self.send_frame(pkt)

        reply = self.recv_until(
            lambda p: (
                ICMP in p
                and p[ICMP].type == 0  # echo reply
                and p[ICMP].id == 0x5678
            )
        )
        self.assertIsNotNone(reply, "No ICMP echo reply from gateway")
        self.assertEqual(reply[IP].src, GW_IP)
        self.assertEqual(reply[ICMP].seq, 1)
        print(f"  Ping gateway: reply from {reply[IP].src}")

    def test_03_tcp_echo(self):
        """TCP: full connection through NAT to localhost echo server."""
        import threading

        # Start echo server
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        srv_port = srv.getsockname()[1]
        srv.listen(1)

        def echo_serve():
            conn, _ = srv.accept()
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                conn.sendall(data)
            conn.close()
            srv.close()

        t = threading.Thread(target=echo_serve, daemon=True)
        t.start()

        # TCP handshake via Scapy frames through the NAT
        DST = "127.0.0.1"
        SPORT = 44444

        # SYN
        syn = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=DST)
            / TCP(sport=SPORT, dport=srv_port, seq=1000, flags="S")
        )
        self.send_frame(syn)

        # Wait for SYN-ACK
        synack = self.recv_until(
            lambda p: (
                TCP in p
                and p[TCP].dport == SPORT
                and p[TCP].flags.S and p[TCP].flags.A
            ),
            timeout=5,
        )
        self.assertIsNotNone(synack, "No SYN-ACK received")
        their_seq = synack[TCP].seq
        our_seq = synack[TCP].ack  # Should be 1001 (SYN consumed 1)
        self.assertEqual(our_seq, 1001, f"SYN-ACK ack wrong: {our_seq}")
        print(f"  TCP: SYN-ACK received, their_seq={their_seq}")

        # ACK (complete handshake)
        ack = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=DST)
            / TCP(sport=SPORT, dport=srv_port, seq=our_seq, ack=their_seq + 1, flags="A")
        )
        self.send_frame(ack)
        time.sleep(0.1)

        # Send data
        test_data = b"Hello from Mac!"
        data_pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=DST)
            / TCP(sport=SPORT, dport=srv_port, seq=our_seq, ack=their_seq + 1, flags="PA")
            / Raw(test_data)
        )
        self.send_frame(data_pkt)

        # Wait for echoed data
        received = b""
        deadline = time.time() + 5
        while len(received) < len(test_data) and time.time() < deadline:
            pkt = self.recv_frame(timeout=deadline - time.time())
            if pkt and TCP in pkt and pkt[TCP].dport == SPORT and Raw in pkt:
                received += bytes(pkt[Raw])
            elif pkt and TCP in pkt and pkt[TCP].flags.R:
                self.fail("Got RST")

        self.assertEqual(received, test_data, f"Echo mismatch: {received!r}")
        print(f"  TCP: echoed data verified: {received!r}")

        # FIN
        fin = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=DST)
            / TCP(sport=SPORT, dport=srv_port, seq=our_seq + len(test_data),
                  ack=their_seq + 1 + len(test_data), flags="FA")
        )
        self.send_frame(fin)
        time.sleep(0.5)
        # Drain any remaining frames
        while self.recv_frame(timeout=0.1):
            pass
        print("  TCP: connection closed")


    def test_04_udp_echo(self):
        """UDP: send/receive through NAT to localhost echo server."""
        import threading

        # Start UDP echo server
        srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        srv.bind(("127.0.0.1", 0))
        srv_port = srv.getsockname()[1]
        srv.settimeout(5)

        def udp_echo_serve():
            try:
                data, addr = srv.recvfrom(4096)
                srv.sendto(data, addr)
            except socket.timeout:
                pass
            finally:
                srv.close()

        t = threading.Thread(target=udp_echo_serve, daemon=True)
        t.start()

        # Send UDP frame through NAT
        DST = "127.0.0.1"
        SPORT = 55555
        test_data = b"UDP test!"

        pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=DST)
            / UDP(sport=SPORT, dport=srv_port)
            / Raw(test_data)
        )
        self.send_frame(pkt)

        # Wait for UDP reply
        reply = self.recv_until(
            lambda p: (
                UDP in p
                and p[UDP].dport == SPORT
                and Raw in p
            ),
            timeout=5,
        )
        self.assertIsNotNone(reply, "No UDP reply received")
        self.assertEqual(bytes(reply[Raw]), test_data, f"Echo mismatch: {bytes(reply[Raw])!r}")
        print(f"  UDP: echoed data verified: {bytes(reply[Raw])!r}")

    def test_05_dns(self):
        """DNS: query through gateway DNS proxy (requires internet)."""
        pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst=GW_IP)
            / UDP(sport=1234, dport=53)
            / DNS(rd=1, qd=DNSQR(qname="example.com"))
        )
        self.send_frame(pkt)

        reply = self.recv_until(
            lambda p: (
                UDP in p
                and p[UDP].dport == 1234
                and DNS in p
            ),
            timeout=5,
        )
        if reply is None:
            print("  DNS: SKIP (no reply in 5s, may need internet)")
            return

        dns = reply[DNS]
        self.assertEqual(dns.qr, 1, "Expected DNS response (qr=1)")
        self.assertEqual(dns.rcode, 0, f"DNS error: rcode={dns.rcode}")
        self.assertGreater(dns.ancount, 0, "No answers in DNS response")
        print(f"  DNS: got {dns.ancount} answer(s) for example.com")


    def test_06_traceroute_ttl1(self):
        """Traceroute: TTL=1 packet gets ICMP Time Exceeded (local, no internet)."""
        pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst="1.2.3.4", ttl=1)
            / UDP(sport=33434, dport=33434)
            / Raw(b"trace")
        )
        self.send_frame(pkt)

        reply = self.recv_until(
            lambda p: (
                ICMP in p
                and p[ICMP].type == 11  # Time Exceeded
            ),
            timeout=3,
        )
        self.assertIsNotNone(reply, "No ICMP Time Exceeded received")
        self.assertEqual(reply[IP].src, GW_IP, f"Time Exceeded src should be gateway, got {reply[IP].src}")
        self.assertEqual(reply[ICMP].type, 11)
        self.assertEqual(reply[ICMP].code, 0)
        print(f"  Traceroute: Time Exceeded from {reply[IP].src}")

    def test_07_ping_real(self):
        """ICMP: ping external host through NAT (requires internet + unprivileged ICMP)."""
        pkt = (
            Ether(src=MAC_ADDR, dst=GW_MAC)
            / IP(src=MAC_IP, dst="8.8.8.8")
            / ICMP(type=8, id=0x1234, seq=1)
            / Raw(b"\xaa" * 8)
        )
        self.send_frame(pkt)

        reply = self.recv_until(
            lambda p: (
                ICMP in p
                and p[ICMP].type == 0  # echo reply
                and p[IP].src == "8.8.8.8"
            ),
            timeout=5,
        )
        if reply is None:
            print("  Ping real: SKIP: no internet or unprivileged ICMP unavailable")
            return

        self.assertEqual(reply[IP].src, "8.8.8.8")
        self.assertEqual(reply[ICMP].id, 0x1234, f"ICMP id mismatch: {reply[ICMP].id:#06x}")
        self.assertEqual(reply[ICMP].seq, 1)
        print(f"  Ping real: reply from {reply[IP].src}, id={reply[ICMP].id:#06x}")


    def test_08_dhcp(self):
        """DHCP: discover/offer/request/ack cycle."""
        xid = 0x12345678

        # DHCP DISCOVER (broadcast)
        discover = (
            Ether(src=MAC_ADDR, dst="ff:ff:ff:ff:ff:ff")
            / IP(src="0.0.0.0", dst="255.255.255.255")
            / UDP(sport=68, dport=67)
            / BOOTP(op=1, xid=xid, chaddr=bytes.fromhex(MAC_ADDR.replace(":", "")))
            / DHCP(options=[("message-type", "discover"), "end"])
        )
        self.send_frame(discover)

        # Wait for OFFER
        offer = self.recv_until(
            lambda p: (
                BOOTP in p
                and p[BOOTP].op == 2  # BOOTREPLY
                and p[BOOTP].xid == xid
            ),
            timeout=3,
        )
        self.assertIsNotNone(offer, "No DHCP OFFER received")
        offered_ip = offer[BOOTP].yiaddr
        self.assertEqual(offered_ip, "10.0.2.15", f"Wrong offered IP: {offered_ip}")

        # Extract DHCP options
        dhcp_opts = {opt[0]: opt[1] for opt in offer[DHCP].options if isinstance(opt, tuple)}
        self.assertEqual(dhcp_opts.get("message-type"), 2, "Expected OFFER (type 2)")
        print(f"  DHCP: OFFER received, IP={offered_ip}")

        # DHCP REQUEST
        request = (
            Ether(src=MAC_ADDR, dst="ff:ff:ff:ff:ff:ff")
            / IP(src="0.0.0.0", dst="255.255.255.255")
            / UDP(sport=68, dport=67)
            / BOOTP(op=1, xid=xid, chaddr=bytes.fromhex(MAC_ADDR.replace(":", "")))
            / DHCP(options=[
                ("message-type", "request"),
                ("requested_addr", "10.0.2.15"),
                ("server_id", GW_IP),
                "end",
            ])
        )
        self.send_frame(request)

        # Wait for ACK
        ack = self.recv_until(
            lambda p: (
                BOOTP in p
                and p[BOOTP].op == 2
                and p[BOOTP].xid == xid
                and DHCP in p
            ),
            timeout=3,
        )
        self.assertIsNotNone(ack, "No DHCP ACK received")
        ack_ip = ack[BOOTP].yiaddr
        self.assertEqual(ack_ip, "10.0.2.15")

        ack_opts = {opt[0]: opt[1] for opt in ack[DHCP].options if isinstance(opt, tuple)}
        self.assertEqual(ack_opts.get("message-type"), 5, "Expected ACK (type 5)")

        # Verify options
        self.assertEqual(ack_opts.get("subnet_mask"), "255.255.255.0")
        self.assertEqual(ack_opts.get("router"), GW_IP)
        self.assertEqual(ack_opts.get("name_server"), GW_IP)
        print(f"  DHCP: ACK received, IP={ack_ip}, gateway={ack_opts.get('router')}, DNS={ack_opts.get('name_server')}")


if __name__ == "__main__":
    # Ensure bridge binary exists
    if not os.path.exists(BRIDGE_BIN):
        print(f"ERROR: Bridge binary not found at {BRIDGE_BIN}")
        print("Build with: cd net-bridge && cargo build --release")
        sys.exit(1)

    unittest.main(verbosity=2)
