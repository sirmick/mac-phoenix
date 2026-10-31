// The Worker's side of the network relay (tools/web/server's /relay):
// the WebSocket that Executor's MacTCP and Open Transport reach the
// network through (host/wasm/net.cpp; the protocol: docs/executor/
// DIRECTION.md, "The network relay"). Signed in to the server only: the
// page says so at the start, and net.url is the relay's then.
//
// The C side opens it, sends through it and takes what came from inbox
// (one Uint8Array per message) as it yields; nothing here calls into C.
'use strict';

Module.net = {
    url: '',
    state: 0,           // 0 closed, 1 opening, 2 open, 3 failed
    inbox: [],
    ws: null,

    // On (from the 'start' message): the relay beside the page.
    enable(on) {
        const u = new URL('relay', location.href);
        u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
        this.url = on ? u.href : '';
    },

    open() {
        if (!this.url || this.state === 1 || this.state === 2)
            return;
        this.inbox = [];
        this.state = 1;
        let ws;
        try {
            ws = new WebSocket(this.url);
        } catch (e) {
            console.log(`[net] ${e}`);
            this.state = 3;
            return;
        }
        ws.binaryType = 'arraybuffer';
        ws.onopen = () => { this.state = 2; };
        ws.onmessage = (e) => {
            if (e.data instanceof ArrayBuffer)
                this.inbox.push(new Uint8Array(e.data));
        };
        ws.onerror = () => { this.state = 3; };
        ws.onclose = (e) => {
            if (this.ws === ws) {
                if (e.code !== 1000)
                    console.log(`[net] the relay closed: ${e.code} ${e.reason}`);
                this.state = 3;
                this.ws = null;
            }
        };
        this.ws = ws;
    },

    send(bytes) {
        if (this.ws && this.state === 2)
            this.ws.send(bytes);
    },
};
