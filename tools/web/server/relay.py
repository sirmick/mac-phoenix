"""The relay's endpoint: /relay, a WebSocket for signed-in users.

The stub: the handshake (RFC 6455), then a close frame, 1013 "try again
later". The relay (Executor's MacTCP/OT over one WebSocket, DIRECTION.md)
replaces handle(): it gets the connection's socket after the handshake
and the user.
"""
import base64
import hashlib
import struct

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


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


def handle(sock, user):
    sock.sendall(close_frame(1013, "no relay on this server yet"))
