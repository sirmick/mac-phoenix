"""Files over HTTP: validators, pre-compressed copies, byte ranges.

For a request for NAME, when NAME.gz exists and the browser accepts gzip,
NAME.gz goes out with Content-Encoding: gzip and NAME's type, and
X-Decoded-Length says NAME's size (the page's download progress). Every
file has an ETag (its size and time) and Last-Modified; a matching
If-None-Match gets 304. A Range request (one range; If-Range, when sent,
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
    return f'"{st.st_size:x}-{int(st.st_mtime):x}"'


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
    end = min(size, int(last) + 1) if last else size
    if start >= size or end <= start:
        return "unsatisfiable"
    return start, end


def send_file(h, path, head=False):
    """Answers h (a BaseHTTPRequestHandler) with the file at path."""
    st = os.stat(path)
    etag = etag_of(st)
    validators = [("ETag", etag), ("Last-Modified", h.date_time_string(st.st_mtime)),
                  ("Accept-Ranges", "bytes")]
    if etag in [t.strip() for t in h.headers.get("If-None-Match", "").split(",")]:
        h.send_response(304)
        for k, v in validators:
            h.send_header(k, v)
        h.send_header("Content-Length", "0")
        h.end_headers()
        return
    rng = None
    if h.headers.get("Range"):
        if_range = h.headers.get("If-Range")
        if not if_range or if_range.strip() == etag:
            rng = parse_range(h.headers["Range"], st.st_size)
    if rng == "unsatisfiable":
        h.send_response(416)
        h.send_header("Content-Range", f"bytes */{st.st_size}")
        h.send_header("Content-Length", "0")
        for k, v in validators:
            h.send_header(k, v)
        h.end_headers()
        return
    gz = path + ".gz"
    gzipped = (rng is None and os.path.isfile(gz)
               and "gzip" in h.headers.get("Accept-Encoding", ""))
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
