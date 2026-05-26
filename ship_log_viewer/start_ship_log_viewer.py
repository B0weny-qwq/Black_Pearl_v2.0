#!/usr/bin/env python3
"""
One-click launcher for the local ship log viewer.

It starts a local HTTP server from the repository root and opens the
serial log viewer page in the default browser.
"""

from __future__ import annotations

import contextlib
import http.server
import os
import socket
import socketserver
import sys
import threading
import time
import webbrowser
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_PORT = 8000
MAX_PORT_TRIES = 30
VIEWER_PATH = "ship_log_viewer/ship_log_viewer.html"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    """Serve files quietly without per-request console spam."""

    def log_message(self, format: str, *args) -> None:  # noqa: A003
        return


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def pick_port(start_port: int, max_tries: int) -> int:
    for port in range(start_port, start_port + max_tries):
        with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if sock.connect_ex(("127.0.0.1", port)) != 0:
                return port
    raise RuntimeError("No free port found")


def build_server(port: int) -> ThreadedTCPServer:
    handler = lambda *args, **kwargs: QuietHandler(*args, directory=str(ROOT_DIR), **kwargs)
    return ThreadedTCPServer(("127.0.0.1", port), handler)


def main() -> int:
    os.chdir(ROOT_DIR)

    try:
        port = pick_port(DEFAULT_PORT, MAX_PORT_TRIES)
    except RuntimeError as exc:
        print(f"[viewer] Start failed: {exc}")
        return 1

    try:
        server = build_server(port)
    except OSError as exc:
        print(f"[viewer] Server create failed: {exc}")
        return 1

    url = f"http://127.0.0.1:{port}/{VIEWER_PATH}"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    print("[viewer] Black Pearl serial log viewer is running")
    print(f"[viewer] Root: {ROOT_DIR}")
    print(f"[viewer] URL : {url}")
    print("[viewer] Press Ctrl+C to stop")

    time.sleep(0.4)
    webbrowser.open(url)

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\n[viewer] Shutting down...")
    finally:
        server.shutdown()
        server.server_close()
        print("[viewer] Stopped")

    return 0


if __name__ == "__main__":
    sys.exit(main())
