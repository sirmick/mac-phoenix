#!/usr/bin/env python3
"""Machine illustrations for the web UI, made with an OpenRouter image model.

Each machine is drawn from two references: a photograph of the real thing
(Wikimedia Commons, fetched once into --raw/ref/) for its shape and front
panel, and --raw/style.png, one illustration of the set, for the drawing
style, so the set matches (Executor, a PC, has no photo). With no
style.png the first machine drawn becomes it. The renders (white background, ~800 KB) are kept in --raw;
what the UI loads is cut from them: background made transparent with the
soft shadow kept as translucent black (the UI is dark grey), cropped,
scaled to --width.

    tools/art/machines.py                 # draw what's missing, cut all
    tools/art/machines.py iici --force    # redraw one
    tools/art/machines.py --cut-only      # just redo the cut

Key: $OPENROUTER_API_KEY or ~/openrouter.token.
"""
import argparse, base64, json, os, sys, time, urllib.parse, urllib.request
import numpy as np
from PIL import Image
from scipy import ndimage as ndi
from pathlib import Path

URL = 'https://openrouter.ai/api/v1/chat/completions'
COMMONS = 'https://commons.wikimedia.org/w/api.php'
UA = 'mac-phoenix-art/1.0 (https://github.com/sirmick/mac-phoenix)'
ROOT = Path(__file__).resolve().parents[2]

STYLE = (
    "A clean product illustration in a consistent set of classic computers. "
    "Three-quarter front view from slightly above, facing left, centered, the whole "
    "machine in frame with generous margin. Soft studio lighting, gentle shading, "
    "crisp outlines, subtle soft shadow under the machine. Warm platinum-grey plastic. "
    "Plain flat pure white background (#FFFFFF), nothing else in the scene: no desk, "
    "no keyboard, no mouse, no people, no captions, no watermark, no lettering except "
    "what is moulded on the real machine. Any screen shows a simple classic Mac OS "
    "desktop: grey patterned desktop, white menu bar, a couple of small icons on the "
    "right, no legible text."
)

# name: (Commons photo of the real machine or None, what to draw)
MACHINES = {
    'se': ('Macintosh SE b.jpg',
           "Apple Macintosh SE (1987), the compact all-in-one: built-in 9-inch "
           "black-and-white screen in a tall bezel; below it a band of horizontal "
           "grille lines with two floppy-drive slots set into the lines on the right; "
           "rainbow Apple logo at the lower left; the narrow recessed foot at the bottom. "
           "The screen is black and white only."),
    'iici': ('Apple-macintosh-iici.jpg',
             "Apple Macintosh IIci (1989): the low, wide horizontal desktop case whose "
             "front and sides are covered in fine horizontal grooves, a single floppy "
             "slot in a groove on the right of the front, rainbow Apple logo and "
             "'Macintosh IIci' at the lower left. An Apple 13-inch RGB colour display "
             "sits on top of it."),
    'quadra650': ('Quadra 650 Front.jpg',
                  "Apple Macintosh Quadra 650 (1993): a low, wide horizontal desktop case "
                  "(not a tower) with a grid of ventilation holes on the left of the top; "
                  "the front has a rainbow Apple logo badge at the upper left, a speaker "
                  "grille below it, the floppy slot at the upper right and the CD-ROM "
                  "drive door below it. An Apple 14-inch colour display sits on top of it."),
    'g3': ('Macintosh G3 DT.jpg',
           "Apple Power Macintosh G3 Desktop (1997), the beige one: a horizontal desktop "
           "case with a front that slopes back at the bottom, ventilation slots on the "
           "left; front panel with the rainbow Apple logo and 'Power Macintosh G3' at the "
           "upper left, the floppy slot at the upper right, the CD-ROM tray and a "
           "drive bay below, round power button at the lower left. A matching beige "
           "Apple 15-inch colour CRT display sits on top of it."),
    # Executor (ARDI's, 1990s): Mac software on a PC, no Apple ROM. Not one
    # model, so no photo.
    'executor': (None,
                 "Not a Macintosh: a generic mid-1990s beige IBM-compatible PC, a "
                 "mini-tower with a 5.25-inch CD-ROM drive, a 3.5-inch floppy drive, a "
                 "turbo button and two small LEDs, no logos or brand names anywhere, "
                 "standing to the right of a matching beige 15-inch PC CRT monitor on a "
                 "swivel stand. The monitor shows a classic Macintosh desktop all the "
                 "same: the Mac running on the PC."),
}


def read_key():
    k = os.environ.get('OPENROUTER_API_KEY')
    if k: return k.strip()
    p = Path.home() / 'openrouter.token'
    if p.exists(): return p.read_text().strip()
    sys.exit('no OpenRouter key: set OPENROUTER_API_KEY or put it in ~/openrouter.token')


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': UA}), timeout=60) as r:
        return r.read()


def photo(ref_dir, title):
    """The Commons file, fetched once (at most 1024 px wide)."""
    path = ref_dir / title.replace(' ', '_')
    if not path.exists():
        q = urllib.parse.urlencode({'action': 'query', 'prop': 'imageinfo', 'iiprop': 'url',
                                    'iiurlwidth': 1024, 'format': 'json', 'titles': 'File:' + title})
        page = next(iter(json.loads(get(f'{COMMONS}?{q}'))['query']['pages'].values()))
        info = page['imageinfo'][0]
        ref_dir.mkdir(parents=True, exist_ok=True)
        path.write_bytes(get(info.get('thumburl') or info['url']))
    return path.read_bytes()


def mime(b):
    return 'image/png' if b[:4] == b'\x89PNG' else 'image/jpeg'


def generate(key, model, prompt, refs):
    content = [{'type': 'text', 'text': prompt}]
    for r in refs:
        content.append({'type': 'image_url', 'image_url': {
            'url': f'data:{mime(r)};base64,' + base64.b64encode(r).decode()}})
    body = {'model': model, 'modalities': ['image', 'text'],
            'image_config': {'aspect_ratio': '1:1', 'image_size': '1K'},
            'messages': [{'role': 'user', 'content': content}]}
    last = None
    for attempt in range(3):
        req = urllib.request.Request(URL, data=json.dumps(body).encode(), headers={
            'Authorization': 'Bearer ' + key, 'Content-Type': 'application/json',
            'X-Title': 'mac-phoenix'})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                d = json.load(r)
        except Exception as e:
            last = str(e); time.sleep(5); continue
        if 'error' in d:
            last = json.dumps(d['error'])[:300]; time.sleep(3); continue
        msg = d['choices'][0]['message']
        imgs = msg.get('images') or []
        if imgs:
            b64 = imgs[0]['image_url']['url'].split(',', 1)[1]
            return base64.b64decode(b64), d.get('usage', {}).get('cost', 0)
        last = 'no image: ' + str(msg.get('content'))[:200]
    sys.exit('generation failed: ' + str(last))


def cut(src, dst, width):
    """White background -> transparent. The background is the near-white,
    unsaturated region connected to the border (the outlines fence the
    machine in); its darkness becomes black at that opacity, so the drop
    shadow survives on any background."""
    a = np.asarray(Image.open(src).convert('RGB')).astype(float)
    L = a.mean(2)
    lab, _ = ndi.label((L > 170) & (a.max(2) - a.min(2) < 20))
    edge = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    bg = np.isin(lab, edge[edge > 0])
    alpha = np.where(bg, np.clip((252 - L) / 252, 0, 1) ** 0.8, 1.0)
    rgb = np.where(bg[..., None], 0, a)
    im = Image.fromarray(np.dstack([rgb, alpha * 255]).astype(np.uint8), 'RGBA')
    ys, xs = np.nonzero(alpha > 8 / 255)
    im = im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    if im.width > width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    im.save(dst, optimize=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help=f"machines (default all): {' '.join(MACHINES)}")
    ap.add_argument('--raw', type=Path, default=Path.home() / 'storage' / 'art' / 'machines')
    ap.add_argument('--out', type=Path, default=ROOT / 'client' / 'machines')
    ap.add_argument('--width', type=int, default=384)
    ap.add_argument('--cut-only', action='store_true', help="don't draw, just cut the renders")
    ap.add_argument('--model', default='google/gemini-3-pro-image')
    ap.add_argument('--force', action='store_true', help='redo images that exist')
    a = ap.parse_args()
    names = a.names or list(MACHINES)
    for n in names:
        if n not in MACHINES: sys.exit(f'unknown machine {n}')
    a.raw.mkdir(parents=True, exist_ok=True)
    a.out.mkdir(parents=True, exist_ok=True)
    key = None if a.cut_only else read_key()
    style = a.raw / 'style.png'
    total = 0.0
    for n in names:
        path = a.raw / f'{n}.png'
        if a.cut_only or (path.exists() and not a.force):
            continue
        title, what = MACHINES[n]
        refs, prompt = [], STYLE + '\n\nThe machine: ' + what
        if title:
            refs.append(photo(a.raw / 'ref', title))
            prompt += ('\n\nThe first attached image is a photograph of the real machine: '
                       'copy its shape, proportions and front-panel layout faithfully (not '
                       'its camera angle, background or colour cast).')
        if style.exists():
            refs.append(style.read_bytes())
            prompt += ('\n\nThe ' + ('second ' if title else '') + 'attached image is '
                       'another machine from the same illustration set: match its drawing '
                       'style, lighting, viewing angle, line weight and colour palette '
                       'exactly, but draw the machine above.')
        t = time.time()
        img, cost = generate(key, a.model, prompt, refs)
        path.write_bytes(img)
        if not style.exists():
            style.write_bytes(img)
        total += cost or 0
        print(f'{n}: {path} ({len(img)//1024} KB, ${cost or 0:.3f}, {time.time()-t:.0f}s)')
    if not a.cut_only:
        print(f'total ${total:.3f}')
    for n in names:
        if (a.raw / f'{n}.png').exists():
            cut(a.raw / f'{n}.png', a.out / f'{n}.png', a.width)
            print(f'{n}: {a.out / (n + ".png")}')


if __name__ == '__main__':
    main()
