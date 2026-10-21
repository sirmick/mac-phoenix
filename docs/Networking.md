# Networking

Guest networking is `net-bridge` (`net-bridge/`, Rust), a separate process
the emulator talks to over a Unix socket. It pairs a userspace TCP/IP stack
(smoltcp) with a NAT that carries the guest's TCP, UDP and ICMP through
ordinary host sockets, so the Mac reaches the Internet without root,
`CAP_NET_ADMIN` or a TUN/TAP device. It is built with the emulator when
cargo is installed (`-DBUILD_NET_BRIDGE=OFF` skips it).

ROM machines only: Executor has no Ethernet; its MacTCP and Open Transport
go straight to host sockets ([executor/README.md](executor/README.md)).

## Turning it on

Settings → Network → NAT, or:

```bash
./build/mac-phoenix --network socket ~/storage/roms/quadra.rom --disk 7.5.5.img
```

The emulator connects to `/tmp/mac-ether.sock`, starting `net-bridge`
there if no bridge is listening. It looks for the binary next to
`mac-phoenix` (the build puts a link there), then `/usr/local/bin` and
`/usr/bin`. A bridge the emulator started is stopped when it exits.

In the guest, set TCP/IP (or MacTCP) to configure via DHCP:

| | |
|---|---|
| Gateway and DNS | `10.0.2.1` (DNS is forwarded to the host's resolver) |
| Netmask | `255.255.255.0` |
| Guests | `10.0.2.15` – `10.0.2.250`, one lease per MAC address |

On the gateway, for tests from inside the guest: an echo service on port 7
(TCP and UDP) and, on TCP port 8, a bulk stream of a known byte pattern.

## Several guests on one bridge

`net-bridge` is a virtual Ethernet switch with any number of ports. Every
emulator started with plain `--network socket` joins the same bridge, so
the guests share one segment:

```bash
./build/mac-phoenix --port 8001 --network socket --disk 7.6.1.img ~/storage/roms/quadra.rom &
./build/mac-phoenix --port 8002 --network socket --disk 7.5.5.img ~/storage/roms/quadra.rom &
```

Each emulator's MAC address is `02:50:48:58` ("PHX") plus the low 16 bits
of its PID, so each gets its own DHCP lease.

* **IP** between guests is switched at layer 2; no NAT involved.
* **AppleTalk**: turn on AppleTalk over Ethernet in each guest and the
  Chooser sees the other Mac (file sharing, printers). The bridge passes
  EtherTalk frames (DDP, AARP) through as plain Ethernet; there is no
  AppleTalk on the host.
* **Broadcast and multicast** (ARP, NBP, AppleTalk's `09:00:07:*`) go to
  every port.

For an isolated network give it another socket:
`--network socket:/path/to/other.sock`. The test scripts do this so tests
running in parallel don't share a bridge.

## Running the bridge yourself

To watch it or debug it, start it first; the emulator joins it:

```bash
RUST_LOG=debug ./build/net-bridge --socket /tmp/mac-ether.sock
./build/mac-phoenix --network socket:/tmp/mac-ether.sock ~/storage/roms/quadra.rom
```

`--debug-network` logs the emulator's side.

## Ping

ICMP goes through Linux's unprivileged ICMP sockets, which the kernel
allows only for groups in `net.ipv4.ping_group_range`. If `ping` in the
guest fails while TCP, UDP and DNS work:

```bash
sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"
echo 'net.ipv4.ping_group_range = 0 2147483647' | sudo tee /etc/sysctl.d/99-mac-phoenix-ping.conf
```

Traceroute works without it: classic Mac traceroute sends UDP, and the
ICMP time-exceeded replies come back through the UDP NAT.
