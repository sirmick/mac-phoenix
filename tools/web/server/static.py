"""Files over HTTP: validators, pre-compressed copies, byte ranges.

For a request for NAME, when NAME.gz exists and the browser accepts gzip,
NAME.gz goes out with Content-Encoding: gzip and NAME's type, and
X-Decoded-Length says NAME's size (the page's download progress), and
Vary: Accept-Encoding. Every file has an ETag (its size and time in ns,
or the caller's: a disk's content, library.py; "-gz" on the compressed
copy's) and Last-Modified; a matching If-None-Match gets 304. A Range request (one range; If-Range, when sent,
must match) gets those bytes of NAME itself, never of NAME.gz: the tab
reads large disk images that way, block by block (overlay.js).
"""
import mimetypes
import os
import re

mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("text/javascript", ".js")

COPY_CHUNK = 1 << 20


def guess_type(path):
    return mimetypes.guess_type(path)[0] or "application/octet-stream"


def etag_of(st):
    return f'{st.st_size:x}-{st.st_mtime_ns:x}'


def parse_range(header, size):
    """(start, end) half-open for one 'bytes=' range; None to send it all;
    'unsatisfiable' for a range outside the file."""
    m = re.fullmatch(r"\s*bytes\s*=\s*(\d*)\s*-\s*(\d*)\s*", header or "")
    if not m or (not m.group(1) and not m.group(2)):
        return None                         # several ranges, or nonsense: all of it
    first, last = m.group(1), m.group(2)
    if not first:                           # the last N bytes
        n = int(last)
        if n == 0:
            return "unsatisfiable"
        return max(0, size - n), size
    start = int(first)
    if last and int(last) < start:
        return None                         # not a range: ignored
    if start >= size:
        return "unsatisfiable"
    return start, min(size, int(last) + 1) if last else size


def send_file(h, path, head=False, tag=None, headers=()):
    """Answers h (a BaseHTTPRequestHandler) with the file at path; tag: its
    ETag's value (default: its size and time); headers: more to send."""
    st = os.stat(path)
    gz = path + ".gz"
    has_gz = os.path.isfile(gz)
    rng = None
    if h.headers.get("Range"):
        rng = parse_range(h.headers["Range"], st.st_size)
    gzipped = rng is None and has_gz and "gzip" in h.headers.get("Accept-Encoding", "")
    tag = tag or etag_of(st)
    etag = f'"{tag}-gz"' if gzipped else f'"{tag}"'
    validators = [("ETag", etag), ("Last-Modified", h.date_time_string(st.st_mtime)),
                  ("Accept-Ranges", "bytes"), *headers]
    if has_gz:
        validators.append(("Vary", "Accept-Encoding"))
    if rng is not None and h.headers.get("If-Range") and h.headers["If-Range"].strip() != f'"{tag}"':
        rng = None                          # another version: all of it
    if etag in [t.strip() for t in h.headers.get("If-None-Match", "").split(",")]:
        h.send_response(304)
        for k, v in validators:
            h.send_header(k, v)
        h.send_header("Content-Length", "0")
        h.end_headers()
        return
    if rng == "unsatisfiable":
        h.send_response(416)
        h.send_header("Content-Range", f"bytes */{st.st_size}")
        h.send_header("Content-Length", "0")
        for k, v in validators:
            h.send_header(k, v)
        h.end_headers()
        return
    with open(gz if gzipped else path, "rb") as f:
        length = os.fstat(f.fileno()).st_size
        start, end = rng or (0, length)
        h.send_response(206 if rng else 200)
        h.send_header("Content-Type", guess_type(path))
        if gzipped:
            h.send_header("Content-Encoding", "gzip")
        if rng:
            h.send_header("Content-Range", f"bytes {start}-{end - 1}/{st.st_size}")
        h.send_header("Content-Length", str(end - start))
        h.send_header("X-Decoded-Length", str(st.st_size))
        for k, v in validators:
            h.send_header(k, v)
        h.end_headers()
        if head:
            return
        f.seek(start)
        left = end - start
        while left > 0:
            chunk = f.read(min(COPY_CHUNK, left))
            if not chunk:
                break
            h.wfile.write(chunk)
            left -= len(chunk)
