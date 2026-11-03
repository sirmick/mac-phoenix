// What the tab prints, sent to the server too (tools/standalone: POST
// /api/log): console.log/info/warn/error, uncaught errors and rejected
// promises, in the page and in its Worker (Executor's stdout and stderr come
// through Module.print/printErr). A batch a second, at once after an error,
// and back to back while there is more: when the renderer crashes there is
// no last word, so the server has what was sent before. Served by anything
// else (no /api/log), it stops trying after the first refusal.
'use strict';

const Log = (() => {
    const BATCH = 60 << 10;     // under keepalive's body limit
    const BUFFER = 4 << 20;     // waiting to be sent; beyond it the oldest go
    const BUDGET = 48 << 20;    // a session's lines, then no more are sent
    const console_ = {};
    let session = '', where = '', lines = [], size = 0, sent = 0, dropped = 0;
    let sending = false, off = false, timer = 0;
    let onError = null;     // the Worker's: tell the page (it takes a screenshot)

    function text(a) {
        if (typeof a === 'string')
            return a;
        if (a instanceof Error)
            return a.stack || `${a.name}: ${a.message}`;
        try { return JSON.stringify(a); } catch { return String(a); }
    }

    function add(level, args) {
        if (off)
            return;
        const line = `${new Date().toISOString().slice(11, 23)} ${where} ${level === 'log' ? '' : level + ' '}` +
                     args.map(text).join(' ');
        lines.push(line);
        size += line.length + 1;
        while (size > BUFFER && lines.length > 1) {
            size -= lines.shift().length + 1;
            dropped++;
        }
        if (level === 'error') {
            flush();
            if (onError)
                try { onError(line.slice(0, 300)); } catch { }
        }
        else if (!timer)
            timer = setTimeout(flush, 1000);
    }

    function take() {
        const out = [];
        let n = 0;
        if (dropped) {
            out.push(`${where} [log] ${dropped} lines dropped (sent too slowly)`);
            dropped = 0;
        }
        while (lines.length && (n === 0 || n + lines[0].length < BATCH)) {
            const l = lines.shift().slice(0, BATCH - 100);
            n += l.length + 1;
            size -= l.length + 1;
            out.push(l);
        }
        return out.join('\n') + '\n';
    }

    async function flush() {
        clearTimeout(timer);
        timer = 0;
        if (!session || off || sending || !lines.length)
            return;
        sending = true;
        try {
            while (lines.length && !off) {
                const body = take();
                sent += body.length;
                const res = await fetch(`/api/log?session=${session}`, {
                    method: 'POST', body, keepalive: true, credentials: 'same-origin',
                    headers: { 'Content-Type': 'text/plain' } });
                if (!res.ok) {
                    off = true;
                    console_.log(`[log] the server keeps no logs (${res.status}): not sent`);
                } else if ((await res.json()).full || sent > BUDGET) {
                    off = true;
                    console_.log('[log] this session\'s log is full: no more sent');
                }
            }
        } catch (e) {
            console_.log(`[log] ${e}`);     // offline, the page going: try again with the next line
        } finally {
            sending = false;
        }
    }

    // The page going away: what is left, as a beacon (survives the unload).
    function last() {
        if (!session || off || !lines.length || !navigator.sendBeacon)
            return;
        while (lines.length)
            navigator.sendBeacon(`/api/log?session=${session}`, new Blob([take()], { type: 'text/plain' }));
    }

    // id: the page's new one, or the one it gave its Worker; name: 'page' | 'worker'.
    function start(id, name) {
        session = id;
        where = name;
        for (const level of ['log', 'info', 'warn', 'error']) {
            const orig = console_[level] = console[level].bind(console);
            console[level] = (...args) => { orig(...args); add(level, args); };
        }
        self.addEventListener('error', (e) =>
            add('error', [`uncaught ${e.message} at ${e.filename}:${e.lineno}:${e.colno}`,
                          ...(e.error && e.error.stack ? [e.error.stack] : [])]));
        self.addEventListener('unhandledrejection', (e) => add('error', ['unhandled rejection:', e.reason]));
        if (name === 'page')
            addEventListener('pagehide', last);
        add('info', [`[log] session ${id} ${location.href} ${navigator.userAgent}`]);
    }

    function newId() {
        const r = new Uint8Array(8);
        crypto.getRandomValues(r);      // randomUUID wants a secure context
        return Date.now().toString(36) + '-' + [...r].map((b) => b.toString(16).padStart(2, '0')).join('');
    }

    return { start, flush, newId, get session() { return session; },
             set onError(f) { onError = f; } };
})();
