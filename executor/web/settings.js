// The tab's settings (docs/executor/Settings.md): settings.schema.json
// describes them, this keeps them and makes the Settings dialog from it.
//
// Where they are kept: the server's /api/settings (executor/server:
// settings.json, the page's and the server's own); without it (refused:
// the server restarted, main.js) the defaults, kept nowhere. The page's
// address overrides some for the visit
// (?size= ?scale= ?ppc ?system= ?logtraps= ?nesting= ?debug= ?ppcbreak=),
// never saved.
//
//   await Settings.ready
//   Settings.get('machine.screen')         what applies now
//   await Settings.set({ machine: { ppc: true } })   a merge patch (null:
//                                          the default); throws an Error
//                                          with .path for an invalid value
//   Settings.on('sound.volume', (v) => …)  live changes ('*': any)
//   Settings.open('input')                 the dialog, at a category: its
//                                          changes are a draft until Save
//                                          (Cancel, Escape: forgotten)
//   Settings.provide('systems', (draft) => […]) choices the page knows
//                                          (strings, or { value, label })
//   Settings.choicesChanged()              they changed: the open dialog's
//                                          lists made again
//   Settings.restart = async () => …       what "Restart Executor" does
//   Settings.started()                     Executor (re)started: what the
//                                          "restart to apply" notice compares
'use strict';

const Settings = (() => {
    const params = new URLSearchParams(location.search);
    // ?param -> setting, and how to read it.
    const OVERRIDES = {
        size: ['machine.screen', String], scale: ['display.scale', Number], ppc: ['machine.ppc', () => true],
        depth: ['machine.depth', Number],
        system: ['machine.system', String], logtraps: ['advanced.logtraps', String],
        nesting: ['advanced.logtraps_nesting', Number], debug: ['advanced.debug', String],
        ppcbreak: ['advanced.ppc_break', String],
    };
    let schema = null, doc = null, where = 'none', scopes = ['page'], canChangeServer = false;
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
            doc = defaults();
            doc.version = schema['x-version'];
            scopes = ['page'];
        }
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
            doc = next;             // for this visit
        }
        const changed = [];
        for (const [cat, n] of Object.entries(schema.properties))
            for (const k of Object.keys(n.properties || {}))
                if (JSON.stringify(at(before, `${cat}.${k}`)) !== JSON.stringify(at(doc, `${cat}.${k}`)))
                    changed.push(`${cat}.${k}`);
        // The open dialog follows: a setting the draft hadn't changed
        // takes the new value (the Disks panel's System choice, say).
        if (draft)
            for (const p of changed) {
                const [cat, k] = p.split('.');
                if (JSON.stringify(at(draft, p)) === JSON.stringify(at(before, p)))
                    draft = patched(draft, { [cat]: { [k]: at(doc, p) } });
            }
        for (const p of changed)
            apply(p);
        notice();
        choicesChanged();
        return changed;
    }

    // The page's choices changed (disks in or out, the System's list):
    // the open dialog's lists made again.
    function choicesChanged() {
        if (dialog && dialog.open && draft)
            render();
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

    // ---- the dialog: a draft of the document, saved whole by Save ----
    let dialog = null, current = null, draft = null;

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
        head.append(h, kept);
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
        const actions = document.createElement('footer');
        actions.className = 'settings-actions';
        const note = document.createElement('span');
        note.className = 'settings-unsaved';
        note.setAttribute('aria-live', 'polite');
        const save = button('Save', 'settings-save', () => saveDraft());
        actions.append(note, button('Cancel', 'settings-cancel', () => dialog.close()), save);
        dialog.append(head, notices, body, actions);
        document.body.append(dialog);
        // Closed by Cancel, Escape or Save: the draft is gone.
        dialog.addEventListener('close', () => { draft = null; });
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
            ? (serverFile ? `kept in ${serverFile}` : 'kept on the server')
            : 'not kept: no server';
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
        autoRefresh = [];
        const h = document.createElement('h2');
        h.textContent = n.title;
        const desc = document.createElement('p');
        desc.className = 'settings-desc';
        desc.textContent = n.description || '';
        const rows = [];
        for (const [k, p] of Object.entries(n.properties)) {
            if (!visible(p) || p['x-shown-in'])
                continue;
            if (p['x-group']) {
                const g = document.createElement('h3');
                g.textContent = p['x-group'];
                rows.push(g);
            }
            rows.push(row(`${current}.${k}`, p));
        }
        // Defaults into the draft (saved by Save, like any change).
        const toDefaults = (c) => {
            for (const [k, p] of Object.entries(schema.properties[c].properties))
                if (visible(p) && !p.readOnly && (p['x-scope'] !== 'server' || canChangeServer))
                    draft[c][k] = defaults(p);
        };
        const foot = document.createElement('div');
        foot.className = 'settings-resets';
        foot.append(button(`Reset ${n.title} to defaults`, 'settings-reset', () => {
            toDefaults(current);
            render();
        }));
        if (current === 'advanced')
            foot.append(button('Reset all settings', 'settings-reset-all', () => {
                if (!confirm('Put every setting back to its default? (Save to keep it.)'))
                    return;
                for (const [c] of categories())
                    toDefaults(c);
                render();
            }));
        panel.replaceChildren(h, desc, ...rows, foot);
        notice();
        unsaved();
    }

    // What the draft changes, setting by setting ("machine.ram_mb").
    function changedPaths() {
        const out = [];
        for (const [cat, n] of Object.entries(schema.properties))
            for (const k of Object.keys(n.properties || {}))
                if (JSON.stringify(at(draft, `${cat}.${k}`)) !== JSON.stringify(at(doc, `${cat}.${k}`)))
                    out.push(`${cat}.${k}`);
        return out;
    }

    // The footer: Save while there is something to save, and what it
    // will take to apply.
    function unsaved() {
        if (!dialog || !draft)
            return;
        const changed = changedPaths();
        const bad = [...dialog.querySelectorAll('.settings-row.invalid')];
        dialog.querySelector('.settings-save').disabled = !changed.length;
        for (const row of dialog.querySelectorAll('.settings-row'))
            row.classList.toggle('changed', changed.includes(row.dataset.path)
                                 || changed.includes((lookup(row.dataset.path) || {})['x-auto-setting']));
        const by = (how) => changed.filter((p) => lookup(p)['x-apply'] === how).map((p) => lookup(p).title);
        const lines = [];
        if (bad.length)
            lines.push('A value is not right: see the setting marked in red.');
        else if (!changed.length)
            lines.push('No changes.');
        else {
            lines.push(`${changed.length} change${changed.length === 1 ? '' : 's'} not saved.`);
            if (by('executor').length)
                lines.push(`Executor restarts to apply ${by('executor').join(', ')}.`);
            if (by('server').length)
                lines.push(`The server applies ${by('server').join(', ')} when it starts again.`);
        }
        const note = dialog.querySelector('.settings-unsaved');
        note.textContent = lines.join(' ');
        note.classList.toggle('error', !!bad.length);
    }

    // A setting's error, under it (its category shown).
    function showError(path, msg) {
        const rowPath = path.replace(/\[.*$/, '').split('.').slice(0, 2).join('.');
        if (!lookup(rowPath)) {
            dialog.querySelector('.settings-unsaved').textContent = msg;
            return;
        }
        if (current !== rowPath.split('.')[0]) {
            current = rowPath.split('.')[0];
            render();
        }
        const row = dialog.querySelector(`.settings-row[data-path="${rowPath}"]`);
        if (row) {
            row.querySelector('.settings-error').textContent = msg.replace(`${rowPath}: `, '');
            row.classList.add('invalid');
        }
        unsaved();
    }

    // Save: the draft's changes in one patch (checked whole: none of it
    // kept if any of it is wrong), then the dialog closes.
    async function saveDraft() {
        const bad = dialog.querySelector('.settings-row.invalid');
        if (bad) {
            const c = bad.querySelector('input, select');
            if (c)
                c.focus();
            return;
        }
        const patch = {};
        for (const p of changedPaths()) {
            const [cat, k] = p.split('.'), v = at(draft, p);
            (patch[cat] = patch[cat] || {})[k] = v === undefined ? null : clone(v);
        }
        const save = dialog.querySelector('.settings-save');
        save.disabled = true;
        try {
            if (Object.keys(patch).length)
                await set(patch);
            dialog.close();
        } catch (e) {
            showError(e.path || '', e.message);
            save.disabled = false;
        }
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
        // The help: a tooltip, on the mouse over the setting or the
        // keyboard's focus in it, and the control's description.
        const help = document.createElement('div');
        help.className = 'settings-help';
        help.id = `${id}-help`;
        help.setAttribute('role', 'tooltip');
        help.textContent = p.description || '';
        const error = document.createElement('div');
        error.className = 'settings-error';
        error.setAttribute('role', 'alert');
        const o = override(path);
        const locked = p.readOnly || (p['x-scope'] === 'server' && !canChangeServer);
        // Into the draft when it is valid; an Error (or an invalid value):
        // said under it, the draft as it was.
        const commit = (value) => {
            error.textContent = '';
            r.classList.remove('invalid');
            try {
                if (value instanceof Error)
                    throw value;
                const [cat, key] = path.split('.');
                draft = patched(draft, { [cat]: { [key]: value } });
            } catch (e) {
                error.textContent = e.message.replace(`${path}: `, '');
                r.classList.add('invalid');
            }
            for (const f of autoRefresh)
                f();
            unsaved();
        };
        const c = control(path, p, id, commit, locked);
        const target = c.id === id ? c : c.querySelector(`[id="${id}"]`);
        if (p.description)
            for (const t of [target, c.querySelector(`[id="${id}-auto"]`)])
                if (t)
                    t.setAttribute('aria-describedby', help.id);
        if (p.description) {
            const i = document.createElement('span');
            i.className = 'settings-info';
            i.setAttribute('aria-hidden', 'true');
            i.textContent = 'ⓘ';
            label.append(i);
        }
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
        right.append(error);
        r.append(label, right);
        if (p.description)
            r.append(help);
        return r;
    }

    function control(path, p, id, commit, locked) {
        const value = at(draft, path);
        const widget = p['x-widget'];
        const types = [].concat(p.type || []);
        let el;
        if (widget === 'disk-list')
            return diskList(id, value, commit, locked);
        if (widget === 'screen')
            return screenSize(id, p, value, commit, locked);
        if (p['x-auto'])
            return automatic(path, id, p, value, commit, locked);
        if (p['x-choices']) {
            // The choices the page has now, given the draft (the disks it
            // would put in): made again after each change in the dialog
            // and when the page's change (choicesChanged).
            el = document.createElement('select');
            const fill = () => {
                const v = at(draft, path);
                const got = (providers[p['x-choices']] ? providers[p['x-choices']](draft) : [])
                    .map((c) => (typeof c === 'string' ? { value: c, label: c } : c));
                const opts = [new Option("Executor's own", '')];
                for (const c of got)
                    if (c.value)
                        opts.push(new Option(c.label || c.value, c.value));
                if (v && !got.some((c) => c.value === v))
                    opts.push(new Option(`${v} (not here)`, v));
                el.replaceChildren(...opts);
                el.value = v;
            };
            fill();
            autoRefresh.push(fill);
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
                el.value = doc.advanced.data || '';     // (not in the draft: readOnly)
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
                commit(v);
                render();
            };
            const up = button('↑', '', () => move(i - 1));
            const down = button('↓', '', () => move(i + 1));
            const rm = button('✕', '', () => {
                commit(value.filter((_, j) => j !== i));
                render();
            });
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
            add.addEventListener('change', () => {
                if (add.value) {
                    commit([...(value || []), add.value]);
                    render();
                }
            });
            wrap.append(add);
        }
        return wrap;
    }

    // A setting with an automatic value (x-auto: the value that means it,
    // choices and labels): an Auto box, then the choices (or a number),
    // disabled while it is ticked, showing what Auto picks.
    const draftSystem = () => (draft && draft.machine && draft.machine.system) || '';
    const AUTO_PICKS = {
        'machine.ram_mb': () => (draftSystem() ? 128 : 32),
        'machine.depth': () => (/(^|\D)(8|9)\.\d/.test(draftSystem()) ? 32 : 8),
        'display.scale': () => Math.min(4, Math.max(1, Math.round(window.devicePixelRatio || 1))),
        'network.secure_cookies': () => location.protocol === 'https:',
    };
    let autoRefresh = [];               // the shown picks, after a change (the System's)

    function automatic(path, id, p, value, commit, locked) {
        const auto = p['x-auto'], isAuto = JSON.stringify(value) === JSON.stringify(auto.value);
        const wrap = document.createElement('span');
        wrap.className = 'settings-auto';
        const boxLabel = document.createElement('label');
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.id = `${id}-auto`;
        box.checked = isAuto;
        box.disabled = locked;
        boxLabel.append(box, 'Auto');
        const pick = AUTO_PICKS[path];
        let el, read, show;
        if (auto.choices) {
            el = document.createElement('select');
            const label = (v) => {
                const i = auto.choices.indexOf(v);
                return i >= 0 ? (auto.labels || [])[i] || String(v) : String(v);
            };
            const fill = (extra) => {
                const vals = [...auto.choices];
                if (extra !== undefined && !vals.includes(extra))
                    vals.push(extra);
                el.replaceChildren(...vals.map((v) => new Option(label(v), JSON.stringify(v))));
            };
            show = () => {
                if (box.checked) {
                    const v = pick ? pick() : auto.choices[0];
                    fill(v);
                    el.value = JSON.stringify(v);
                    el.options[el.selectedIndex].textContent = `${label(v)} (Auto)`;
                } else {
                    const v = at(draft, path);
                    fill(v);
                    el.value = JSON.stringify(v);
                }
            };
            read = () => JSON.parse(el.value);
        } else {
            el = document.createElement('input');
            el.type = 'number';
            if (p.minimum !== undefined)
                el.min = p.minimum;
            if (p.maximum !== undefined)
                el.max = p.maximum;
            el.step = 1;
            show = () => {
                el.value = box.checked ? '' : at(draft, path);
                el.placeholder = box.checked ? 'any' : '';
            };
            read = () => (el.value === '' ? NaN : Number(el.value));
        }
        el.id = id;
        const enable = () => { el.disabled = locked || box.checked; };
        show();
        enable();
        box.addEventListener('change', () => {
            if (box.checked)
                commit(auto.value);
            else    // what Auto picked, now chosen (a number: the default)
                commit(auto.choices ? read() : (p.default !== auto.value ? p.default : 1));
            show();
            enable();
            if (!box.checked)
                el.focus();
        });
        el.addEventListener('change', () => commit(read()));
        autoRefresh.push(() => { if (box.checked) show(); });
        wrap.append(boxLabel, el);
        return wrap;
    }

    // The screen's size: Auto (the setting x-auto-setting names: the
    // screen follows the window), the classic Macs' (the schema's
    // examples), or Custom… with a width and a height.
    function screenSize(id, p, value, commit, locked) {
        const wrap = document.createElement('span');
        wrap.className = 'settings-screen';
        const sel = document.createElement('select');
        sel.id = id;
        sel.disabled = locked;
        const sizes = p.examples || [];
        const autoPath = p['x-auto-setting'];
        const auto = (on) => {
            const [cat, key] = autoPath.split('.');
            draft = patched(draft, { [cat]: { [key]: on } });
        };
        if (autoPath) {
            const a = new Option('Auto', 'auto');
            a.title = lookup(autoPath).description || '';
            sel.append(a);
        }
        sel.append(...sizes.map((s, i) => new Option((p['x-labels'] || [])[i] || s, s)), new Option('Custom…', 'custom'));
        const custom = document.createElement('span');
        custom.className = 'settings-screen-custom';
        const [w0, h0] = String(value || '').split('x');
        const number = (suffix, v, label) => {
            const n = document.createElement('input');
            n.type = 'number';
            n.id = `${id}-${suffix}`;
            n.min = 100;
            n.max = 9999;
            n.step = 1;
            n.value = v || '';
            n.disabled = locked;
            n.setAttribute('aria-label', label);
            return n;
        };
        const w = number('w', w0, 'Width'), h = number('h', h0, 'Height');
        custom.append(w, ' × ', h);
        const show = () => { custom.hidden = sel.value !== 'custom'; };
        sel.value = autoPath && at(draft, autoPath) ? 'auto' : sizes.includes(value) ? value : 'custom';
        show();
        const typed = () => {
            const [a, b] = [w.value, h.value].map(Number);
            if (![a, b].every((x) => Number.isInteger(x) && x >= 100 && x <= 9999))
                return commit(new Error('width and height are 100 to 9999 pixels'));
            commit(`${a}x${b}`);
        };
        sel.addEventListener('change', () => {
            show();
            if (autoPath)
                auto(sel.value === 'auto');
            if (sel.value === 'auto') {
                commit(at(draft, 'machine.screen'));    // (the size kept for when Auto is off)
            } else if (sel.value === 'custom') {
                w.focus();
                typed();
            } else {
                [w.value, h.value] = sel.value.split('x');
                commit(sel.value);
            }
        });
        w.addEventListener('change', typed);
        h.addEventListener('change', typed);
        wrap.append(sel, custom);
        return wrap;
    }

    async function open(category) {
        await ready;
        if (!dialog)
            build();
        if (category)
            current = category;
        if (!dialog.open)
            draft = clone(doc);
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
        choicesChanged,
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
