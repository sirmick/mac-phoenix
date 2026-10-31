"""The relay's endpoint: /relay, a WebSocket for signed-in users.

Executor's MacTCP and Open Transport in the tab reach the network through
here (host/wasm/net.cpp, web/executor/net.js): TCP connections out, UDP
datagrams to and from the hosts the tab sent to, and DNS on this host's
resolver. Nothing listens for the tab: its listeners and loopback stay in
the tab. The protocol is in docs/executor/DIRECTION.md ("The network
relay"); one binary WebSocket message is one relay message:

    op (u8), id (u32), then the op's fields; big-endian

Limits ([relay] in the configuration): destination ports from an
allow-list; no private, loopback, link-local, multicast or reserved
addresses unless allow_private; max_sockets per user (all their
connections together) and max_connections per user; WINDOW bytes in
flight each way per TCP socket, so a connection's buffers stay small.
"""
import base64
import errno
import hashlib
import ipaddress
import random
import selectors
import socket
import struct
import threading
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
VERSION = 1
WINDOW = 64 * 1024            # unacknowledged bytes each way, per TCP socket
MAX_MESSAGE = 128 * 1024      # one WebSocket message from the tab
MAX_DATAGRAM = 65507
MAX_LOOKUPS = 8               # at once, per connection
PING_EVERY = 30               # seconds; a connection silent 3x this is dead

# Tab -> relay.
CONNECT, UDP, SEND, SENDTO, SHUTDOWN, CLOSE, CREDIT = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
LOOKUP, REVERSE, QUERY = 0x10, 0x11, 0x12
# Relay -> tab.
HELLO, CONNECTED, DATA, DATAFROM, EOF, ERROR, ACK = 0x80, 0x81, 0x83, 0x84, 0x85, 0x86, 0x87
ADDRS, NAME, ANSWER = 0x90, 0x91, 0x92
REPLY = {LOOKUP: ADDRS, REVERSE: NAME, QUERY: ANSWER}
# ERROR's codes; lookups answer 0 (found) or NO_NAME..LOOKUP_FAILED.
REFUSED, NET_UNREACH, HOST_UNREACH, TIMED_OUT, RESET, DENIED, TOO_MANY, FAILED = range(1, 9)
NO_NAME, TRY_AGAIN, LOOKUP_FAILED = 1, 2, 3

CODES = {
    errno.ECONNREFUSED: REFUSED, errno.ENETUNREACH: NET_UNREACH, errno.EHOSTUNREACH: HOST_UNREACH,
    errno.ETIMEDOUT: TIMED_OUT, errno.ECONNRESET: RESET, errno.EPIPE: RESET,
    errno.ECONNABORTED: RESET, errno.EACCES: DENIED, errno.EPERM: DENIED,
}


def accept_key(key):
    return base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()


def upgrade(h):
    """The 101 answer; False (and a 400 sent) when it isn't a WebSocket request."""
    key = h.headers.get("Sec-WebSocket-Key")
    if h.headers.get("Upgrade", "").lower() != "websocket" or not key:
        return False
    h.send_response(101)
    h.send_header("Upgrade", "websocket")
    h.send_header("Connection", "Upgrade")
    h.send_header("Sec-WebSocket-Accept", accept_key(key))
    h.end_headers()
    h.wfile.flush()
    return True


def close_frame(code, reason):
    payload = struct.pack(">H", code) + reason.encode()[:123]
    return bytes([0x88, len(payload)]) + payload


def frame(payload, opcode=0x2):
    """A frame to the tab (unmasked, one per message)."""
    n = len(payload)
    if n < 126:
        head = bytes([0x80 | opcode, n])
    elif n < 65536:
        head = struct.pack(">BBH", 0x80 | opcode, 126, n)
    else:
        head = struct.pack(">BBQ", 0x80 | opcode, 127, n)
    return head + payload


class Closed(Exception):
    """The WebSocket ends, with this close code and reason."""
    def __init__(self, code=1000, reason=""):
        super().__init__(reason)
        self.code, self.reason = code, reason


class Reader:
    """RFC 6455 frames from the tab (masked) -> whole messages."""
    def __init__(self):
        self.buf = bytearray()
        self.parts = []
        self.opcode = None

    def feed(self, data):
        self.buf += data

    def messages(self):
        """(opcode, payload) per complete message, control frames included."""
        while True:
            b = self.buf
            if len(b) < 2:
                return
            fin, opcode, masked, n = b[0] & 0x80, b[0] & 0x0F, b[1] & 0x80, b[1] & 0x7F
            at = 2
            if n == 126:
                if len(b) < 4:
                    return
                n, at = struct.unpack(">H", b[2:4])[0], 4
            elif n == 127:
                if len(b) < 10:
                    return
                n, at = struct.unpack(">Q", b[2:10])[0], 10
            if not masked:
                raise Closed(1002, "frames from a client are masked")
            if n + sum(map(len, self.parts)) > MAX_MESSAGE:
                raise Closed(1009, "message too big")
            if len(b) < at + 4 + n:
                return
            mask = bytes(b[at:at + 4])
            payload = bytes(b[at + 4:at + 4 + n])
            del b[:at + 4 + n]
            if n:
                m = int.from_bytes((mask * ((n + 3) // 4))[:n], "big")
                payload = (int.from_bytes(payload, "big") ^ m).to_bytes(n, "big")
            if opcode >= 0x8:
                if not fin or n > 125:
                    raise Closed(1002, "a fragmented or long control frame")
                yield opcode, payload
                continue
            if opcode == 0:
                if self.opcode is None:
                    raise Closed(1002, "a continuation of nothing")
            elif self.opcode is not None:
                raise Closed(1002, "a message inside a fragmented one")
            else:
                self.opcode = opcode
            self.parts.append(payload)
            if fin:
                yield self.opcode, b"".join(self.parts)
                self.parts, self.opcode = [], None


def parse_ports(spec):
    """The allow-list (ints and "A-B" ranges) -> a test."""
    ranges = []
    for p in spec:
        a, _, b = str(p).partition("-")
        ranges.append((int(a), int(b or a)))
    return lambda port: any(a <= port <= b for a, b in ranges)


def nameserver():
    """The first IPv4 name server in /etc/resolv.conf (else systemd-resolved's)."""
    try:
        with open("/etc/resolv.conf") as f:
            for line in f:
                w = line.split()
                if len(w) >= 2 and w[0] == "nameserver" and ":" not in w[1]:
                    return w[1]
    except OSError:
        pass
    return "127.0.0.53"


def res_query(name, cls, qtype, timeout=4.0):
    """One question to the host's name server: its whole answer (wire format)."""
    qid = random.getrandbits(16)
    q = struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0)       # recursion desired
    for label in name.rstrip(".").split("."):
        raw = label.encode("ascii")
        if not 0 < len(raw) < 64:
            raise ValueError("a bad name")
        q += bytes([len(raw)]) + raw
    q += b"\0" + struct.pack(">HH", qtype, cls)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(timeout / 2)
        s.connect((nameserver(), 53))
        for _ in range(2):
            s.send(q)
            try:
                while True:
                    a = s.recv(4096)
                    if len(a) >= 12 and struct.unpack(">H", a[:2])[0] == qid:
                        return a
            except socket.timeout:
                continue
    raise socket.timeout()


class Relay:
    """The policy and the per-user counts, shared by every connection."""
    def __init__(self, cfg):
        self.cfg = cfg
        self.port_ok = parse_ports(cfg["ports"])
        self.lock = threading.Lock()
        self.connections = {}            # user id -> WebSockets open
        self.sockets = {}                # user id -> sockets open

    def allowed(self, addr, port):
        if not self.port_ok(port):
            return False
        a = ipaddress.IPv4Address(addr)
        if a.is_unspecified or a.is_multicast or int(a) == 0xFFFFFFFF:
            return False
        return self.cfg["allow_private"] or a.is_global

    def take(self, table, user, limit):
        with self.lock:
            if table.get(user, 0) >= limit:
                return False
            table[user] = table.get(user, 0) + 1
            return True

    def give(self, table, user):
        with self.lock:
            table[user] -= 1

    def handle(self, sock, user):
        """A signed-in user's WebSocket, after the handshake: until it closes."""
        if not self.cfg["enabled"]:
            sock.sendall(close_frame(1013, "no relay on this server"))
            return
        if not self.take(self.connections, user["id"], self.cfg["max_connections"]):
            sock.sendall(close_frame(1013, "too many relay connections"))
            return
        try:
            Connection(self, sock, user["id"]).run()
        finally:
            self.give(self.connections, user["id"])


class Sock:
    def __init__(self, id, s, kind):
        self.id, self.s, self.kind = id, s, kind      # kind: tcp, udp, dead
        self.connecting = kind == "tcp"
        self.out = bytearray()           # SEND bytes not yet written
        self.unacked = 0                 # DATA bytes sent the tab hasn't credited
        self.eof = False                 # the peer's FIN seen
        self.shut_out = False            # SHUTDOWN asked
        self.peers = set()               # UDP: the hosts sent to, the only ones heard


class Connection:
    def __init__(self, relay, ws, user):
        self.relay, self.ws, self.user = relay, ws, user
        self.cfg = relay.cfg
        self.sel = selectors.DefaultSelector()
        self.reader = Reader()
        self.socks = {}
        self.done = []                   # finished lookups' replies (from their threads)
        self.done_lock = threading.Lock()
        self.wake_r, self.wake_w = socket.socketpair()
        self.lookups = 0
        self.heard = time.monotonic()

    def send(self, op, id, body=b""):
        self.ws.sendall(frame(struct.pack(">BI", op, id) + body))

    def run(self):
        self.ws.settimeout(None)
        self.sel.register(self.ws, selectors.EVENT_READ, None)
        self.sel.register(self.wake_r, selectors.EVENT_READ, "wake")
        self.send(HELLO, 0, struct.pack(">HHI", VERSION, self.cfg["max_sockets"], WINDOW))
        code, reason = 1000, ""
        pinged = time.monotonic()
        try:
            while True:
                for key, events in self.sel.select(timeout=PING_EVERY):
                    if key.data is None:
                        self.from_tab()
                    elif key.data == "wake":
                        self.wake_r.recv(4096)
                        with self.done_lock:
                            done, self.done = self.done, []
                        for reply in done:
                            self.lookups -= 1
                            self.send(*reply)
                    else:
                        self.from_peer(key.data, events)
                now = time.monotonic()
                if now - self.heard > 3 * PING_EVERY:
                    raise Closed(1001, "nothing heard")
                if now - pinged >= PING_EVERY:
                    self.ws.sendall(frame(b"", 0x9))
                    pinged = now
        except Closed as e:
            code, reason = e.code, e.reason
        except OSError:
            code = None                  # the tab's side is gone
        finally:
            for s in list(self.socks.values()):
                self.close(s)
            self.sel.close()
            self.wake_r.close()
            self.wake_w.close()
            if code is not None:
                try:
                    self.ws.sendall(close_frame(code, reason))
                except OSError:
                    pass

    # ---- the tab's messages ----
    def from_tab(self):
        data = self.ws.recv(65536)
        if not data:
            raise Closed(1001, "gone")
        self.heard = time.monotonic()
        self.reader.feed(data)
        for opcode, payload in self.reader.messages():
            if opcode == 0x8:
                raise Closed(1000, "")
            if opcode == 0x9:
                self.ws.sendall(frame(payload, 0xA))
            elif opcode == 0x1:
                raise Closed(1003, "binary messages only")
            elif opcode == 0x2:
                if len(payload) < 5:
                    raise Closed(1008, "a short message")
                op, id = struct.unpack(">BI", payload[:5])
                self.op(op, id, payload[5:])

    def op(self, op, id, body):
        if op in REPLY:
            return self.lookup(op, id, body)
        if op in (CONNECT, UDP):
            return self.open(op, id, body)
        s = self.socks.get(id)
        if s is None:
            raise Closed(1008, f"no socket {id}")
        if op == CLOSE:
            return self.close(s, abort=bool(body[:1] and body[0] & 1))
        if s.kind == "dead":
            return                       # sent before our ERROR reached the tab
        if op == SEND:
            if s.kind != "tcp" or s.shut_out:
                raise Closed(1008, "SEND on that socket")
            if len(s.out) + len(body) > WINDOW:
                raise Closed(1008, "more than the window")
            s.out += body
            if not s.connecting:
                self.flush(s)
        elif op == SENDTO:
            if s.kind != "udp" or len(body) < 6:
                raise Closed(1008, "SENDTO on that socket")
            addr, port = struct.unpack(">IH", body[:6])
            if not self.relay.allowed(addr, port):
                return                   # dropped, as by a firewall
            peer = (str(ipaddress.IPv4Address(addr)), port)
            s.peers.add(peer)
            try:
                s.s.sendto(body[6:6 + MAX_DATAGRAM], peer)
            except OSError:
                pass                     # datagrams may be lost
        elif op == SHUTDOWN:
            s.shut_out = True
            if not s.out and not s.connecting:
                self.shut(s)
        elif op == CREDIT:
            if len(body) < 4:
                raise Closed(1008, "a short CREDIT")
            s.unacked = max(0, s.unacked - struct.unpack(">I", body[:4])[0])
        else:
            raise Closed(1008, f"op {op:#x}")
        self.update(s)

    def open(self, op, id, body):
        if id in self.socks:
            raise Closed(1008, f"socket {id} is open")
        if op == CONNECT and len(body) < 6:
            raise Closed(1008, "a short CONNECT")
        if not self.relay.take(self.relay.sockets, self.user, self.cfg["max_sockets"]):
            return self.send(ERROR, id, bytes([TOO_MANY]))
        kind = "tcp" if op == CONNECT else "udp"
        s = Sock(id, socket.socket(socket.AF_INET, socket.SOCK_STREAM if op == CONNECT else socket.SOCK_DGRAM), kind)
        s.s.setblocking(False)
        self.socks[id] = s
        if op == UDP:
            s.s.bind(("0.0.0.0", 0))
            return self.update(s)
        addr, port = struct.unpack(">IH", body[:6])
        if not self.relay.allowed(addr, port):
            return self.error(s, DENIED)
        s.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        err = s.s.connect_ex((str(ipaddress.IPv4Address(addr)), port))
        if err not in (0, errno.EINPROGRESS, errno.EAGAIN):
            return self.error(s, CODES.get(err, FAILED))
        self.update(s)

    # ---- sockets ----
    def error(self, s, code):
        """The socket is dead; the tab frees its id with CLOSE."""
        self.send(ERROR, s.id, bytes([code]))
        self.drop(s)

    def drop(self, s):
        if s.kind == "dead":
            return
        if s.s in self.sel.get_map():
            self.sel.unregister(s.s)
        s.s.close()
        s.kind = "dead"

    def close(self, s, abort=False):
        if abort and s.kind == "tcp":
            try:
                s.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            except OSError:
                pass
        self.drop(s)
        if self.socks.pop(s.id, None) is s:
            self.relay.give(self.relay.sockets, self.user)

    def update(self, s):
        """Select on what the socket waits for: connecting, writing, room to read."""
        if s.kind == "dead":
            return
        ev = 0
        if s.connecting or s.out:
            ev |= selectors.EVENT_WRITE
        if not s.connecting and not s.eof and (s.kind == "udp" or s.unacked < WINDOW):
            ev |= selectors.EVENT_READ
        registered = s.s in self.sel.get_map()
        if ev and registered:
            self.sel.modify(s.s, ev, s)
        elif ev:
            self.sel.register(s.s, ev, s)
        elif registered:
            self.sel.unregister(s.s)

    def shut(self, s):
        try:
            s.s.shutdown(socket.SHUT_WR)
        except OSError:
            pass

    def flush(self, s):
        try:
            n = s.s.send(s.out)
        except BlockingIOError:
            return
        except OSError as e:
            return self.error(s, CODES.get(e.errno, RESET))
        del s.out[:n]
        if n:
            self.send(ACK, s.id, struct.pack(">I", n))
        if not s.out and s.shut_out:
            self.shut(s)

    def from_peer(self, s, events):
        if s.connecting:
            err = s.s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
            if err:
                return self.error(s, CODES.get(err, FAILED))
            s.connecting = False
            self.send(CONNECTED, s.id)
            if s.out:
                self.flush(s)
            elif s.shut_out:
                self.shut(s)
        elif events & selectors.EVENT_WRITE and s.out:
            self.flush(s)
        if s.kind != "dead" and events & selectors.EVENT_READ:
            if s.kind == "udp":
                self.datagram(s)
            else:
                self.stream(s)
        self.update(s)

    def datagram(self, s):
        try:
            data, (host, port) = s.s.recvfrom(MAX_DATAGRAM)
        except OSError:
            return                       # an ICMP error for an earlier send
        if (host, port) in s.peers:
            self.send(DATAFROM, s.id, struct.pack(">IH", int(ipaddress.IPv4Address(host)), port) + data)

    def stream(self, s):
        try:
            data = s.s.recv(min(16384, WINDOW - s.unacked))
        except BlockingIOError:
            return
        except OSError as e:
            return self.error(s, CODES.get(e.errno, RESET))
        if data:
            s.unacked += len(data)
            self.send(DATA, s.id, data)
        else:
            s.eof = True
            self.send(EOF, s.id)

    # ---- DNS, on threads ----
    def lookup(self, op, id, body):
        if self.lookups >= MAX_LOOKUPS:
            return self.send(REPLY[op], id, bytes([TRY_AGAIN]))
        self.lookups += 1
        threading.Thread(target=self.resolve, args=(op, id, bytes(body)), daemon=True).start()

    def resolve(self, op, id, body):
        try:
            if op == LOOKUP:
                addrs = []
                for *_, (host, _) in socket.getaddrinfo(body.decode("ascii"), None, socket.AF_INET,
                                                        socket.SOCK_STREAM):
                    a = int(ipaddress.IPv4Address(host))
                    if a not in addrs:
                        addrs.append(a)
                answer = b"".join(struct.pack(">I", a) for a in addrs[:16])
            elif op == REVERSE:
                host = str(ipaddress.IPv4Address(struct.unpack(">I", body[:4])[0]))
                answer = socket.getnameinfo((host, 0), socket.NI_NAMEREQD)[0].encode("ascii", "replace")
            else:
                cls, qtype = struct.unpack(">HH", body[:4])
                answer = res_query(body[4:].decode("ascii"), cls, qtype)
            reply = bytes([0]) + answer
        except socket.gaierror as e:
            no_name = (socket.EAI_NONAME, getattr(socket, "EAI_NODATA", socket.EAI_NONAME))
            reply = bytes([NO_NAME if e.errno in no_name else TRY_AGAIN if e.errno == socket.EAI_AGAIN
                           else LOOKUP_FAILED])
        except socket.timeout:
            reply = bytes([TRY_AGAIN])
        except (OSError, ValueError, UnicodeError, struct.error):
            reply = bytes([LOOKUP_FAILED])
        with self.done_lock:
            self.done.append((REPLY[op], id, reply))
        try:
            self.wake_w.send(b"x")
        except OSError:
            pass                         # the connection ended meanwhile
