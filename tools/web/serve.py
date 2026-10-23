#!/usr/bin/env python3
"""Serve Executor's tab (or any static directory) with pre-compressed files.

    tools/web/serve.py DIR [--port 8090] [--bind 0.0.0.0]

For a request for NAME, when NAME.gz exists and the browser accepts gzip,
NAME.gz goes out with Content-Encoding: gzip and NAME's type, and
X-Decoded-Length says NAME's size (the page's download progress). Every
file has an ETag (NAME's size and time) and Last-Modified; a matching
If-None-Match gets 304, so the browser's cache and the page's disk cache
(web/executor/main.js) revalidate instead of downloading again. The
module (38 MB) and a boot disk (16 MB) compress to 3 and 6 MB. Production
hosts do the same with their own settings (nginx gzip_static, a CDN).
make_demo.sh writes the .gz files.
"""
import argparse
import functools
import http.server
import mimetypes
import os

mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("text/javascript", ".js")


class Handler(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            return super().send_head()            # directories, 404s
        st = os.stat(path)
        etag = f'"{st.st_size:x}-{int(st.st_mtime):x}"'
        validators = [("ETag", etag), ("Last-Modified", self.date_time_string(st.st_mtime))]
        if etag in [t.strip() for t in self.headers.get("If-None-Match", "").split(",")]:
            self.send_response(304)
            for k, v in validators:
                self.send_header(k, v)
            self.end_headers()
            return None
        gz = path + ".gz"
        gzipped = os.path.isfile(gz) and "gzip" in self.headers.get("Accept-Encoding", "")
        f = open(gz if gzipped else path, "rb")
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(path))
        if gzipped:
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(os.fstat(f.fileno()).st_size))
        self.send_header("X-Decoded-Length", str(st.st_size))
        for k, v in validators:
            self.send_header(k, v)
        self.end_headers()
        return f

    def end_headers(self):
        # Revalidate every time (304s are cheap); the page's disk cache
        # needs the validators.
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Access-Control-Expose-Headers", "X-Decoded-Length, ETag, Last-Modified")
        super().end_headers()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dir")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--bind", default="0.0.0.0")
    args = ap.parse_args()
    handler = functools.partial(Handler, directory=args.dir)
    with http.server.ThreadingHTTPServer((args.bind, args.port), handler) as server:
        print(f"Serving {args.dir} on http://{args.bind}:{args.port}/", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
