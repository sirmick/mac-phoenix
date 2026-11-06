// What Executor's start waits for, step by step, over its screen until the
// first frame (main.js): the module (downloaded with its size and percent,
// or from this browser's cache, then compiled), the disks, what the server
// keeps (Executor's disk, the disks' changes), the Mac starting.
//
//   Loading.begin()                        a start: the panel shown
//   Loading.step('module', { state, detail, got, total })
//                                          state: 'active' 'done' 'error';
//                                          got/total: bytes (a bar)
//   Loading.end()                          the first frame: gone
//   Loading.active                         shown now
//   Loading.log                            what the steps said ("module done
//                                          ready"), for tests
'use strict';

const Loading = (() => {
    const STEPS = [
        ['module', 'Executor'],
        ['disks', 'Disks'],
        ['saved', 'Your saved files'],
        ['start', 'Starting the Mac'],
    ];
    const el = document.createElement('div');
    el.id = 'loading';
    el.hidden = true;
    el.setAttribute('role', 'status');
    el.setAttribute('aria-live', 'polite');
    const title = document.createElement('div');
    title.className = 'loading-title';
    title.textContent = 'Starting Executor…';
    const list = document.createElement('ol');
    const rows = {};
    for (const [key, text] of STEPS) {
        const li = document.createElement('li');
        li.dataset.step = key;
        li.className = 'pending';
        const name = document.createElement('span');
        name.className = 'loading-name';
        name.textContent = text;
        const detail = document.createElement('span');
        detail.className = 'loading-detail';
        const bar = document.createElement('progress');
        bar.max = 1;
        bar.hidden = true;
        bar.setAttribute('aria-label', text);
        li.append(name, detail, bar);
        list.append(li);
        rows[key] = { li, detail, bar };
    }
    el.append(title, list);

    const mb = (n) => (n >= 1e6 ? `${(n / 1e6).toFixed(n < 1e7 ? 1 : 0)} MB` : `${Math.round(n / 1e3)} KB`);

    const log = [];
    function step(key, { state = 'active', detail, got, total } = {}) {
        const r = rows[key];
        if (!r)
            return;
        r.li.className = state;
        if (got !== undefined) {
            r.detail.textContent = total ? `${mb(got)} of ${mb(total)} (${Math.floor(got * 100 / total)}%)` : mb(got);
            r.bar.hidden = false;
            if (total)
                r.bar.value = got / total;
            else
                r.bar.removeAttribute('value');     // (indeterminate)
        } else if (detail !== undefined) {
            r.detail.textContent = detail;
        }
        if (state !== 'active' || got === undefined)
            r.bar.hidden = true;
        const line = `${key} ${state} ${r.detail.textContent}`;
        if (log[log.length - 1] !== line && log.push(line) > 500)
            log.shift();
    }

    return {
        el, step, log,
        get active() { return !el.hidden; },
        // A start: every step pending but the disks' (fetched once a visit,
        // main.js: fetchDisks, maybe done already).
        begin() {
            for (const [key] of STEPS)
                if (key !== 'disks' || rows.disks.li.className === 'pending' || rows.disks.li.className === 'error')
                    step(key, { state: 'pending', detail: '' });
            el.hidden = false;
        },
        end() { el.hidden = true; },
    };
})();
