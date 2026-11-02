// The Disks panel: the System disk, the disks in the Mac, the library.
// The bar's Disks button opens it (a side panel over the page's right
// edge). main.js owns the disks; this draws them and says what the user
// asked for, through the handlers given to Disks.init:
//   add(files)        files picked or dropped (disk images, applications)
//   insert(disk)      a library disk to put in the Mac
//   insertShelved(name), forgetShelved(name)
//                     one of one's own disks taken out earlier ("Your disks")
//   eject(name)       a disk to take out
//   revert(name)      forget a disk's changes
//   download(name)    the image as it is now
//   reset()           forget the changes to Executor's disk for this System
//   systemSelect      the page's System choice (a <select>, main.js), shown here
// Disks.update(state) draws what is there now:
//   { system: { value, label }, mounted: [{ name, title, cover,
//     kind: 'hd'|'cd'|'floppy'|'app', state: { modified, changed (bytes),
//     writable, inMemory, borrowed, onServer }, inserting }],
//     library: [{ title, url, note, tags, size, cover }], mountedUrls,
//     shelf: [{ name, size }] }
'use strict';

const Disks = (() => {
    let on = {}, state = { system: { value: '', label: "Executor's own" }, mounted: [], library: [], mountedUrls: [], shelf: [] };
    let panel = null, button = null, drop = null, filter = '', tags = new Set();

    const ICONS = {
        hd: '<svg viewBox="0 0 32 32"><rect x="2" y="9" width="28" height="15" rx="2" fill="#bbb" stroke="#555"/>'
            + '<rect x="5" y="18" width="10" height="2" fill="#666"/><circle cx="25" cy="19" r="1.5" fill="#4c4"/></svg>',
        cd: '<svg viewBox="0 0 32 32"><circle cx="16" cy="16" r="13" fill="#ccd" stroke="#667"/>'
            + '<circle cx="16" cy="16" r="9" fill="none" stroke="#aac" stroke-width="2"/><circle cx="16" cy="16" r="3" fill="#2b2b2b" stroke="#667"/></svg>',
        floppy: '<svg viewBox="0 0 32 32"><path d="M4 3h21l3 3v23H4z" fill="#556" stroke="#334"/>'
            + '<rect x="9" y="3" width="13" height="9" fill="#ccc"/><rect x="17" y="4" width="3" height="7" fill="#556"/>'
            + '<rect x="7" y="17" width="18" height="12" fill="#eee"/></svg>',
        app: '<svg viewBox="0 0 32 32"><path d="M16 3l12 7v12l-12 7-12-7V10z" fill="#99a" stroke="#556"/>'
            + '<path d="M16 3v26M4 10l12 7 12-7" stroke="#556" fill="none"/></svg>',
        system: '<svg viewBox="0 0 32 32"><rect x="5" y="2" width="22" height="26" rx="3" fill="#ccc" stroke="#555"/>'
            + '<rect x="8" y="5" width="16" height="12" fill="#335"/><path d="M13 9v2M19 9v2M13 14q3 2 6 0" stroke="#9cf" fill="none"/>'
            + '<rect x="9" y="22" width="8" height="1.5" fill="#666"/></svg>',
    };

    const el = (tag, cls, text) => {
        const e = document.createElement(tag);
        if (cls)
            e.className = cls;
        if (text !== undefined)
            e.textContent = text;
        return e;
    };
    const btn = (text, title, fn) => {
        const b = el('button', '', text);
        b.title = title;
        b.addEventListener('click', fn);
        return b;
    };
    const icon = (kind) => {
        const s = el('span', 'pic');
        s.innerHTML = ICONS[kind] || ICONS.hd;
        return s;
    };
    const picture = (d, kind) => {
        if (!d.cover)
            return icon(kind);
        const s = el('span', 'pic');
        const img = new Image();
        img.src = d.cover;
        img.alt = '';
        s.append(img);
        return s;
    };
    const size = (n) => n >= 1e9 ? `${(n / 1e9).toFixed(1)} GB` : n >= 1e6 ? `${Math.round(n / 1e6)} MB`
        : n >= 1e3 ? `${Math.round(n / 1e3)} KB` : `${n} bytes`;
    const isCD = (d) => /\.(iso|toast|cdr)$/i.test(d.name || d.url || '') || (d.tags || []).includes('cd');

    // A mounted disk's state: its badge and what it means.
    function describe(d) {
        const st = d.state || {};
        if (d.kind === 'app')
            return { badge: ['original', 'Application'], explain: 'Copied into Executor’s disk, in the Uploads folder.' };
        if (d.kind === 'cd')
            return { badge: ['cd', 'CD · read-only'], explain: 'Read-only, as a CD: nothing on it changes.' };
        const where = st.onServer ? 'on the server' : st.inMemory || st.borrowed ? 'in memory only' : 'in this browser';
        if (st.borrowed)
            return { badge: ['memory', 'In memory'], explain: 'Open in another tab: changes here last until this tab closes.' };
        if (st.modified)
            return { badge: ['changed', 'Changed'], changed: true,
                     explain: `${st.changed ? size(st.changed) + ' changed' : 'Changed'}, kept ${where}. `
                              + 'The image itself is untouched: Revert forgets the changes, Download saves the disk as it is now.' };
        if (st.inMemory && !st.onServer)
            return { badge: ['original', 'Original'], explain: 'Changes would last until Executor restarts (not a secure page).' };
        return { badge: ['original', 'Original'], explain: '' };
    }

    function mountedRow(d) {
        const row = el('div', 'disk');
        const info = describe(d);
        if (info.changed)
            row.classList.add('changed');
        if (d.inserting)
            row.classList.add('inserting');
        row.dataset.name = d.name;
        const title = el('div', 'title', d.title || d.name);
        title.title = d.title || d.name;
        const file = el('div', 'file');
        file.append(el('span', `badge ${info.badge[0]}`, d.inserting ? 'Inserting…' : info.badge[1]),
                    d.title && d.title !== d.name ? d.name : '');
        const acts = el('div', 'acts');
        const revert = btn('↺', 'Revert: forget the changes to this disk', () => on.revert && on.revert(d.name));
        revert.disabled = !info.changed;
        revert.className = 'revert';
        const dl = btn('⤓', 'Download the image as it is now', () => on.download && on.download(d.name));
        dl.className = 'download';
        const ej = btn('⏏', 'Eject (its changes are kept)', () => on.eject && on.eject(d.name));
        ej.className = 'eject';
        acts.append(...(d.kind === 'app' ? [] : [revert, dl]), ej);
        row.append(picture(d, d.kind), title, acts, file);
        if (info.explain)
            row.append(el('div', 'explain', info.explain));
        return row;
    }

    function systemSection() {
        const wrap = el('section', 'sys');
        wrap.id = 'disks-system';
        const h = el('h2', '', 'System disk');
        h.append(el('span', 'hint', 'what the Mac starts from'));
        const pick = el('div', 'system-pick');
        if (on.systemSelect)
            pick.append(on.systemSelect);
        const row = el('div', 'disk');
        const title = el('div', 'title', state.system.label);
        const file = el('div', 'file');
        file.append(el('span', 'badge system', 'Startup'),
                    state.system.value ? 'Apple’s System and Finder from this disk' : 'Executor’s own System, no disk needed');
        const acts = el('div', 'acts');
        const reset = btn('Reset…', "Forget what was changed on Executor's disk for this System (preferences, installed files)",
                          () => on.reset && on.reset());
        reset.id = 'reset';
        acts.append(reset);
        let explain = 'Your changes to the System Folder are kept for this System; Reset forgets them. '
            + 'Changing the System restarts the Mac.';
        // The System's disk image: its own changes, as a mounted disk's.
        const disk = state.system.disk;
        if (disk) {
            row.dataset.name = disk.name;
            const info = describe(disk);
            file.append(el('span', `badge ${info.badge[0]}`, info.badge[1]));
            if (info.changed)
                row.classList.add('changed');
            if (info.explain)
                explain = `${info.explain} ${explain}`;
            const revert = btn('↺', 'Revert: forget the changes to this disk (restarts the Mac)',
                               () => on.revert && on.revert(disk.name));
            revert.className = 'revert';
            revert.disabled = !info.changed;
            const dl = btn('⤓', 'Download the image as it is now', () => on.download && on.download(disk.name));
            dl.className = 'download';
            acts.prepend(revert, dl);
        }
        row.append(icon('system'), title, acts, file, el('div', 'explain', explain));
        wrap.append(h, pick, row);
        return wrap;
    }

    function mountedSection() {
        const wrap = el('section');
        wrap.id = 'disks-mounted';
        const h = el('h2', '', 'In the Mac');
        h.append(el('span', 'hint', `${state.mounted.length || 'no'} disk${state.mounted.length === 1 ? '' : 's'}`));
        wrap.append(h);
        if (!state.mounted.length)
            wrap.append(el('div', 'empty-note', 'No disks in the Mac. Insert one from the library below, or add your own.'));
        for (const d of state.mounted)
            wrap.append(mountedRow(d));
        const zone = el('div', 'add-zone');
        zone.append(btn('Add disk image or application…', 'A disk image (.img, .dsk, .iso, …) or an application in MacBinary',
                        () => document.getElementById('add').click()), document.createTextNode('or drop files here'));
        wrap.append(zone);
        return wrap;
    }

    function librarySection() {
        const wrap = el('section');
        wrap.id = 'disks-library';
        const mounted = new Set(state.mountedUrls.map((u) => new URL(u, location.href).href));
        const lib = state.library;
        const h = el('h2', '', 'Library');
        h.append(el('span', 'hint', `${lib.length} disk${lib.length === 1 ? '' : 's'} on this site`));
        wrap.append(h);
        if (!lib.length) {
            wrap.append(el('div', 'empty-note', 'This site offers no library.'));
            return wrap;
        }
        const f = el('div', 'lib-filter');
        const search = document.createElement('input');
        search.type = 'search';
        search.placeholder = 'Filter: title, note, contents';
        search.value = filter;
        search.addEventListener('input', () => {
            filter = search.value;
            drawCards();
        });
        f.append(search);
        const tagBox = el('div', 'lib-tags');
        const all = [...new Set(lib.flatMap((d) => d.tags || []))].sort();
        for (const t of all) {
            const b = btn(t, `Only disks tagged ${t}`, () => {
                tags.has(t) ? tags.delete(t) : tags.add(t);
                b.classList.toggle('on', tags.has(t));
                drawCards();
            });
            b.classList.toggle('on', tags.has(t));
            tagBox.append(b);
        }
        const grid = el('div', 'lib-grid');
        function drawCards() {
            const words = filter.toLowerCase().split(/\s+/).filter(Boolean);
            const shown = lib.filter((d) => [...tags].every((t) => (d.tags || []).includes(t))
                && words.every((w) => `${d.title} ${d.note || ''} ${(d.tags || []).join(' ')}`.toLowerCase().includes(w)));
            grid.replaceChildren(...shown.map((d) => {
                const card = el('div', 'lib-card');
                const here = mounted.has(new URL(d.url, location.href).href);
                card.classList.toggle('mounted', here);
                card.title = d.note ? `${d.title}: ${d.note}` : d.title;
                card.dataset.url = d.url;
                const art = el('div', 'art');
                if (d.cover) {
                    const img = new Image();
                    img.src = d.cover;
                    img.alt = '';
                    img.loading = 'lazy';
                    art.append(img);
                } else {
                    art.innerHTML = ICONS[isCD(d) ? 'cd' : (d.tags || []).includes('system') ? 'system' : 'hd'];
                }
                const ins = btn('Insert', 'Put this disk in the Mac', (e) => {
                    e.stopPropagation();
                    on.insert && on.insert(d);
                });
                ins.className = 'insert';
                card.append(art, el('div', 'title', d.title),
                            el('div', 'meta', [d.size ? size(d.size) : '', ...(d.tags || [])].filter(Boolean).join(' · ')), ins);
                if (!here)
                    card.addEventListener('click', () => on.insert && on.insert(d));
                return card;
            }));
            if (!shown.length)
                grid.replaceChildren(el('div', 'empty-note', 'No disk matches.'));
        }
        drawCards();
        wrap.append(f, tagBox, grid);
        return wrap;
    }

    // One's own disks taken out: put back in, or forgotten.
    function shelfSection() {
        const wrap = el('section');
        wrap.id = 'disks-shelf';
        const h = el('h2', '', 'Your disks');
        h.append(el('span', 'hint', 'taken out, kept in this browser'));
        wrap.append(h);
        for (const d of state.shelf) {
            const row = el('div', 'disk');
            row.dataset.name = d.name;
            const acts = el('div', 'acts');
            const ins = btn('Insert', 'Put this disk in the Mac', () => on.insertShelved && on.insertShelved(d.name));
            ins.className = 'insert';
            const del = btn('✕', 'Forget this disk (deleted from this browser)', () => on.forgetShelved && on.forgetShelved(d.name));
            del.className = 'forget';
            acts.append(ins, del);
            const file = el('div', 'file', d.size ? size(d.size) : '');
            row.append(icon(isCD(d) ? 'cd' : d.size <= 1440 * 1024 ? 'floppy' : 'hd'), el('div', 'title', d.name), acts, file);
            wrap.append(row);
        }
        return wrap;
    }

    function render() {
        if (button) {
            const n = state.mounted.length;
            button.replaceChildren(document.createTextNode('Disks'), ...(n ? [el('span', 'count', String(n))] : []));
        }
        if (!panel || panel.hidden)
            return;
        const scroll = panel.querySelector('.scroll');
        const top = scroll.scrollTop;
        scroll.replaceChildren(systemSection(), mountedSection(),
                               ...(state.shelf.length ? [shelfSection()] : []), librarySection());
        scroll.scrollTop = top;
    }

    function build() {
        panel = el('aside');
        panel.id = 'disks-panel';
        panel.hidden = true;
        panel.setAttribute('aria-label', 'Disks');
        const header = el('header');
        header.append(el('h1', '', 'Disks'), btn('✕', 'Close', () => toggle(false)));
        header.lastChild.className = 'close';
        panel.append(header, el('div', 'scroll'));
        document.body.append(panel);
        // Files dropped anywhere on the page: added (and the panel opened).
        drop = el('div', '', 'Drop disk images or applications to add them');
        drop.id = 'disks-drop';
        drop.hidden = true;
        document.body.append(drop);
        let depth = 0;
        const files = (e) => e.dataTransfer && [...e.dataTransfer.types].includes('Files');
        addEventListener('dragenter', (e) => { if (files(e)) { depth++; drop.hidden = false; } });
        addEventListener('dragleave', (e) => { if (files(e) && --depth <= 0) { depth = 0; drop.hidden = true; } });
        addEventListener('dragover', (e) => { if (files(e)) e.preventDefault(); });
        addEventListener('drop', (e) => {
            if (!files(e))
                return;
            e.preventDefault();
            depth = 0;
            drop.hidden = true;
            toggle(true);
            if (on.add && e.dataTransfer.files.length)
                on.add([...e.dataTransfer.files]);
        });
        addEventListener('keydown', (e) => { if (e.key === 'Escape' && !panel.hidden) toggle(false); });
    }

    function toggle(open = panel.hidden) {
        panel.hidden = !open;
        document.body.classList.toggle('disks-open', open);
        dispatchEvent(new Event('resize'));         // the screen fits what is left (main.js)
        button.classList.toggle('open', open);
        render();
    }

    function init(handlers) {
        on = handlers;
        build();
        button = document.getElementById('disks-button');
        button.addEventListener('click', () => toggle());
        render();
    }

    function update(s) {
        state = { ...state, ...s };
        render();
    }

    return { init, update, open: () => toggle(true), close: () => toggle(false), get state() { return state; } };
})();
