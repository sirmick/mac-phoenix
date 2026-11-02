// The tab's settings (docs/executor/Settings.md): settings.schema.json
// describes them, this keeps them and makes the Settings dialog from it.
//
// Where they are kept: the server's /api/settings when there is one
// (tools/standalone: settings.json, the page's and the server's own;
// tools/web/server, signed in: the account's, the page's only), else this
// browser's localStorage. The page's address overrides some for the visit
// (?size= ?scale= ?ppc ?system= ?logtraps= ?nesting= ?debug= ?ppcbreak=),
// never saved.
//
//   await Settings.ready
//   Settings.get('machine.screen')         what applies now
//   await Settings.set({ machine: { ppc: true } })   a merge patch (null:
//                                          the default); throws an Error
//                                          with .path for an invalid value
//   Settings.on('sound.volume', (v) => …)  live changes ('*': any)
//   Settings.open('input')                 the dialog, at a category
//   Settings.provide('systems', () => […]) choices the page knows
//   Settings.restart = async () => …       what "Restart Executor" does
//   Settings.started()                     Executor (re)started: what the
//                                          "restart to apply" notice compares
'use strict';

const Settings = (() => {
    const LOCAL_KEY = 'executor-settings';
    const params = new URLSearchParams(location.search);
    // ?param -> setting, and how to read it.
    const OVERRIDES = {
        size: ['machine.screen', String], scale: ['display.scale', Number], ppc: ['machine.ppc', () => true],
        system: ['machine.system', String], logtraps: ['advanced.logtraps', String],
        nesting: ['advanced.logtraps_nesting', Number], debug: ['advanced.debug', String],
        ppcbreak: ['advanced.ppc_break', String],
    };
    let schema = null, doc = null, where = 'local', scopes = ['page'], canChangeServer = false;
    let restartNeeded = [], serverFile = '', atStart = null;
    const listeners = new Map(), providers = {};

    // ---- the schema's checks (as schema.go and settings.py make them) ----
    class FieldError extends Error {
        constructor(path, msg) {
            super(path ? `${path}: ${msg}` : msg);
            this.path = path;
        }
    }
    const join = (path, key) => (path ? `${path}.${key}` : key);
    const jsonType = (v) => v === null ? 'null' : Array.isArray(v) ? 'array'
        : typeof v === 'number' ? (Number.isInteger(v) ? 'integer' : 'number')
        : typeof v === 'object' ? 'object' : typeof v;

    function validate(v, n, path = '') {
        const want = n.type === undefined ? [] : [].concat(n.type), got = jsonType(v);
        if (want.length && !want.some((w) => w === got || (w === 'number' && got === 'integer')))
            throw new FieldError(path, `${want.join(' or ')}, not ${got}`);
        if (n.enum && !n.enum.some((e) => e === v))
            throw new FieldError(path, `one of ${n.enum.map((e) => JSON.stringify(e)).join(', ')}`);
        if (got === 'integer' || got === 'number') {
            if (n.minimum !== undefined && v < n.minimum)
                throw new FieldError(path, `at least ${n.minimum}`);
            if (n.maximum !== undefined && v > n.maximum)
                throw new FieldError(path, `at most ${n.maximum}`);
        } else if (got === 'string') {
            const len = [...v].length;
            if (n.minLength !== undefined && len < n.minLength)
                throw new FieldError(path, `at least ${n.minLength} characters`);
            if (n.maxLength !== undefined && len > n.maxLength)
                throw new FieldError(path, `at most ${n.maxLength} characters`);
            if (n.pattern && !new RegExp(n.pattern).test(v))
                throw new FieldError(path, `${JSON.stringify(v)} does not match ${n.pattern}`);
        } else if (got === 'array') {
            if (n.maxItems !== undefined && v.length > n.maxItems)
                throw new FieldError(path, `at most ${n.maxItems} items`);
            if (n.items)
                v.forEach((e, i) => validate(e, n.items, `${path}[${i}]`));
        } else if (got === 'object') {
            for (const k of Object.keys(v).sort()) {
                const p = (n.properties || {})[k];
                if (!p) {
                    if (n.additionalProperties === false)
                        throw new FieldError(join(path, k), 'no such setting');
                    continue;
                }
                validate(v[k], p, join(path, k));
            }
        }
    }

    function check(d) {
        validate(d, schema);
        for (const p of (d.network && d.network.relay_ports) || []) {
            const [a, b] = String(p).split('-');
            const lo = Number(a), hi = b === undefined ? lo : Number(b);
            if (lo < 1 || hi > 65535 || lo > hi)
                throw new FieldError('network.relay_ports', `${p}: ports are 1-65535, A-B with A <= B`);
        }
    }

    const clone = (v) => (v === undefined ? v : JSON.parse(JSON.stringify(v)));
    function defaults(n = schema) {
        if (!n.properties)
            return clone(n.default);
        const out = {};
        for (const [k, p] of Object.entries(n.properties))
            if (!p.readOnly)
                out[k] = defaults(p);
        return out;
    }
    function fill(d, n = schema) {
        for (const [k, p] of Object.entries(n.properties || {})) {
            if (p.readOnly)
                continue;
            if (!(k in d))
                d[k] = defaults(p);
            else if (p.properties && d[k] && typeof d[k] === 'object' && !Array.isArray(d[k]))
                fill(d[k], p);
        }
        return d;
    }
    function mergePatch(d, patch) {         // RFC 7396, in place
        for (const [k, v] of Object.entries(patch)) {
            if (v === null)
                delete d[k];
            else if (jsonType(v) === 'object')
                d[k] = mergePatch(jsonType(d[k]) === 'object' ? d[k] : {}, v);
            else
                d[k] = clone(v);
        }
        return d;
    }
    function lookup(path) {
        let n = schema;
        for (const k of path.split('.')) {
            n = n && n.properties && n.properties[k];
            if (!n)
                return null;
        }
        return n;
    }
    function leaves(patch, path = '', n = schema) {
        const out = [];
        for (const [k, v] of Object.entries(patch)) {
            const p = join(path, k), child = n && n.properties && n.properties[k];
            if (child && child.properties && jsonType(v) === 'object')
                out.push(...leaves(v, p, child));
            else if (child && child.properties && v === null)
                out.push(...Object.entries(child.properties).filter(([, c]) => !c.readOnly).map(([ck]) => join(p, ck)));
            else
                out.push(p);
        }
        return out;
    }
    // patch applied to d (not changed), checked: what the server will do.
    function patched(d, patch, within = scopes) {
        patch = clone(patch);
        delete patch.version;
        for (const p of leaves(patch).sort()) {
            const n = lookup(p);
            if (!n)
                throw new FieldError(p, 'no such setting');
            if (n.readOnly)
                throw new FieldError(p, 'read-only');
            if (!n.properties && !within.includes(n['x-scope']))
                throw new FieldError(p, "set by the site's configuration");
        }
        const out = fill(mergePatch(clone(d), patch));
        check(out);
        return out;
    }

    const at = (d, path) => path.split('.').reduce((v, k) => (v == null ? undefined : v[k]), d);
    function override(path) {
        for (const [param, [p, read]] of Object.entries(OVERRIDES))
            if (p === path && params.has(param))
                return { param, value: read(params.get(param)) };
        return null;
    }

    // ---- loading and saving ----
    function fromServer(v) {
        doc = fill(v.settings);
        scopes = v.scopes || ['page'];
        canChangeServer = !!v.can_change_server;
        restartNeeded = v.restart_needed || [];
        serverFile = v.file || '';
    }

    function readLocal() {
        let saved = {};
        try {
            saved = JSON.parse(localStorage.getItem(LOCAL_KEY) || '{}') || {};
        } catch { }
        // Kept what is still valid, setting by setting.
        let d = defaults();
        for (const [cat, values] of Object.entries(saved))
            if (jsonType(values) === 'object' && jsonType(d[cat]) === 'object')
                for (const [k, v] of Object.entries(values))
                    try {
                        d = patched(d, { [cat]: { [k]: v } });
                    } catch { }
        d.version = schema['x-version'];
        return d;
    }

    // The page's settings before there were any (main.js's localStorage keys).
    async function migrateOld() {
        const patch = {};
        const sys = localStorage.getItem('executor-system'), ppc = localStorage.getItem('executor-ppc');
        const lib = localStorage.getItem('executor-library');
        if (sys !== null)
            (patch.machine = patch.machine || {}).system = sys;
        if (ppc !== null)
            (patch.machine = patch.machine || {}).ppc = ppc === '1';
        if (lib !== null)
            try {
                patch.disks = { mounted: JSON.parse(lib).filter((u) => typeof u === 'string' && u) };
            } catch { }
        if (!Object.keys(patch).length)
            return;
        try {
            await set(patch);
        } catch (e) {
            console.log(`[web] settings: the old ones not kept (${e.message})`);
        }
        for (const k of ['executor-system', 'executor-ppc', 'executor-library'])
            localStorage.removeItem(k);
    }

    const ready = (async () => {
        try {
            schema = await (await fetch('settings.schema.json', { cache: 'no-cache' })).json();
        } catch (e) {
            console.error(`[web] settings.schema.json: ${e.message}`);
            schema = { type: 'object', properties: {}, 'x-version': 2 };
        }
        try {
            const res = await fetch('api/settings', { cache: 'no-store' });
            if (res.ok && (res.headers.get('Content-Type') || '').includes('json')) {
                fromServer(await res.json());
                where = 'server';
            }
        } catch { }
        if (where !== 'server') {
            doc = readLocal();
            scopes = ['page'];
        }
        await migrateOld();
        apply('*');
    })();

    async function set(patch) {
        const before = doc;
        const next = patched(doc, patch);
        if (where === 'server') {
            const res = await fetch('api/settings', {
                method: 'POST', cache: 'no-store', body: JSON.stringify(patch),
                headers: { 'Content-Type': 'application/json' } });
            const v = await res.json().catch(() => ({}));
            if (!res.ok)
                throw new FieldError(v.path || '', v.error || `the server: ${res.status}`);
            fromServer(v);
        } else {
            doc = next;
            const save = {};        // the page's only
            for (const [cat, n] of Object.entries(schema.properties))
                if (n.properties)
                    save[cat] = doc[cat];
            localStorage.setItem(LOCAL_KEY, JSON.stringify({ version: schema['x-version'], ...save }));
        }
        const changed = [];
        for (const [cat, n] of Object.entries(schema.properties))
            for (const k of Object.keys(n.properties || {}))
                if (JSON.stringify(at(before, `${cat}.${k}`)) !== JSON.stringify(at(doc, `${cat}.${k}`)))
                    changed.push(`${cat}.${k}`);
        for (const p of changed)
            apply(p);
        notice();
        return changed;
    }

    function get(path) {
        const o = override(path);
        return o ? o.value : at(doc, path);
    }

    function on(path, fn) {
        if (!listeners.has(path))
            listeners.set(path, []);
        listeners.get(path).push(fn);
    }

    // ---- what applies live: here, or by whoever listens ----
    function apply(path) {
        const fire = (p) => {
            for (const fn of listeners.get(p) || [])
                fn(get(p), p);
        };
        if (path === '*') {
            for (const p of listeners.keys())
                if (p !== '*')
                    fire(p);
        } else
            fire(path);
        for (const fn of listeners.get('*') || [])
            fn(path === '*' ? null : get(path), path);
    }

    on('display.smoothing', (v) => document.body.classList.toggle('executor-smooth', !!v));
    on('advanced.screenshot_button', (v) => {
        const b = document.getElementById('shot');
        if (b)
            b.hidden = !v;
    });
    on('display.fullscreen_button', (v) => {
        fsButton.hidden = !v || !document.fullscreenEnabled;
    });
    on('input.command_key', (v) => {
        if (typeof setCommandKey === 'function')
            setCommandKey(v);           // keys.js
    });
    on('input.keyboard_capture', () => lockKeys());

    // ---- fullscreen, with the keyboard captured (Keyboard Lock) ----
    const fsButton = document.createElement('button');
    fsButton.id = 'fullscreen';
    fsButton.textContent = 'Fullscreen';
    fsButton.title = 'The Mac\'s screen on all of yours (Esc to leave; held, with the keyboard captured)';
    fsButton.hidden = true;
    fsButton.addEventListener('click', () => fullscreen());

    async function fullscreen() {
        const box = document.getElementById('screen-box');
        if (!box || document.fullscreenElement)
            return;
        await box.requestFullscreen({ navigationUI: 'hide' }).catch((e) => console.log(`[web] fullscreen: ${e.message}`));
        const c = box.querySelector('canvas');
        if (c)
            c.focus();
    }

    async function lockKeys() {
        const kb = navigator.keyboard;
        if (!kb || !kb.lock)
            return;
        if (document.fullscreenElement && get('input.keyboard_capture'))
            await kb.lock().catch((e) => console.log(`[web] keyboard lock: ${e.message}`));
        else
            kb.unlock();
    }
    document.addEventListener('fullscreenchange', () => {
        lockKeys();
        dispatchEvent(new Event('resize'));     // main.js sizes the canvas
    });

    // ---- the "restart to apply" notice ----
    const pending = () => {
        if (!atStart)
            return [];
        return Object.keys(atStart).filter((p) => JSON.stringify(get(p)) !== JSON.stringify(atStart[p]));
    };
    function started() {
        atStart = {};
        for (const [cat, n] of Object.entries(schema.properties))
            for (const [k, p] of Object.entries(n.properties || {}))
                if (p['x-apply'] === 'executor')
                    atStart[`${cat}.${k}`] = clone(get(`${cat}.${k}`));
        notice();
    }

    const barNotice = document.createElement('span');
    barNotice.id = 'settings-pending';
    barNotice.hidden = true;
    const titleOf = (p) => {
        const [cat] = p.split('.');
        return `${schema.properties[cat].title} › ${lookup(p).title}`;
    };

    function notice() {
        const exec = pending(), server = where === 'server' ? restartNeeded : [];
        barNotice.hidden = !exec.length;
        barNotice.replaceChildren('Restart Executor to apply your settings ',
            button('Restart', 'restart-now', () => restart()));
        barNotice.title = exec.map(titleOf).join('\n');
        if (!dialog)
            return;
        const box = dialog.querySelector('.settings-notices');
        const lines = [];
        if (exec.length) {
            const d = document.createElement('div');
            d.className = 'settings-notice';
            d.append(`Restart Executor to apply: ${exec.map(titleOf).join(', ')}. `,
                     button('Restart Executor', 'restart-now', () => restart()));
            lines.push(d);
        }
        if (server.length) {
            const d = document.createElement('div');
            d.className = 'settings-notice server';
            d.textContent = `Applies when the server starts again: ${server.filter(lookup).map(titleOf).join(', ')}.`;
            lines.push(d);
        }
        box.replaceChildren(...lines);
        for (const row of dialog.querySelectorAll('.settings-row'))
            row.classList.toggle('pending', exec.includes(row.dataset.path) || server.includes(row.dataset.path));
    }

    async function restart() {
        if (api.restart)
            await api.restart();
        else
            location.reload();
    }

    // ---- the dialog ----
    let dialog = null, current = null;

    function button(text, cls, onclick) {
        const b = document.createElement('button');
        b.type = 'button';
        b.textContent = text;
        if (cls)
            b.className = cls;
        b.addEventListener('click', onclick);
        return b;
    }

    const visible = (p) => !p['x-scope'] || scopes.includes(p['x-scope']);
    const categories = () => Object.entries(schema.properties)
        .filter(([, n]) => n.properties && Object.values(n.properties).some(visible));

    function build() {
        dialog = document.createElement('dialog');
        dialog.id = 'settings';
        dialog.className = 'settings';
        const head = document.createElement('header');
        const h = document.createElement('h1');
        h.textContent = 'Settings';
        const kept = document.createElement('span');
        kept.className = 'settings-where';
        head.append(h, kept, button('✕', 'settings-close', () => dialog.close()));
        const body = document.createElement('div');
        body.className = 'settings-body';
        const nav = document.createElement('nav');
        nav.setAttribute('role', 'tablist');
        nav.setAttribute('aria-orientation', 'vertical');
        const panel = document.createElement('section');
        panel.setAttribute('role', 'tabpanel');
        body.append(nav, panel);
        const notices = document.createElement('div');
        notices.className = 'settings-notices';
        dialog.append(head, notices, body);
        document.body.append(dialog);
        nav.addEventListener('keydown', (e) => {
            const tabs = [...nav.querySelectorAll('button')];
            const i = tabs.indexOf(document.activeElement);
            if (i < 0 || (e.key !== 'ArrowDown' && e.key !== 'ArrowUp'))
                return;
            const t = tabs[(i + (e.key === 'ArrowDown' ? 1 : tabs.length - 1)) % tabs.length];
            t.focus();
            t.click();
            e.preventDefault();
        });
        dialog.addEventListener('keydown', (e) => e.stopPropagation());   // not the Mac's
    }

    function render() {
        const cats = categories();
        if (!cats.some(([c]) => c === current))
            current = cats.length ? cats[0][0] : null;
        dialog.querySelector('.settings-where').textContent = where === 'server'
            ? (serverFile ? `kept in ${serverFile}` : 'kept with your account')
            : 'kept in this browser';
        const nav = dialog.querySelector('nav');
        nav.replaceChildren(...cats.map(([c, n]) => {
            const b = button(n.title, c === current ? 'active' : '', () => {
                current = c;
                render();
            });
            b.dataset.category = c;
            b.setAttribute('role', 'tab');
            b.setAttribute('aria-selected', c === current);
            b.tabIndex = c === current ? 0 : -1;
            return b;
        }));
        const panel = dialog.querySelector('section');
        if (!current) {
            panel.replaceChildren();
            return;
        }
        const n = schema.properties[current];
        const h = document.createElement('h2');
        h.textContent = n.title;
        const desc = document.createElement('p');
        desc.className = 'settings-desc';
        desc.textContent = n.description || '';
        const rows = [];
        for (const [k, p] of Object.entries(n.properties)) {
            if (!visible(p))
                continue;
            if (p['x-group']) {
                const g = document.createElement('h3');
                g.textContent = p['x-group'];
                rows.push(g);
            }
            rows.push(row(`${current}.${k}`, p));
        }
        const foot = document.createElement('footer');
        foot.append(button(`Reset ${n.title} to defaults`, 'settings-reset', async () => {
            const reset = {};
            for (const [k, p] of Object.entries(n.properties))
                if (visible(p) && !p.readOnly && (p['x-scope'] !== 'server' || canChangeServer))
                    reset[k] = null;
            await set({ [current]: reset }).catch((e) => alert(e.message));
            render();
        }));
        if (current === 'advanced')
            foot.append(button('Reset all settings', 'settings-reset-all', async () => {
                if (!confirm('Put every setting back to its default?'))
                    return;
                const reset = {};
                for (const [c, cn] of categories()) {
                    reset[c] = {};
                    for (const [k, p] of Object.entries(cn.properties))
                        if (visible(p) && !p.readOnly && (p['x-scope'] !== 'server' || canChangeServer))
                            reset[c][k] = null;
                }
                await set(reset).catch((e) => alert(e.message));
                render();
            }));
        panel.replaceChildren(h, desc, ...rows, foot);
        notice();
    }

    // One setting: its label, control, help and error.
    function row(path, p) {
        const r = document.createElement('div');
        r.className = 'settings-row';
        r.dataset.path = path;
        const id = `setting-${path.replace(/\./g, '-')}`;
        const label = document.createElement('label');
        label.htmlFor = id;
        label.textContent = p.title || path;
        const right = document.createElement('div');
        right.className = 'settings-control';
        const help = document.createElement('div');
        help.className = 'settings-help';
        help.textContent = p.description || '';
        const error = document.createElement('div');
        error.className = 'settings-error';
        error.setAttribute('role', 'alert');
        const o = override(path);
        const locked = p.readOnly || (p['x-scope'] === 'server' && !canChangeServer);
        const commit = async (value, control) => {
            error.textContent = '';
            r.classList.remove('invalid');
            try {
                const [cat, key] = path.split('.');
                await set({ [cat]: { [key]: value } });
                if (control && control.refresh)
                    control.refresh();
            } catch (e) {
                error.textContent = e.message.replace(`${path}: `, '');
                r.classList.add('invalid');
            }
        };
        const c = control(path, p, id, commit, locked);
        right.append(c);
        if (p['x-apply'] === 'executor' || p['x-apply'] === 'server') {
            const tag = document.createElement('span');
            tag.className = 'settings-apply';
            tag.textContent = p['x-apply'] === 'executor' ? 'restarts Executor' : 'at the server\'s next start';
            label.append(tag);
        }
        if (o) {
            const note = document.createElement('div');
            note.className = 'settings-override';
            note.textContent = `This visit: ?${o.param} in the page's address (${JSON.stringify(o.value)}).`;
            right.append(note);
        }
        if (p['x-scope'] === 'server' && !canChangeServer && !p.readOnly) {
            const note = document.createElement('div');
            note.className = 'settings-override';
            note.textContent = "Set by the server's owner.";
            right.append(note);
        }
        right.append(help, error);
        r.append(label, right);
        return r;
    }

    function control(path, p, id, commit, locked) {
        const value = at(doc, path);
        const widget = p['x-widget'];
        const types = [].concat(p.type || []);
        let el;
        if (widget === 'disk-list')
            return diskList(id, value, commit, locked);
        if (p['x-choices']) {
            el = document.createElement('select');
            const names = providers[p['x-choices']] ? providers[p['x-choices']]() : [];
            const opts = [new Option("Executor's own", '')];
            for (const v of new Set([...names, value].filter(Boolean)))
                opts.push(new Option(names.includes(v) ? v : `${v} (not here)`, v));
            el.replaceChildren(...opts);
            el.value = value;
            el.addEventListener('change', () => commit(el.value));
        } else if (p.enum) {
            el = document.createElement('select');
            p.enum.forEach((v, i) => {
                const opt = new Option((p['x-labels'] || [])[i] || String(v), JSON.stringify(v));
                el.append(opt);
            });
            el.value = JSON.stringify(value);
            el.addEventListener('change', () => commit(JSON.parse(el.value)));
        } else if (types[0] === 'boolean') {
            el = document.createElement('input');
            el.type = 'checkbox';
            el.checked = !!value;
            el.addEventListener('change', () => commit(el.checked));
        } else if (types[0] === 'integer' || types[0] === 'number') {
            el = document.createElement('input');
            el.type = widget === 'range' ? 'range' : 'number';
            if (p.minimum !== undefined)
                el.min = p.minimum;
            if (p.maximum !== undefined)
                el.max = p.maximum;
            el.step = types[0] === 'integer' ? 1 : 'any';
            el.value = value;
            const read = () => (el.value === '' ? NaN : Number(el.value));
            if (widget === 'range') {
                const wrap = document.createElement('span');
                wrap.className = 'settings-range';
                const out = document.createElement('output');
                out.textContent = value;
                el.addEventListener('input', () => { out.textContent = el.value; });
                el.id = id;
                el.disabled = locked;
                el.addEventListener('change', () => commit(read()));
                wrap.append(el, out);
                return wrap;
            }
            el.addEventListener('change', () => commit(read()));
        } else if (types[0] === 'array') {
            el = document.createElement('input');
            el.type = 'text';
            el.value = (value || []).join(' ');
            el.placeholder = 'separated by spaces';
            const numeric = [].concat((p.items || {}).type || []).includes('integer');
            el.addEventListener('change', () => commit(el.value.split(/[\s,]+/).filter(Boolean)
                .map((w) => (numeric && /^\d+$/.test(w) ? Number(w) : w))));
        } else {
            el = document.createElement('input');
            el.type = 'text';
            el.value = value === undefined ? '' : value;
            if (p.readOnly && path === 'advanced.data' && doc.advanced)
                el.value = doc.advanced.data || '';
            if (widget === 'path')
                el.classList.add('mono');
            if (p.examples) {
                const list = document.createElement('datalist');
                list.id = `${id}-examples`;
                list.append(...p.examples.map((x) => new Option(x, x)));
                el.setAttribute('list', list.id);
                el.after(list);
                const wrap = document.createElement('span');
                wrap.append(el, list);
                el.id = id;
                el.disabled = locked;
                el.addEventListener('change', () => commit(el.value));
                return wrap;
            }
            el.addEventListener('change', () => commit(el.value));
        }
        el.id = id;
        el.disabled = locked;
        return el;
    }

    // The library's disks mounted, in order: up, down, remove, add.
    function diskList(id, value, commit, locked) {
        const wrap = document.createElement('div');
        wrap.className = 'settings-disks';
        wrap.id = id;
        const library = providers.library ? providers.library() : [];
        const titleOf = (url) => {
            const d = library.find((x) => new URL(x.url, location.href).href === new URL(url, location.href).href);
            return d ? d.title : decodeURIComponent(url.split('/').pop());
        };
        const list = document.createElement('ol');
        (value || []).forEach((url, i) => {
            const li = document.createElement('li');
            const name = document.createElement('span');
            name.textContent = titleOf(url);
            name.title = url;
            const move = (to) => {
                const v = [...value];
                v.splice(to, 0, v.splice(i, 1)[0]);
                commit(v).then(render);
            };
            const up = button('↑', '', () => move(i - 1));
            const down = button('↓', '', () => move(i + 1));
            const rm = button('✕', '', () => commit(value.filter((_, j) => j !== i)).then(render));
            up.title = 'Earlier';
            down.title = 'Later';
            rm.title = 'Remove (its changes are kept)';
            up.disabled = locked || i === 0;
            down.disabled = locked || i === value.length - 1;
            rm.disabled = locked;
            li.append(name, up, down, rm);
            list.append(li);
        });
        if (!(value || []).length) {
            const li = document.createElement('li');
            li.className = 'empty';
            li.textContent = library.length ? 'None yet.' : 'This site offers no library.';
            list.append(li);
        }
        const have = new Set((value || []).map((u) => new URL(u, location.href).href));
        const offer = library.filter((d) => !have.has(new URL(d.url, location.href).href));
        wrap.append(list);
        if (offer.length) {
            const add = document.createElement('select');
            add.append(new Option('Add a disk…', ''), ...offer.map((d) => new Option(d.title, d.url)));
            add.disabled = locked;
            add.addEventListener('change', () => add.value && commit([...(value || []), add.value]).then(render));
            wrap.append(add);
        }
        return wrap;
    }

    async function open(category) {
        await ready;
        if (!dialog)
            build();
        if (category)
            current = category;
        render();
        if (!dialog.open)
            dialog.showModal();
        const tab = dialog.querySelector('nav button.active');
        if (tab)
            tab.focus();
    }

    // The bar: a Settings button, Fullscreen, the notice.
    function addToBar() {
        const bar = document.getElementById('bar');
        if (!bar)
            return;
        const b = button('Settings', '', () => open());
        b.id = 'settings-button';
        b.title = 'Settings: the machine, disks, keys, display, sound, network';
        const restartButton = document.getElementById('restart');
        bar.insertBefore(b, restartButton ? restartButton.nextSibling : null);
        bar.insertBefore(fsButton, b.nextSibling);
        bar.append(barNotice);
    }
    if (document.readyState === 'loading')
        document.addEventListener('DOMContentLoaded', addToBar);
    else
        addToBar();

    const api = {
        ready, get, set, on, open, started, restart: null, FieldError,
        provide(name, fn) { providers[name] = fn; },
        get where() { return where; },
        get schema() { return schema; },
        get doc() { return clone(doc); },
        defaults: () => defaults(),
        check: (d) => check(d),
        patched: (d, p, within = ['page', 'server']) => patched(d, p, within),
        fullscreen,
    };
    return api;
})();
