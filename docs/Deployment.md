# Running and deploying

## Modes

| Command | What runs |
|---|---|
| `mac-phoenix [ROM]` | web server on `--port` (11000) plus the emulator as a child process (`--ipc`); the machine starts on Start in the UI or `POST /api/emulator/start` |
| `mac-phoenix --headless-http ...` | the HTTP API only, no video or sound encoding, the bridge on; the ROM machines run in-process (Executor still as a child), and the process exits with the guest. What the test scripts use |
| `mac-phoenix --no-webserver ...` | no HTTP at all; with `--timeout N` for timed runs |

The web server and the emulator are separate processes in the normal
mode: Stop kills the child, Start makes a new one, and a crash in the guest
doesn't take the server down (`/api/status` reports it). Frames and sound
cross through shared memory.

## Transports

One TCP listener serves the UI, the API and the `/ws` WebSocket. How the
screen reaches the browser depends on the codec (Settings, or the codec
menu):

| Codec | Transport | Works when |
|---|---|---|
| PNG, WebP | binary messages on `/ws` | any HTTP(S) path, proxies included |
| H.264, VP9 | WebRTC RTP over UDP, negotiated by ICE | the browser can reach the host on UDP: LAN, or a public address |
| HTTP stream | `/api/frame` polling | anywhere plain GET works |

Signaling and input always use `/ws`. Sound is Opus: an RTP track with
H.264/VP9, `/ws` messages with PNG/WebP. The client falls back from
H.264/VP9 to WebP/PNG to the HTTP stream when frames don't arrive.

WebRTC uses host candidates only (no STUN or TURN), so H.264/VP9 work on a
LAN or with a reachable host address, not through NAT or a proxy.

## Behind a reverse proxy

HTTP and the WebSocket share the port, so one `location` is enough:

```nginx
map $http_upgrade $conn_upgrade { default upgrade; '' close; }

server {
    listen 443 ssl http2;
    location / {
        proxy_pass http://127.0.0.1:11000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $conn_upgrade;
        proxy_set_header Host $host;
        proxy_buffering off;
        proxy_read_timeout 3600s;
    }
}
```

`proxy_buffering off` and the long read timeout keep `/ws` and the frame
polling responsive. Use PNG/WebP (or the HTTP stream) for users behind the
proxy; H.264/VP9 still need UDP to the host itself.

The API has no authentication: expose it only where everyone who can
reach it may control the emulator.

## Ports and files

* `--port N`: everything HTTP. Run several emulators on different ports.
* `--storage-dir`: ROMs, images, Executor Systems, snapshots (`~/storage`).
* `~/.config/mac-phoenix/config.json`: what the UI saves
  ([JsonConfig.md](JsonConfig.md)); `--config /dev/null` ignores it.
* With `--network socket`, the net-bridge socket (`/tmp/mac-ether.sock`;
  [Networking.md](Networking.md)).
