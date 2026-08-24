#!/usr/bin/env python3
import http.server
import socketserver
import subprocess
import threading

BODY = b'{"amiapi":"m1","ok":true}\n'

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path != "/hello":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, fmt, *args):
        pass

with socketserver.TCPServer(("127.0.0.1", 0), Handler) as server:
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    result = subprocess.run(
        ["./build/httpget", f"http://127.0.0.1:{port}/hello"],
        text=True, capture_output=True
    )
    server.shutdown()

if result.returncode != 0:
    raise SystemExit(result.stderr or result.stdout)

assert "HTTP 200" in result.stdout, result.stdout
assert "content-type: application/json" in result.stdout, result.stdout
assert f"body-bytes: {len(BODY)}" in result.stdout, result.stdout
print("HTTP integration test: OK")
