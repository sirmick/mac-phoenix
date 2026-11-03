// M1's targets used in the tab (docs/executor/M1.md): each application on
// a library disk opened on one of the site's systems, as tab_site.mjs
// does, and then made to do its job, as a user would, through the keyboard
// and mouse only: a document typed and saved, a script or a build run, a
// game taken past its title screen. Each application gets a fresh load of
// the page (the Finder again); one that crashes the tab gets a new tab.
//
//   node web/tests/tab_m1.mjs DIR LIBRARY SYSTEM DISK
//
// DIR the wasm build's page, LIBRARY tools/web/make_library.py's output,
// SYSTEM 7.6.1 or 8.6, DISK a library disk there (productivity.img, creative.img,
// development.img, tools-8.img, games-68k.img, games-ppc.img). The targets are TARGETS
// below, not the disk record's launch list (which tab_site.mjs opens). Every
// target is expected to work, but the known exceptions (KNOWN: stopped at
// exactly their message): one that doesn't fails the cell, with what it
// got to (M1.md keeps the table). M1_APPS=Name,Name: only those.
// M1_QUERY: more of the page's query (debug=fslog, logtraps=NAME...).
// M1_RESULTS=FILE: one JSON line per application appended. The canvas after
// each step goes to M1_SHOTS (default ~/storage/tmp/m1/shots) as
// SYSTEM-NAME-STEP.png. M1_LOG=1: the Worker's console. M1_WINDOWS=1: the
// window list (dialogs' text too) when a target fails.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, newPage, firstFrame, request as ask, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, library, system, diskName] = process.argv.slice(2);
const SHOTS = process.env.M1_SHOTS || path.join(os.homedir(), 'storage/tmp/m1/shots');

// ---- the targets ----
// app: the path on the library disk; name: CurApName in front; systems; ppc:
// the page's PowerPC box for it (8.6 always); use(t): what it does with it,
// throwing (fail) with what went wrong. t: the helpers below.
// known: a known exception (M1.md): the target passes when it stops at
// exactly this message, and fails on anything else.
const DOCUMENT = 'Hello from M1';

// The games that stop at a message of their own, on any Mac: Pathways'
// manual copy protection, EV Nova wants QuickTime 3 (M2), Ferazel's Wand
// its CD.
const KNOWN = {
    'Pathways Into Darkness': /in the order they appear on the right side of page \d+ in your manual/,
    'EV Nova': /^Sorry, Nova requires Quicktime 3\.0 or newer\.$/,
    "Ferazel's Wand": /^Please insert the Ferazel's Wand CD and try again\.$/,
};
function game(name, app, system, start) {
    const known = KNOWN[name];
    return { name, app, systems: [system], known, use: (t) => known ? t.stopsAt(known) : t.play(start) };
}
// Photoshop (4.0, 5.5): File > New (its dialog, titled New: any other dialog
// is the alert that stopped it), the paintbrush (B) drawn across the image,
// Save As.
async function photoshop(t, doc) {
    const alert = async () => {
        const d = (await t.windows(true)).find((w) => w.kind === DIALOG_KIND && w.title !== 'New');
        if (d)
            throw new Error(`alert "${said(d).replace(/\s+/g, ' ')}"`);
    };
    await t.until(async () => {
        await alert();
        return (await t.windows()).some((w) => w.title === 'Layers');
    }, 60000, 'no palettes');
    await t.key('Meta+KeyN');
    await t.until(async () => {
        await alert();
        return (await t.windows(true)).some((w) => w.title === 'New');   // a movable modal (kind 8)
    }, 20000, 'no New dialog');
    await t.key('Enter');
    await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 20000, 'no Untitled window');
    const w = (await t.windows()).find((x) => /^Untitled/.test(x.title));
    // (the rectangle is the frame's: 8.x's title bar is 22 pixels, the
    // scroll bars and the status line 16; a stroke from the title bar
    // drags the window, and Save does nothing for an untouched document)
    const [top, left, bottom, right] = w.rect;
    const before = await sample();
    await t.key('KeyB');
    await t.drag([[left + 20, top + 40], [right - 40, (top + bottom) / 2], [left + 20, bottom - 40]]);
    await t.pause(1000);
    if (changed(before, await sample()) < 0.002)
        throw new Error('nothing drawn');
    await t.saveAs(doc);
}
const TARGETS = {
    'productivity.img': [
        { name: 'AppleWorks', app: 'Productivity:AppleWorks 5.0.3 ƒ:AppleWorks', use: async (t) => {
            // New Document: word processing (the default) -> type -> Save As
            await t.until(async () => (await t.windows()).some((w) => w.title === 'New Document'), 20000, 'no New Document dialog');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => /^untitled/.test(w.title)), 20000, 'no untitled document');
            await t.type(DOCUMENT);
            await t.saveAs('M1 AW');
        } },
        { name: 'ClarisDraw 1.0v4', app: 'Productivity:ClarisDraw 1.0v4 ƒ:ClarisDraw 1.0v4', use: async (t) => {
            // no window at launch: File > New, the text tool (the palette's
            // second), a click on the page, typed, saved
            await t.pause(5000);
            await t.key('Meta+KeyN');
            await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 30000, 'no Untitled window');
            await t.pause(2000);
            await t.click(14, 68);
            await t.pause(500);
            await t.click(200, 160);
            await t.pause(500);
            await t.type(DOCUMENT);
            await t.saveAs('M1 CD');
        } },
        { name: 'Corel WordPerfect', app: 'Productivity:Corel WordPerfect 3.5e ƒ:Corel WordPerfect', use: async (t) => {
            await t.document();
            await t.type(DOCUMENT);
            await t.shot('typed');
            await t.until(async () => (await t.drawn()).some((s) => s.includes('Hello')), 10000, 'the text never drawn');
            await t.saveAs('M1 WP');
        } },
        { name: 'Microsoft Excel', app: 'Productivity:Microsoft Excel 4.0 ƒ:Microsoft Excel', use: async (t) => {
            // Worksheet1, A1 typed
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Worksheet1'), 30000, 'no Worksheet1');
            await t.type(DOCUMENT);
            await t.key('Enter');
            await t.saveAs('M1 XL');
        } },
        { name: 'FileMaker Pro', app: 'Productivity:FileMaker Pro 4.0 ƒ:FileMaker Pro', use: async (t) => {
            // New Database: an empty file, named; a text field defined; the
            // first record's field typed (FileMaker saves as it goes)
            await t.until(async () => (await t.windows()).some((w) => w.title === 'New Database'), 30000, 'no New Database dialog');
            await t.clickButton('Create a new empty file');
            await t.key('Enter');
            await t.until(async () => !(await t.windows()).some((w) => w.title === 'New Database'), 20000, 'New Database stayed');
            await t.until(async () => (await t.windows()).some((w) => w.kind === DIALOG_KIND), 20000, 'no Create dialog');
            await t.pause(1000);
            await t.type('M1 FM');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => /^Define Fields/.test(w.title)), 30000, 'no Define Fields');
            await t.pause(1000);
            await t.type('Greeting');
            await t.clickButton('Create');
            await t.pause(1000);
            await t.clickButton('Done');
            await t.until(async () => (await t.windows()).some((w) => w.title === 'M1 FM'), 30000, 'no M1 FM window');
            await t.pause(2000);
            await t.type(DOCUMENT);
            await t.key('Enter');
            await t.until(async () => (await t.drawn()).some((s) => s.includes('Hello')), 10000, 'the text never drawn');
        } },
        { name: 'Aldus Persuasion 3.0', app: 'Productivity:Aldus Persuasion 3.0 ƒ:Aldus Persuasion 3.0', use: async (t) => {
            // its start window at launch (500x300, untitled, as on the
            // Quadra; 504x304 with its frame): Create presentation (its
            // first button, 398,18 in the content) makes one, the Tools
            // palette up; its text tool (top right) on the slide, typed,
            // saved; an alert instead fails
            let start;
            await t.until(async () => {
                const d = (await t.windows(true)).find((w) => w.kind === DIALOG_KIND);
                if (d)
                    throw new Error(`alert "${said(d).replace(/\s+/g, ' ')}"`);
                start = (await t.windows()).find((w) => w.title === '' && w.rect[3] - w.rect[1] === 504 && w.rect[2] - w.rect[0] === 304);
                return start;
            }, 60000, 'no start window');
            await t.pause(2000);
            await t.click(start.rect[1] + 1 + 398, start.rect[0] + 1 + 18);
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Tools'), 60000, 'no Tools palette');
            await t.document();
            const tools = (await t.windows()).find((w) => w.title === 'Tools');
            await t.click(tools.rect[3] - 14, tools.rect[0] + 26);   // the text tool, "A"
            await t.click(320, 240);
            await t.pause(1000);
            const before = await sample();   // the text box up
            await t.type(DOCUMENT);
            // its text isn't in the drawn-text feed: the screen changed
            // (a line of text is a few samples of sample()'s grid)
            await t.until(async () => changed(before, await sample()) > 0, 10000, 'the text never drawn');
            await t.saveAs('M1 PE');
        } },
        { name: 'Microsoft Works 4.0', app: 'Productivity:Microsoft Works 4.0b ƒ:Microsoft Works 4.0', use: async (t) => {
            // Microsoft Works Startup at launch, Word Processor picked: New
            // (Return) -> an untitled word processor document -> typed, saved
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Microsoft Works Startup'), 60000,
                          'no Microsoft Works Startup');
            await t.pause(1000);
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 30000, 'no Untitled document');
            await t.pause(1000);
            await t.type(DOCUMENT);
            await t.until(async () => (await t.drawn()).some((s) => s.includes('Hello')), 10000, 'the text never drawn');
            await t.saveAs('M1 MW');
        } },
    ],
    'creative.img': [
        { name: 'Kid Pix', app: 'Creative:Kid Pix 2.5 ƒ:Kid Pix', use: async (t) => {
            // its full-screen page (no title): the pencil (the palette's
            // first) drawn across it, the page then different; Save As
            // (Standard File), the dialog gone and no alert
            await t.pause(5000);
            const blank = await sample();
            await t.click(20, 40);
            await t.pause(500);
            await t.drag([[150, 150], [450, 160], [460, 350], [160, 340], [150, 150]]);
            await t.pause(1000);
            if (changed(blank, await sample()) < 0.002)
                throw new Error('nothing drawn');
            await t.key('Meta+KeyS');
            await t.until(async () => (await t.windows()).some((w) => w.kind === DIALOG_KIND), 20000, 'no Save dialog');
            await t.pause(1000);
            await t.type('M1 KP');
            await t.key('Enter');
            await t.until(async () => !(await t.windows()).some((w) => w.kind === DIALOG_KIND), 30000, 'the Save dialog stayed');
            await t.pause(2000);
            const d = (await t.windows()).find((w) => w.kind === DIALOG_KIND);
            if (d)
                throw new Error(`alert "${said(d).replace(/\s+/g, ' ')}"`);
        } },
        { name: 'Adobe Photoshop® 4.0', app: 'Creative:Adobe Photoshop 4.0 ƒ:Adobe Photoshop® 4.0',
          use: (t) => photoshop(t, 'M1 PS') },
        { name: 'Adobe Illustrator® 6.0', app: 'Creative:Adobe Illustrator 6.0 ƒ:Adobe Illustrator® 6.0', use: async (t) => {
            // Untitled art 1 opens at launch; the rectangle tool (the
            // toolbox's sixth row, second column) dragged across it, Save As.
            // On the Quadra the toolbox's content is 49x241 at the left.
            await t.until(async () => (await t.windows()).some((w) => /^Untitled art/.test(w.title)), 60000, 'no Untitled art');
            const tools = (await t.windows()).find((w) => w.title === '' && w.rect[2] - w.rect[0] > 200 && w.rect[3] - w.rect[1] < 70);
            if (!tools)
                throw new Error('no toolbox');
            const w = (await t.windows()).find((x) => /^Untitled art/.test(x.title));
            const [top, left, bottom, right] = w.rect;
            const before = await sample();
            await t.click(tools.rect[1] + 36, tools.rect[0] + 98);
            await t.pause(500);
            await t.drag([[left + 250, top + 100], [left + 450, top + 300]]);
            await t.pause(1000);
            if (changed(before, await sample()) < 0.002)
                throw new Error('nothing drawn');
            await t.saveAs('M1 AI');
        } },
        { name: 'FreeHand 5.5', app: 'Creative:Macromedia FreeHand 5.5 ƒ:FreeHand 5.5', use: async (t) => {
            // File > New, the rectangle tool (the toolbox's second row,
            // left) dragged across the page, Save As
            await t.until(async () => (await t.windows()).some((w) => w.title === '_Tools'), 60000, 'no toolbox');
            await t.key('Meta+KeyN');
            await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 30000, 'no Untitled window');
            await t.pause(2000);
            const tools = (await t.windows()).find((w) => w.title === '_Tools');
            const before = await sample();
            await t.click(tools.rect[1] + 13, tools.rect[0] + 39);
            await t.pause(500);
            await t.drag([[220, 160], [420, 300]]);
            await t.pause(1000);
            if (changed(before, await sample()) < 0.002)
                throw new Error('nothing drawn');
            await t.saveAs('M1 FH');
        } },
        { name: 'Canvas™ 5', app: 'Creative:Deneba Canvas 5.0.2 ƒ:Canvas™ 5', use: async (t) => {
            // Untitled-1 opens at launch; the rectangle tool (the
            // toolbox's third row, left) dragged across the page, Save As.
            // On the Quadra the toolbox's content is 47x191 at the left.
            await t.until(async () => {
                const m = (await t.windows(true)).find((w) => w.title === 'Canvas Message' || w.kind === DIALOG_KIND);
                if (m)
                    throw new Error(`alert "${said(m).replace(/\s+/g, ' ')}"`);
                return (await t.windows()).some((w) => /Untitled-1/.test(w.title));
            }, 90000, 'no Untitled-1');
            await t.pause(2000);
            const tools = (await t.windows()).find((w) => w.title === '' && w.rect[2] - w.rect[0] > 150 && w.rect[3] - w.rect[1] < 70);
            if (!tools)
                throw new Error('no toolbox');
            const before = await sample();
            await t.click(tools.rect[1] + 11, tools.rect[0] + 58);
            await t.pause(500);
            await t.drag([[220, 160], [400, 330]]);
            await t.pause(1000);
            if (changed(before, await sample()) < 0.002)
                throw new Error('nothing drawn');
            await t.saveAs('M1 CV');
        } },
    ],
    'creative-8.img': [
        { name: 'Adobe Illustrator® 8.0', app: 'Creative:Adobe Illustrator 8.0 ƒ:Adobe Illustrator® 8.0', use: async (t) => {
            // File > New, the rectangle tool (M) dragged across it, Save As
            await t.until(async () => (await t.windows()).some((w) => w.title === '' && w.rect[2] - w.rect[0] > 200), 60000, 'no toolbox');
            await t.key('Meta+KeyN');
            await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 30000, 'no Untitled window');
            await t.pause(2000);
            const w = (await t.windows()).find((x) => /^Untitled/.test(x.title));
            const [top, left] = w.rect;
            const before = await sample();
            await t.key('KeyM');
            await t.pause(500);
            await t.drag([[left + 150, top + 100], [left + 350, top + 260]]);
            await t.pause(1000);
            if (changed(before, await sample()) < 0.002)
                throw new Error('nothing drawn');
            await t.saveAs('M1 AI8');
        } },
        { name: 'Adobe® Photoshop® 5.5', app: 'Creative:Adobe Photoshop 5.5 ƒ:Adobe® Photoshop® 5.5',
          use: (t) => photoshop(t, 'M1 PS5') },
        { name: 'Adobe® PageMaker® 6.5', app: 'Creative:Adobe PageMaker 6.5 ƒ:Adobe® PageMaker® 6.5', use: async (t) => {
            // File > New (Document Setup's defaults), text typed with the
            // text tool (Shift+F4) on the page, Save As
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Styles' || w.title === 'Colors'), 90000, 'no palettes');
            await t.key('Meta+KeyN');
            await t.until(async () => (await t.windows(true)).some((w) => w.kind === DIALOG_KIND), 20000, 'no Document Setup');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => /^Untitled/.test(w.title)), 30000, 'no Untitled window');
            await t.pause(2000);
            const w = (await t.windows()).find((x) => /^Untitled/.test(x.title));
            const [top, left, bottom, right] = w.rect;
            const before = await sample();
            await t.key('Shift+F4');
            await t.pause(500);
            await t.click((left + right) / 2 - 60, (top + bottom) / 2 - 40);
            await t.pause(500);
            await t.type(DOCUMENT);
            await t.pause(1000);
            if (changed(before, await sample()) < 0.0005)
                throw new Error('nothing typed');
            await t.saveAs('M1 PM');
        } },
    ],
    'development.img': [
        { name: 'MPW Shell', app: 'Development:MPW:MPW Shell', use: async (t) => {
            // The Worksheet: a compile (SC) and its {Status} in an alert.
            await t.until(async () => (await t.windows()).some((w) => /Worksheet$/.test(w.title)), 30000, 'no Worksheet');
            await t.key('Meta+KeyA');
            await t.type('SC -o "{MPW}M1Count.c.o" "{MPW}Examples:CExamples:Count.c"; Alert "M1 build {Status}"');
            await t.key('Meta+KeyA', 'NumpadEnter');
            await t.alertSays(/M1 build 0/, 120000);
        } },
        { name: 'CodeWarrior IDE 3.2', app: 'Development:CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2',
          ppc: true, use: async (t) => {
            // A new project from stationery (Standard Console C), made and run.
            await t.codewarrior();
        } },
        { name: 'THINK Project Manager', app: 'Development:THINK C 6.0 ƒ:THINK C:THINK Project Manager', use: async (t) => {
            // The Object Bullseye demo opened from the Open dialog, converted
            // to THINK C 6's format, run (brought up to date first). Not the
            // plain Bullseye demo: its bullWindow.c doesn't compile against
            // THINK C 6's headers, on the Quadra either (FillOval's pattern).
            await t.sfOpen(['Development', 'THINK C', 'Demos', 'OOP Demos', 'Object Bullseye', 'Object B']);
            await t.until(async () => {
                const d = (await t.windows(true)).find((w) => w.kind === DIALOG_KIND);
                if (d && (d.items || []).some((i) => i.title === 'Convert'))
                    await t.clickButton('Convert'); // its default is Cancel
                return (await t.windows()).some((w) => /Object Bullseye/.test(w.title));
            }, 30000, 'no Object Bullseye project window');
            await t.key('Meta+KeyR');
            // "Bring the project up to date?": Update; anything else: its default
            await t.until(async () => {
                const d = (await t.windows(true)).find((w) => w.kind === DIALOG_KIND);
                if (d && (d.items || []).some((i) => i.title === 'Update'))
                    await t.clickButton('Update');
                else if (d)
                    await t.key('Enter');
                return (await t.app()) !== 'THINK Project Manager';
            }, 180000, 'the demo never ran');
        } },
        { name: 'THINK Pascal 4.5', app: 'Development:Languages ƒ:THINK Pascal 4.5d4 ƒ:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5',
          use: async (t) => {
            // An Instant Project, its program replaced, run: "M1 42" in Text.
            await t.until(async () => (await t.windows()).some((w) => w.kind === 2), 20000, 'no Open dialog');
            await t.clickButton('New');
            await t.pause(2000);
            await t.clickButton('Instant Project');
            await t.type('M1Proj');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => w.title === 'M1Proj.p'), 30000, 'no M1Proj.p');
            await t.key('Meta+KeyA');
            await t.type("program M1Proj;\nbegin\nwriteln('M1 ', 6 * 7);\nreadln\nend.\n");
            await t.key('Meta+KeyR');
            // Runtime.lib isn't beside the application on this disk: located
            // through the dialogs, as a user would; then save before running.
            const end = Date.now() + 180000;
            for (;;) {
                const ws = await t.windows();
                if (ws.some((w) => w.title === 'Text'))
                    break;
                if (Date.now() > end)
                    throw new Error('no Text window');
                const said = ws.filter((w) => w.kind === 2).map((w) => [w.text, ...(w.params || [])].join(' ')).join(' ');
                if (/Can.t find the file .Runtime\.lib/.test(said)) {
                    await t.key('Enter');
                    await t.pause(2000);
                    await t.sfOpen(['Development', 'Languages', 'THINK Pascal', 'Think Pascal', 'THINKPas', 'Runtime']);
                } else if (/Can.t find the file .Interface\.lib/.test(said)) {
                    await t.key('Enter');
                    await t.pause(2000);
                    await t.sfOpen(['Development', 'Languages', 'THINK Pascal', 'Think Pascal', 'THINKPas', 'Interface.lib']);
                } else if (/in all SUBSEQUENT|save the changes/.test(said))
                    await t.key('Enter');
                await t.pause(1500);
            }
            await t.shot('text');
        } },
        { name: 'HyperCard', app: 'Development:Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard', use: async (t) => {
            // The message box: answer "M1" && 6*7
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Home' && w.visible), 30000, 'no Home stack');
            await t.key('Meta+KeyM');
            await t.pause(1500);
            await t.type('answer "M1" && 6*7');
            await t.key('Enter');
            await t.alertSays(/M1 42/, 20000);
        } },
        { name: 'MacPerl', app: 'Development:Languages ƒ:MacPerl 5.2.0r4 ƒ:MacPerl', use: async (t) => {
            // A new script window, run (Cmd-Shift-R: Run Front Window).
            await t.key('Meta+KeyN');
            await t.until(async () => (await t.windows()).some((w) => w.title === 'Untitled' && w.visible), 20000, 'no Untitled window');
            await t.type('MacPerl::Answer("M1 " . 6*7);');
            await t.key('Meta+Shift+KeyR');
            await t.alertSays(/M1 42/, 30000);
        } },
        { name: 'ResEdit', app: 'Development:Tools ƒ:ResEdit 2.1.3 ƒ:ResEdit 2.1.3:ResEdit', use: async (t) => {
            // Past the splash, New file "M1 RE", a STR resource made in it.
            // The splash is a dialog with no buttons; then the Open dialog.
            const buttons = async () => ((await t.windows(true)).find((w) => w.kind === DIALOG_KIND) || { items: [] })
                .items.filter((i) => i.type >= 4 && i.type < 8).map((i) => i.title);
            await t.until(async () => (await t.windows()).some((w) => w.kind === DIALOG_KIND), 30000, 'no splash');
            await t.click(320, 240);
            await t.until(async () => (await buttons()).includes('New'), 30000, 'no Open dialog with New');
            await t.clickButton('New');
            await t.pause(2000);
            await t.type('M1 RE');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => w.title === 'M1 RE'), 20000, 'no M1 RE window');
            await t.key('Meta+KeyK');
            await t.pause(2000);
            await t.type('STR ');
            await t.key('Enter');
            await t.until(async () => (await t.windows()).some((w) => /^STR .*from M1 RE/.test(w.title)), 20000, 'no STR editor');
            await t.key('Meta+KeyS');
        } },
        { name: 'BBEdit Lite 4.1', app: 'Development:Tools ƒ:BBEdit Lite 4.1 ƒ:BBEdit Lite 4.1', use: async (t) => {
            await t.until(async () => (await t.windows()).some((w) => w.title === 'untitled'), 20000, 'no untitled window');
            await t.type(DOCUMENT);
            await t.saveAs('M1 BB');
        } },
    ],
    'tools-8.img': [
        { name: 'BBEdit Lite 6.1', app: 'Tools:Tools ƒ:BBEdit Lite 6.1.2 ƒ:BBEdit Lite 6.1', systems: ['8.6'], use: async (t) => {
            // its first-run survey ("Do It" mails Bare Bones): Cancel
            const untitled = async () => (await t.windows()).some((w) => /^untitled/.test(w.title));
            await t.until(async () => await untitled() || (await t.windows()).some((w) => w.kind === DIALOG_KIND), 30000,
                          'no window');
            if ((await t.windows()).some((w) => w.kind === DIALOG_KIND)) {
                await t.clickButton('Cancel');
                await t.pause(2000);
            }
            if (!await untitled())
                await t.key('Meta+KeyN');
            await t.until(untitled, 30000, 'no untitled window');
            await t.type(DOCUMENT);
            await t.saveAs('M1 BB6');
        } },
    ],
    'games-68k.img': [
        ['A-Train', 'A-Train ƒ:A-Train'], ['Apeiron', 'Apeiron 1.0.2 ƒ:Apeiron', [[426, 46]]], ['Avara', 'Avara 1.0.2 ƒ:Avara'],
        ['Bolo 0.99.7', 'Bolo 0.99.7 ƒ:Bolo 0.99.7'], ['Civilization™', 'Civilization 1.0.6 ƒ:Civilization™', ['Enter']],
        ['Exile v2.0.1', 'Exile 2.0.1 ƒ:Exile v2.0.1'], ['Exile II v2.0.3', 'Exile II 2.0.3 ƒ:Exile II v2.0.3'],
        ['Lemmings', 'Lemmings 1.5.2 ƒ:Lemmings', [[136, 354]]], ['Maelstrom', 'Maelstrom 1.4.2 ƒ:Maelstrom', ['KeyP']],
        ['Master of Orion', 'Master of Orion ƒ:Master of Orion', [[396, 278]]],
        ['Pathways Into Darkness', 'Pathways into Darkness 1.1 ƒ:Pathways ƒ:Pathways Into Darkness'],
        ['MacPopulousII', 'Populous II ƒ:MacPopulousII:MacPopulousII', [[186, 240]]], ['Prince of Persia', 'Prince of Persia ƒ:Prince of Persia'],
        ['SimEarth Color', 'SimEarth 2.0 ƒ:SimEarth Color'], ['SimLife™ Color', 'SimLife 1.0.1 ƒ:SimLife™ Color'],
        ['Spectre 1.1', 'Spectre 1.1 ƒ:Spectre 1.1', [[503, 139], 'KeyP']], ['Swoop', 'Swoop 1.0.2 ƒ:Swoop', ['KeyN']],
        ['Monkey Island', 'The Secret of Monkey Island ƒ:Monkey Island'],
    ].map(([name, app, start]) => game(name, `Games:${app}`, '7.6.1', start)),
    'games-ppc.img': [
        ['Marathon v1.2', 'Marathon 1.2 ƒ:Marathon v1.2', [[316, 156]]], ['Marathon 2', 'Marathon 2 ƒ:Marathon 2', [[144, 192]]],
        ['Escape Velocity', 'Escape Velocity 1.0.5 ƒ:Escape Velocity', [[200, 274], 'Enter', 'Enter', [440, 270]]], ['EV Override', 'EV Override 1.0.2 ƒ:EV Override', [[200, 272], 'Enter', 'Enter', [440, 270]]],
        ['Bubble Trouble', 'Bubble Trouble 1.0.1 ƒ:Bubble Trouble', [[240, 242]]], ['Barrack', 'Barrack 1.0.4 ƒ:Barrack', [[130, 96]]],
        ['Chiral', 'Chiral 1.0.4 ƒ:Chiral', [[407, 94]]], ['Flashback', 'Flashback 1.0 ƒ:Flashback', ['Enter']],
        ['SimCity 2000® 1.2', 'SimCity 2000® Collection ƒ:SimCity 2000® 1.2', ['Enter']],
        ['Wolfenstein 3D™', 'Wolfenstein 3D 1.0.2 ƒ:Wolfenstein 3D™'],
        ['EV Nova', 'EV Nova 1.0.10 ƒ:EV Nova'], ["Ferazel's Wand", "Ferazel's Wand 1.0.3 ƒ:Ferazel's Wand"],
        ['Slithereens', 'Slithereens 1.0.1 ƒ:Slithereens'], ['Mars Rising', 'Mars Rising 1.0.2 ƒ:Mars Rising', [[465, 86]]],
    ].map(([name, app, start]) => game(name, `Games PPC:${app}`, '8.6', start)).concat([
        { name: 'GameBox', app: 'Games PPC:GameBox Classic 1.2.0 ƒ:GameBox', systems: ['8.6'], use: async (t) => {
            // REALbasic: Welcome -> Continue, Choose Profile -> New, a
            // profile named and saved, then its library window
            const shown = (title) => async () => (await t.windows()).some((w) => w.title === title);
            await t.until(shown('Welcome to GameBox'), 60000, 'no Welcome window');
            await t.click(459, 307);
            await t.until(shown('Choose Profile'), 30000, 'no Choose Profile window');
            await t.click(264, 165);
            await t.until(shown('New Profile'), 30000, 'no New Profile window');
            await t.click(370, 113);
            await t.type('M1');
            await t.click(221, 181);
            await t.until(shown('GameBox'), 60000, 'no library window');
            await t.pause(5000);
            await shot('library');
        } },
    ]),
};

// ---- the page ----
const records = {};
for (const f of fs.existsSync(library || '') ? fs.readdirSync(library) : [])
    if (f.endsWith('.img.json'))
        records[f.slice(0, -'.json'.length)] = JSON.parse(fs.readFileSync(path.join(library, f), 'utf8'));
const boot = Object.keys(records).find((n) => records[n].system === system);
if (!['7.6.1', '8.6'].includes(system) || !boot || !TARGETS[diskName] || !fs.existsSync(path.join(library, diskName))) {
    console.log(`SKIP: no ${system} boot disk or ${diskName} in ${library} (tools/web/make_library.py)`);
    process.exit(77);
}
const only = process.env.M1_APPS ? process.env.M1_APPS.split(',') : null;
const targets = TARGETS[diskName].filter((a) => (a.systems || ['7.6.1', '8.6']).includes(system)
                                                && (!only || only.includes(a.name)));

// A request, or null after ms: answered at the application's next event
// call, so an application stuck (or crashed) answers nothing.
const request = (op, arg, ms = 15000) => Promise.race([
    ask(page, op, arg), new Promise((r) => setTimeout(() => r(null), ms))]);

// The screen, coarsely: grey levels of every 4th pixel of every 4th row; null
// when the Worker doesn't answer.
const sample = () => Promise.race([page.evaluate(async () => {
    const p = await executor.request('pixels');
    const sx = p.width / 640, sy = p.height / 480, out = [];
    for (let y = 0; y < 480; y += 4)
        for (let x = 0; x < 640; x += 4) {
            const i = (Math.floor(y * sy) * p.width + Math.floor(x * sx)) * 4;
            out.push((p.data[i] + p.data[i + 1] + p.data[i + 2]) / 3 | 0);
        }
    return out;
}), new Promise((r) => setTimeout(() => r(null), 15000))]).catch(() => null);
const changed = (a, b) => !a || !b ? 0 : a.reduce((n, v, i) => n + (Math.abs(v - b[i]) > 8), 0) / a.length;

let page = null, crashed = false, current = '';
async function shot(step) {
    const png = await Promise.race([page.evaluate(async () => {
        const p = await executor.request('pixels');
        const c = new OffscreenCanvas(p.width, p.height);
        c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.width, p.height), 0, 0);
        const bytes = new Uint8Array(await (await c.convertToBlob({ type: 'image/png' })).arrayBuffer());
        let bin = '';
        for (let i = 0; i < bytes.length; i += 0x8000)
            bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return btoa(bin);
    }), new Promise((r) => setTimeout(() => r(null), 15000))]).catch(() => null);
    if (!png)
        return;
    fs.mkdirSync(SHOTS, { recursive: true });
    fs.writeFileSync(path.join(SHOTS, `${system}-${current}-${step}`.replace(/[^\w.-]+/g, '_') + '.png'),
                     Buffer.from(png, 'base64'));
}

// A character as the key a US keyboard makes it with (Shift held for
// capitals and the shifted symbols: the tab reads codes, not characters).
const SHIFTED = { '!': 'Digit1', '@': 'Digit2', '#': 'Digit3', '$': 'Digit4', '%': 'Digit5', '^': 'Digit6',
                  '&': 'Digit7', '*': 'Digit8', '(': 'Digit9', ')': 'Digit0', _: 'Minus', '+': 'Equal',
                  '{': 'BracketLeft', '}': 'BracketRight', '|': 'Backslash', ':': 'Semicolon', '"': 'Quote',
                  '<': 'Comma', '>': 'Period', '?': 'Slash', '~': 'Backquote' };
const PLAIN = { '-': 'Minus', '=': 'Equal', '[': 'BracketLeft', ']': 'BracketRight', '\\': 'Backslash',
                ';': 'Semicolon', "'": 'Quote', ',': 'Comma', '.': 'Period', '/': 'Slash', '`': 'Backquote',
                ' ': 'Space', '\n': 'Enter', '\t': 'Tab' };
function keyOf(ch) {
    if (/[a-z]/.test(ch))
        return `Key${ch.toUpperCase()}`;
    if (/[A-Z]/.test(ch))
        return `Shift+Key${ch}`;
    if (/[0-9]/.test(ch))
        return `Digit${ch}`;
    if (SHIFTED[ch] || PLAIN[ch])
        return SHIFTED[ch] ? `Shift+${SHIFTED[ch]}` : PLAIN[ch];
    throw new Error(`no key for ${JSON.stringify(ch)}`);
}

const at = async (x, y) => {
    const b = await page.locator('#screen').boundingBox();
    return [b.x + x * b.width / 640, b.y + y * b.height / 480];
};
const DIALOG_KIND = 2;
const ERROR = /error|sorry|requires?\b|required|cannot|can't|could not|couldn't|unable|not enough|out of memory|newer version|insert the|damaged|modified by|(^|\s)-\d{3,}/i;
// (an alert: a game's own dialog full of rules isn't one)
const failing = (w) => w && w.kind === DIALOG_KIND && said(w).length < 400 && ERROR.test(said(w));
const said = (w) => w.kind !== DIALOG_KIND ? ''
    : /\^[0-3]/.test(w.text) ? w.text.replace(/\^([0-3])/g, (_, i) => (w.params || [])[i] || '')
        : [w.text, ...(w.params || [])].filter(Boolean).join('\n');

// A game's dialog answered by the first of these buttons it has.
const SAFE = [/^Not Yet/i, /^Later/i, /^Leave Alone/i, /^Don.t (Change|Switch)/i, /^Switch\b/i, /^(Continue|OK|Okay)\b/i, /^(Play|Start|Begin|New Game)\b/i, /^(Done|Skip)\b/i, /^Cancel\b/i];

// M1_LOG: each action too.
const trace = (...a) => process.env.M1_LOG && console.log('[m1]', ...a);

// What a target's use() has to work with.
const t = {
    pause: (ms) => page.waitForTimeout(ms),
    app: () => request('app'),
    windows: async (items) => {
        trace('windows?');
        const ws = (await request('windows', items ? 'items' : '')) || [];
        trace('windows', JSON.stringify(ws.filter((w) => w.visible).map((w) => [w.title, w.kind, (w.items || []).length])));
        return ws.filter((w) => w.visible && w.title !== 'Desktop');
    },
    // the strings drawn lately, oldest first (web_main.cpp's 'drawn')
    drawn: async () => (await request('drawn')) || [],
    shot,
    async key(...combos) {
        trace('key', combos.join(' '));
        await page.locator('#screen').focus();
        for (const c of combos) {
            await page.keyboard.press(c);
            await page.waitForTimeout(150);
        }
    },
    async type(text) {
        trace('type', text);
        await page.locator('#screen').focus();
        for (const ch of text) {
            await page.keyboard.press(keyOf(ch));
            await page.waitForTimeout(40);
        }
    },
    // held a moment: games that poll Button() see it
    async click(x, y) {
        trace('click', x, y);
        await page.mouse.move(...await at(x, y));
        await page.mouse.down();
        await page.waitForTimeout(200);
        await page.mouse.up();
    },
    // the button held along points (a pencil stroke)
    async drag(points) {
        trace('drag', JSON.stringify(points));
        await page.mouse.move(...await at(...points[0]));
        await page.mouse.down();
        for (const p of points.slice(1))
            await page.mouse.move(...await at(...p), { steps: 10 });
        await page.mouse.up();
    },
    async until(fn, ms, why) {
        if (!await until(fn, ms, 500))
            throw new Error(why);
    },
    // A dialog (or alert) whose text matches re.
    async alertSays(re, ms) {
        let last = '';
        const ok = await until(async () => {
            const d = (await t.windows()).find((w) => w.kind === DIALOG_KIND);
            last = d ? said(d) : last;
            return d && re.test(said(d));
        }, ms, 1000);
        await shot('alert');
        if (!ok)
            throw new Error(`no alert saying ${re}${last ? ` (last: "${last.replace(/\s+/g, ' ')}")` : ''}`);
        await t.key('Enter');
    },
    // Save As: the Standard File dialog, the name typed (the field's text
    // is selected), the window then has the name.
    async saveAs(name) {
        await t.key('Meta+KeyS');
        await t.until(async () => (await t.windows()).some((w) => w.kind === DIALOG_KIND), 20000, 'no Save dialog');
        await t.pause(1000);
        await t.type(name);
        await t.key('Enter');
        const lower = name.toLowerCase();
        await t.until(async () => (await t.windows()).some((w) => w.title.toLowerCase().startsWith(lower)), 30000,
                      `no window named ${name} after saving`);
    },
    // A control of the front dialog clicked by its title (a prefix:
    // "New" for "New..."), the dialog's items as Executor has them.
    // (or the item itself, as found)
    async clickButton(title) {
        const d = typeof title === 'string' && (await t.windows(true)).find((w) => w.kind === DIALOG_KIND);
        const item = typeof title !== 'string' ? title
            : d && (d.items || []).find((i) => i.type >= 4 && i.type < 8 && i.title.startsWith(title));
        if (!item)
            throw new Error(`no ${title} in the dialog (${d ? d.items.map((i) => i.title).filter(Boolean).join(', ') : 'none'})`);
        const [top, left, bottom, right] = item.rect;
        await t.click((left + right) / 2, (top + bottom) / 2);
    },
    // A document window: the one the application opened at launch, or
    // File > New's.
    async document() {
        const doc = async () => (await t.windows()).some((w) => w.kind !== DIALOG_KIND);
        if (!await until(doc, 20000, 1000))
            await t.key('Meta+KeyN');
        await t.until(doc, 30000, 'no document window');
        await t.pause(1000);
    },
    // Standard File's Open: from the Desktop, each name typed (type-select)
    // and opened.
    async sfOpen(names) {
        await t.clickButton('Desktop');
        await t.pause(1500);
        for (const n of names) {
            await t.type(n);
            await t.key('Enter');
            await t.pause(1500);
        }
    },
    // CodeWarrior: a project from the Standard Console stationery (C, 68K)
    // in a folder of its own, made, then run: its console window.
    async codewarrior() {
        await t.key('Meta+Shift+KeyN');
        let d = null;
        await t.until(async () => (d = (await t.windows()).find((w) => w.title === 'New Project')), 30000, 'no New Project dialog');
        // the stationery list's rows, from the dialog's corner: MacOS,
        // C_C++, Standard Console (their triangles), Std C Console 68K, OK
        const [top, left] = d.rect;
        for (const [x, y] of [[23, 93], [23, 107], [23, 135], [129, 149], [253, 224]]) {
            await t.click(left + x, top + y);
            await t.pause(1500);
        }
        await t.until(async () => (await t.windows()).some((w) => w.kind === DIALOG_KIND), 30000, 'no dialog to name the project');
        await t.type('M1CW');
        await t.key('Enter');
        await t.until(async () => (await t.windows()).some((w) => /^M1CW/.test(w.title)), 60000, 'no M1CW project window');
        // "Errors & Warnings" after a make: errors fail, with their lines
        // (as drawn); warnings only: closed
        const checkMade = async () => {
            if (!(await t.windows()).some((w) => w.title === 'Errors & Warnings'))
                return;
            const errors = (await t.drawn()).filter((s) => /couldn.t|error|undefined|not found/i.test(s))
                .filter((s) => !/^Errors (&|and) warnings/i.test(s));
            if (errors.length)
                throw new Error(`Make: ${[...new Set(errors)].slice(0, 4).join(' | ')}`);
            await t.key('Meta+KeyW');
            await t.pause(1500);
        };
        await t.key('Meta+KeyM');                          // Make
        await t.pause(10000);
        await checkMade();
        await t.key('Meta+KeyR');                          // Run (makes first if it must)
        await t.until(async () => {
            await checkMade();
            return (await t.app()) === 'Std C Console 68K';
        }, 300000, 'the program never ran');
        // its SIOUX console window, the program's output in it
        await t.until(async () => (await t.windows()).some((w) => w.title === 'Std C Console 68K.out'), 60000,
            'no console window');
        await t.until(async () => (await t.drawn()).some((s) => /Hello World/.test(s)), 30000,
            'no "Hello World" in the console');
    },
    // A known exception: a dialog saying exactly re, nothing else (an
    // error alert of another kind fails at once).
    async stopsAt(re) {
        let last = '';
        const ok = await until(async () => {
            const d = (await t.windows()).find((w) => w.kind === DIALOG_KIND);
            if (!d)
                return false;
            last = said(d).replace(/\s+/g, ' ');
            if (re.test(last))
                return true;
            if (failing(d))
                throw new Error(`alert "${last}", not ${re}`);
            return false;
        }, 60000, 1000);
        await shot('known');
        if (!ok)
            throw new Error(`never stopped at ${re}${last ? ` (last dialog: "${last}")` : ''}`);
    },
    // A game: its title screen, then taken on as a player would: a dialog
    // answered by its harmless button (a shareware notice's "Not Yet", never
    // its default "Register..."), a splash clicked away, then the game's
    // own start (start: key combos, or [x, y] clicks on its New Game),
    // which must change the screen; passed when the
    // screen is then something else, the game still up (in front, or too
    // busy to answer) and no error alert.
    async play(start = []) {
        await t.pause(8000);
        const title = await sample();
        await shot('title');
        for (let round = 0; round < 4; round++) {
            const ws = await t.windows(true);
            const d = ws.find((w) => w.kind === DIALOG_KIND);
            if (failing(d))
                throw new Error(`alert "${said(d).replace(/\s+/g, ' ')}"`);
            const buttons = d ? (d.items || []).filter((i) => i.type >= 4 && i.type < 8) : [];
            const button = SAFE.map((re) => buttons.find((i) => re.test(i.title))).find(Boolean);
            if (button)
                await t.clickButton(button);
            else if (!d)
                await t.click(320, 240);
            await t.pause(3000);
        }
        const menu = await sample();
        await shot('menu');
        for (const k of start) {
            if (Array.isArray(k))
                await t.click(...k);
            else
                await t.key(k);
            await t.pause(3000);
        }
        await t.pause(3000);
        const then = await sample();
        await shot('played');
        const now = await t.app();
        if (now !== null && now !== current)
            throw new Error(`then gone (${now} in front)`);
        const d = (await t.windows()).find((w) => w.kind === DIALOG_KIND);
        if (failing(d))
            throw new Error(`alert "${said(d).replace(/\s+/g, ' ')}"`);
        if (changed(title, then) < 0.1)
            throw new Error('still the title screen');
        if (start.length && changed(menu, then) < 0.05)
            throw new Error('the game never started');
    },
};

// ---- the run ----
const server = await startServer(dir, { images: library, include: [boot, diskName] });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const browser = await launch();
async function openTab(target) {
    const query = `?disk=disks/${encodeURIComponent(boot)}&disk=disks/${encodeURIComponent(diskName)}`
        + `&system=${encodeURIComponent(boot)}${system === '8.6' || target.ppc ? '&ppc=1' : ''}`
        + (process.env.M1_QUERY ? `&${process.env.M1_QUERY}` : '');
    if (!page || crashed) {
        const context = page ? page.context() : null;
        if (page)
            await page.close().catch(() => {});
        page = context ? await context.newPage() : await newPage(browser, { log: !!process.env.M1_LOG });
        page.on('dialog', (d) => d.accept());
        page.on('crash', () => { crashed = true; });
        page.on('pageerror', (e) => console.log('[page error]', e.message));
        if (context && process.env.M1_LOG)
            page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
        crashed = false;
    }
    await page.goto(`${server.url}/${query}`);
    await firstFrame(page, 120000);
    return until(() => request('app').then((a) => a === 'Finder'), 240000, 500);
}

let failed = 0;
const table = [];
try {
    for (const target of targets) {
        current = target.name;
        const t0 = Date.now();
        let ok = false, why = '';
        try {
            if (!await openTab(target))
                throw new Error('no Finder');
            await page.waitForTimeout(2000);
            const desktop = await sample();
            const err = await request('launch', target.app, 60000);
            if (err !== '0' && err !== 0)
                throw new Error(`LaunchApplication: ${err}`);
            // in front; or, a game waiting for the mouse in a loop of its
            // own, answering nothing, with the screen its own
            let last;
            if (!await until(async () => (last = await request('app', '', 5000)) === target.name
                             || (last === null && changed(desktop, await sample()) > 0.3), 90000, 500))
                throw new Error(`not in front (${last})`);
            await shot('front');
            await target.use(t);
            ok = true;
        } catch (e) {
            why = crashed ? `the tab crashed (${e.message})` : e.message;
            // what the page said of it (main.js: an Executor fatal, a trap)
            const stopped = crashed ? null : await page.evaluate(() => window.executor.stopped).catch(() => null);
            if (stopped)
                why += ` (Executor stopped: ${stopped})`;
            if (!crashed && process.env.M1_WINDOWS)
                for (const w of await t.windows(true).catch(() => []))
                    console.log(`  window "${w.title}" kind ${w.kind} [${w.rect}]${said(w) ? `: ${said(w).replace(/\s+/g, ' ')}` : ''}`);
        }
        if (!crashed)
            await shot(ok ? 'done' : 'failed');
        failed += !ok;
        if (ok && target.known)
            why = `known exception (${target.known.source})`;
        const line = `${ok ? 'PASS' : 'FAIL'} ${system} ${target.name}${why ? `: ${why}` : ''}`;
        console.log(line);
        table.push({ system, disk: diskName, app: target.name, ok, why, seconds: Math.round((Date.now() - t0) / 1000) });
    }
} finally {
    await browser.close();
    server.stop();
}
if (process.env.M1_RESULTS)
    fs.appendFileSync(process.env.M1_RESULTS, table.map((r) => JSON.stringify(r) + '\n').join(''));
console.log(failed ? `${failed} of ${table.length} not used` : `all ${table.length} used`);
process.exit(failed ? 1 : 0);
