#!/usr/bin/env python3
"""TCP + UDP echo on every interface, one port for both, for guest network tests.

The ROM machines reach net-bridge's echo service at 10.0.2.1:7; Executor's
MacTCP is on host sockets, so its guest tests use this one instead.

    echo_server.py PORTFILE   # binds a free port, writes it to PORTFILE

Every interface, not only loopback: MacTCP applications (GUSI) send
127.0.0.1 to the address ipctlGetAddr reports, the host's own.
"""
import selectors
import socket
import sys


def main():
    sel = selectors.DefaultSelector()
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind(("0.0.0.0", 0))
    port = tcp.getsockname()[1]
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("0.0.0.0", port))
    tcp.listen(8)
    tcp.setblocking(False)
    udp.setblocking(False)
    sel.register(tcp, selectors.EVENT_READ, "accept")
    sel.register(udp, selectors.EVENT_READ, "udp")
    with open(sys.argv[1], "w") as f:
        f.write(f"{port}\n")
    while True:
        for key, _ in sel.select():
            sock = key.fileobj
            if key.data == "accept":
                conn, _ = sock.accept()
                conn.setblocking(False)
                sel.register(conn, selectors.EVENT_READ, "conn")
            elif key.data == "udp":
                data, addr = sock.recvfrom(65535)
                sock.sendto(data, addr)
            else:
                try:
                    data = sock.recv(4096)
                except OSError:
                    data = b""
                if data:
                    sock.sendall(data)
                else:
                    sel.unregister(sock)
                    sock.close()


if __name__ == "__main__":
    main()
