#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
#
# Serve the built web host so a browser will actually run it.
#
#     python3 scripts/serve-web.py [--config Debug] [--port 8000]
#
# `python3 -m http.server -d <dir>` looks like it should be enough, and on
# Linux and macOS it is. On Windows it is not: the standard library reads
# MIME types from the registry, where .mjs is commonly registered as
# text/plain. Browsers apply a strict MIME check to module scripts and
# refuse to execute one served that way, so index.html's import of
# host.mjs fails before any of its own error handling can run and the page
# sits on "loading…". This sets the types the page needs, whatever the
# machine thinks, and is therefore the one serve command that behaves the
# same on all three desktop platforms.

import argparse
import http.server
import pathlib
import sys

# Wins over anything the platform believes: SimpleHTTPRequestHandler
# consults extensions_map before falling back to the mimetypes database.
TYPES = {
    ".mjs": "text/javascript",
    ".js": "text/javascript",
    ".wasm": "application/wasm",
    ".html": "text/html",
}


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="Debug",
                        help="build configuration to serve (default: Debug)")
    parser.add_argument("--port", type=int, default=8000,
                        help="port to listen on (default: 8000)")
    parser.add_argument("--build-dir", default=None,
                        help="build tree to serve from "
                             "(default: build/wasm)")
    args = parser.parse_args()

    build = pathlib.Path(args.build_dir) if args.build_dir \
        else root / "build" / "wasm"
    directory = build / "hosts" / "web" / args.config

    # Loud and specific rather than an empty directory listing: "no such
    # file" in a browser is a much worse clue than the build command.
    if not (directory / "index.html").is_file():
        print(f"serve-web: nothing built in {directory}", file=sys.stderr)
        print("  cmake --preset wasm", file=sys.stderr)
        print("  cmake --build --preset wasm", file=sys.stderr)
        return 1

    handler = http.server.SimpleHTTPRequestHandler
    handler.extensions_map = {**handler.extensions_map, **TYPES}

    class Handler(handler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(directory), **kw)

        # Never answer "304 Not Modified". A browser that cached host.mjs
        # from a server that typed it text/plain will revalidate rather
        # than refetch, and a 304 tells it to keep using the copy it has —
        # wrong content type and all. Since the file itself has not
        # changed, that survives every reload and every server fix, which
        # makes it the single most confusing failure this page has. Drop
        # the conditional and always send the body.
        def send_head(self):
            del self.headers["If-Modified-Since"]
            del self.headers["If-None-Match"]
            return super().send_head()

        # And do not let it be cached again.
        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

    with http.server.ThreadingHTTPServer(("127.0.0.1", args.port),
                                         Handler) as httpd:
        print(f"serving {directory}")
        print(f"  http://localhost:{args.port}/   (ctrl-c to stop)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
