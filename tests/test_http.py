#!/usr/bin/env python3

import http.server
import os
import shutil
import socketserver
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path


BODY = b'{"amiapi":"m1","ok":true}\n'
HTTPGET = "./build/httpget"


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


class TestServer(socketserver.TCPServer):
    allow_reuse_address = True


def run_httpget(url, env=None):
    return subprocess.run(
        [HTTPGET, url],
        text=True,
        capture_output=True,
        env=env,
        timeout=15,
    )


def assert_success(result, label):
    if result.returncode != 0:
        raise AssertionError(
            f"{label} unexpectedly failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert "HTTP 200" in result.stdout, result.stdout
    assert "content-type: application/json" in result.stdout, result.stdout
    assert f"body-bytes: {len(BODY)}" in result.stdout, result.stdout


def make_certificates(directory):
    if shutil.which("openssl") is None:
        raise SystemExit(
            "openssl CLI is required for the local HTTPS integration test"
        )

    directory = Path(directory)

    ca_key = directory / "ca.key"
    ca_cert = directory / "ca.pem"
    server_key = directory / "server.key"
    server_csr = directory / "server.csr"
    server_cert = directory / "server.pem"
    ext_file = directory / "server.ext"

    def openssl(*args):
        subprocess.run(
            ["openssl", *args],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    openssl(
        "req",
        "-x509",
        "-newkey", "rsa:2048",
        "-nodes",
        "-keyout", str(ca_key),
        "-out", str(ca_cert),
        "-days", "1",
        "-sha256",
        "-subj", "/CN=AmiAPI Test CA",
    )

    openssl(
        "req",
        "-newkey", "rsa:2048",
        "-nodes",
        "-keyout", str(server_key),
        "-out", str(server_csr),
        "-sha256",
        "-subj", "/CN=localhost",
    )

    ext_file.write_text(
        "basicConstraints=CA:FALSE\n"
        "keyUsage=digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost\n"
    )

    openssl(
        "x509",
        "-req",
        "-in", str(server_csr),
        "-CA", str(ca_cert),
        "-CAkey", str(ca_key),
        "-CAcreateserial",
        "-out", str(server_cert),
        "-days", "1",
        "-sha256",
        "-extfile", str(ext_file),
    )

    return ca_cert, server_cert, server_key


def test_http():
    with TestServer(("127.0.0.1", 0), Handler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = run_httpget(
                f"http://127.0.0.1:{port}/hello"
            )
        finally:
            server.shutdown()
            thread.join()

    assert_success(result, "HTTP")
    print("HTTP integration test: OK")


def test_https():
    with tempfile.TemporaryDirectory(prefix="amiapi-https-") as tmp:
        ca_cert, server_cert, server_key = make_certificates(tmp)

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(
            certfile=server_cert,
            keyfile=server_key,
        )

        with TestServer(("127.0.0.1", 0), Handler) as server:
            server.socket = context.wrap_socket(
                server.socket,
                server_side=True,
            )

            port = server.server_address[1]

            thread = threading.Thread(
                target=server.serve_forever,
                daemon=True,
            )
            thread.start()

            env = os.environ.copy()
            env["SSL_CERT_FILE"] = str(ca_cert)

            try:
                good = run_httpget(
                    f"https://localhost:{port}/hello",
                    env=env,
                )

                wrong_host = run_httpget(
                    f"https://127.0.0.1:{port}/hello",
                    env=env,
                )
            finally:
                server.shutdown()
                thread.join()

        assert_success(good, "HTTPS")

        if wrong_host.returncode == 0:
            raise AssertionError(
                "HTTPS hostname verification failed: "
                "certificate for localhost was accepted for 127.0.0.1\n"
                f"stdout:\n{wrong_host.stdout}\n"
                f"stderr:\n{wrong_host.stderr}"
            )

        print("HTTPS integration test: OK")
        print("HTTPS hostname verification test: OK")


class ShortBodyHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = b"short\n"

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body) + 10))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True

    def log_message(self, fmt, *args):
        pass


class LongBodyHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = b"body-is-longer-than-advertised\n"

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "4")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True

    def log_message(self, fmt, *args):
        pass


def run_failure_server(handler, label):
    with TestServer(("127.0.0.1", 0), handler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = run_httpget(
                f"http://127.0.0.1:{port}/hello"
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode == 0:
        raise AssertionError(
            f"{label} unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )


def test_content_length_framing():
    # If the peer closes before the advertised Content-Length has been
    # received, the response is truncated and must fail.
    run_failure_server(
        ShortBodyHandler,
        "truncated Content-Length response",
    )

    # Content-Length is authoritative.  Bytes sent after the advertised
    # body length are not part of this HTTP response body, so AmiAPI must
    # stop after exactly four bytes rather than waiting for EOF.
    with TestServer(("127.0.0.1", 0), LongBodyHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = run_httpget(
                f"http://127.0.0.1:{port}/hello"
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "Content-Length bounded response unexpectedly failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert "HTTP 200" in result.stdout, result.stdout
    assert "body-bytes: 4" in result.stdout, result.stdout

    print("Content-Length framing tests: OK")


class ChunkedHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()

        self.wfile.write(
            b"4;amiapi=test\r\n"
            b"Wiki\r\n"
            b"5\r\n"
            b"pedia\r\n"
            b"A\r\n"
            b"-chunked!!\r\n"
            b"0\r\n"
            b"X-AmiAPI-Trailer: yes\r\n"
            b"\r\n"
        )
        self.wfile.flush()

    def log_message(self, fmt, *args):
        pass


class BadChunkSizeHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()

        self.wfile.write(
            b"ZZ\r\n"
            b"broken\r\n"
            b"0\r\n\r\n"
        )
        self.wfile.flush()
        self.close_connection = True

    def log_message(self, fmt, *args):
        pass


class TruncatedChunkHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()

        self.wfile.write(
            b"10\r\n"
            b"short"
        )
        self.wfile.flush()
        self.close_connection = True

    def log_message(self, fmt, *args):
        pass


def test_chunked_transfer():
    with TestServer(("127.0.0.1", 0), ChunkedHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = run_httpget(
                f"http://127.0.0.1:{port}/hello"
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "chunked response unexpectedly failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    # 4 + 5 + 10 decoded body bytes.
    assert "HTTP 200" in result.stdout, result.stdout
    assert "body-bytes: 19" in result.stdout, result.stdout

    run_failure_server(
        BadChunkSizeHandler,
        "malformed chunk size",
    )

    run_failure_server(
        TruncatedChunkHandler,
        "truncated chunk",
    )

    print("Chunked transfer tests: OK")


def test_https_chunked():
    with tempfile.TemporaryDirectory(prefix="amiapi-https-chunked-") as tmp:
        ca_cert, server_cert, server_key = make_certificates(tmp)

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(
            certfile=server_cert,
            keyfile=server_key,
        )

        with TestServer(("127.0.0.1", 0), ChunkedHandler) as server:
            server.socket = context.wrap_socket(
                server.socket,
                server_side=True,
            )

            port = server.server_address[1]

            thread = threading.Thread(
                target=server.serve_forever,
                daemon=True,
            )
            thread.start()

            env = os.environ.copy()
            env["SSL_CERT_FILE"] = str(ca_cert)

            try:
                result = run_httpget(
                    f"https://localhost:{port}/hello",
                    env=env,
                )
            finally:
                server.shutdown()
                thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "HTTPS chunked response unexpectedly failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert "HTTP 200" in result.stdout, result.stdout
    assert "body-bytes: 19" in result.stdout, result.stdout

    print("HTTPS chunked transfer test: OK")


class RedirectHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def send_redirect(self, status, location):
        self.send_response(status)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path == "/start":
            self.send_redirect(302, "/step2")
            return

        if self.path == "/step2":
            self.send_redirect(307, "final")
            return

        if self.path == "/a/b/start":
            self.send_redirect(302, "../final")
            return

        if self.path == "/a/final":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "2")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"ok")
            return

        if self.path == "/dot/start":
            self.send_redirect(302, "./next")
            return

        if self.path == "/dot/next":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "2")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"ok")
            return

        if self.path == "/query/start?old=1":
            self.send_redirect(302, "?page=2")
            return

        if self.path == "/query/start?page=2":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "2")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"ok")
            return

        if self.path == "/fragment/start":
            self.send_redirect(302, "/final#client-only")
            return

        if self.path == "/final":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "8")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"redirect")
            return

        if self.path.startswith("/loop"):
            self.send_redirect(302, "/loop")
            return

        if self.path == "/missing":
            self.send_response(302)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return

        self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def test_redirects():
    with TestServer(("127.0.0.1", 0), RedirectHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = run_httpget(
                f"http://127.0.0.1:{port}/start"
            )

            loop = run_httpget(
                f"http://127.0.0.1:{port}/loop"
            )

            missing = run_httpget(
                f"http://127.0.0.1:{port}/missing"
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "redirect chain unexpectedly failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert "HTTP 200" in result.stdout, result.stdout
    assert "body-bytes: 8" in result.stdout, result.stdout

    if loop.returncode == 0:
        raise AssertionError(
            "redirect loop unexpectedly succeeded"
        )

    if missing.returncode == 0:
        raise AssertionError(
            "redirect without Location unexpectedly succeeded"
        )

    # RFC-style relative redirect resolution.
    cases = [
        "/a/b/start",
        "/dot/start",
        "/query/start?old=1",
        "/fragment/start",
    ]

    for path in cases:
        with TestServer(("127.0.0.1", 0), RedirectHandler) as extra_server:
            extra_port = extra_server.server_address[1]

            extra_thread = threading.Thread(
                target=extra_server.serve_forever,
                daemon=True,
            )
            extra_thread.start()

            try:
                extra = run_httpget(
                    f"http://127.0.0.1:{extra_port}{path}"
                )
            finally:
                extra_server.shutdown()
                extra_thread.join()

        if extra.returncode != 0:
            raise AssertionError(
                f"redirect resolution failed for {path}\n"
                f"stdout:\n{extra.stdout}\n"
                f"stderr:\n{extra.stderr}"
            )

        assert "HTTP 200" in extra.stdout, extra.stdout

    print("Redirect tests: OK")


def test_http_to_https_redirect():
    with tempfile.TemporaryDirectory(prefix="amiapi-redirect-") as tmp:
        ca_cert, server_cert, server_key = make_certificates(tmp)

        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(
            certfile=server_cert,
            keyfile=server_key,
        )

        with TestServer(("127.0.0.1", 0), Handler) as https_server:
            https_server.socket = tls_context.wrap_socket(
                https_server.socket,
                server_side=True,
            )

            https_port = https_server.server_address[1]

            https_thread = threading.Thread(
                target=https_server.serve_forever,
                daemon=True,
            )
            https_thread.start()

            class UpgradeHandler(http.server.BaseHTTPRequestHandler):
                protocol_version = "HTTP/1.1"

                def do_GET(self):
                    location = (
                        f"https://localhost:{https_port}/hello"
                    )

                    self.send_response(302)
                    self.send_header("Location", location)
                    self.send_header("Content-Length", "0")
                    self.send_header("Connection", "close")
                    self.end_headers()

                def log_message(self, fmt, *args):
                    pass

            with TestServer(("127.0.0.1", 0), UpgradeHandler) as http_server:
                http_port = http_server.server_address[1]

                http_thread = threading.Thread(
                    target=http_server.serve_forever,
                    daemon=True,
                )
                http_thread.start()

                env = os.environ.copy()
                env["SSL_CERT_FILE"] = str(ca_cert)

                try:
                    result = run_httpget(
                        f"http://127.0.0.1:{http_port}/start",
                        env=env,
                    )
                finally:
                    http_server.shutdown()
                    http_thread.join()
                    https_server.shutdown()
                    https_thread.join()

    assert_success(result, "HTTP to HTTPS redirect")
    print("HTTP to HTTPS redirect test: OK")


class RequestHeaderHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    seen = []

    def do_GET(self):
        type(self).seen.append({
            "path": self.path,
            "authorization": self.headers.get("Authorization"),
            "accept": self.headers.get("Accept"),
            "user_agent": self.headers.get("User-Agent"),
            "cookie": self.headers.get("Cookie"),
        })

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_request_headers():
    RequestHeaderHandler.seen = []

    with TestServer(("127.0.0.1", 0), RequestHeaderHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/requestget",
                    f"http://127.0.0.1:{port}/headers",
                    "Authorization", "Bearer secret-token",
                    "Accept", "application/json",
                    "User-Agent", "AmiAPI-Test/1",
                    "X-AmiAPI-Test", "yes",
                ],
                text=True,
                capture_output=True,
                timeout=15,
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "custom request headers failed\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert len(RequestHeaderHandler.seen) == 1

    seen = RequestHeaderHandler.seen[0]

    assert seen["authorization"] == "Bearer secret-token"
    assert seen["accept"] == "application/json"
    assert seen["user_agent"] == "AmiAPI-Test/1"

    print("Custom request header test: OK")


def test_reserved_headers():
    cases = [
        ("Host", "example.invalid"),
        ("Connection", "keep-alive"),
        ("Content-Length", "123"),
        ("Transfer-Encoding", "chunked"),
    ]

    for name, value in cases:
        result = subprocess.run(
            [
                "./build/requestget",
                "http://127.0.0.1:1/",
                name,
                value,
            ],
            text=True,
            capture_output=True,
            timeout=5,
        )

        if result.returncode != 5:
            raise AssertionError(
                f"reserved header {name} was not rejected\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    print("Reserved request header tests: OK")


def test_header_injection():
    result = subprocess.run(
        [
            "./build/requestget",
            "http://127.0.0.1:1/",
            "X-Test",
            "good\r\nInjected: bad",
        ],
        text=True,
        capture_output=True,
        timeout=5,
    )

    if result.returncode != 5:
        raise AssertionError(
            "CR/LF request-header injection was not rejected"
        )

    print("Request header injection test: OK")


class SameOriginAuthHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_GET(self):
        type(self).seen.append(
            (self.path, self.headers.get("Authorization"))
        )

        if self.path == "/start":
            self.send_response(302)
            self.send_header("Location", "/final")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return

        if self.path == "/final":
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return

        self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def test_sensitive_header_same_origin_redirect():
    SameOriginAuthHandler.seen = []

    with TestServer(("127.0.0.1", 0), SameOriginAuthHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/requestget",
                    f"http://127.0.0.1:{port}/start",
                    "Authorization",
                    "Bearer same-origin-secret",
                ],
                text=True,
                capture_output=True,
                timeout=15,
            )
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "same-origin authorization redirect failed\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert SameOriginAuthHandler.seen == [
        ("/start", "Bearer same-origin-secret"),
        ("/final", "Bearer same-origin-secret"),
    ], SameOriginAuthHandler.seen

    print("Same-origin sensitive-header redirect test: OK")


class CrossOriginTargetHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_GET(self):
        type(self).seen.append(
            (self.path, self.headers.get("Authorization"))
        )

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_sensitive_header_cross_origin_redirect():
    CrossOriginTargetHandler.seen = []

    with TestServer(("127.0.0.1", 0), CrossOriginTargetHandler) as target:
        target_port = target.server_address[1]

        target_thread = threading.Thread(
            target=target.serve_forever,
            daemon=True,
        )
        target_thread.start()

        class SourceHandler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"
            seen = []

            def do_GET(self):
                type(self).seen.append(
                    (self.path, self.headers.get("Authorization"))
                )

                self.send_response(302)
                self.send_header(
                    "Location",
                    f"http://localhost:{target_port}/target",
                )
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()

            def log_message(self, fmt, *args):
                pass

        SourceHandler.seen = []

        with TestServer(("127.0.0.1", 0), SourceHandler) as source:
            source_port = source.server_address[1]

            source_thread = threading.Thread(
                target=source.serve_forever,
                daemon=True,
            )
            source_thread.start()

            try:
                result = subprocess.run(
                    [
                        "./build/requestget",
                        f"http://127.0.0.1:{source_port}/start",
                        "Authorization",
                        "Bearer cross-origin-secret",
                    ],
                    text=True,
                    capture_output=True,
                    timeout=15,
                )
            finally:
                source.shutdown()
                source_thread.join()
                target.shutdown()
                target_thread.join()

    if result.returncode != 0:
        raise AssertionError(
            "cross-origin authorization redirect failed\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert SourceHandler.seen == [
        ("/start", "Bearer cross-origin-secret"),
    ], SourceHandler.seen

    assert CrossOriginTargetHandler.seen == [
        ("/target", None),
    ], CrossOriginTargetHandler.seen

    print("Cross-origin sensitive-header stripping test: OK")


def main():
    test_request_headers()
    test_sensitive_header_same_origin_redirect()
    test_sensitive_header_cross_origin_redirect()
    test_reserved_headers()
    test_header_injection()
    test_http()
    test_https()
    test_content_length_framing()
    test_chunked_transfer()
    test_https_chunked()
    test_redirects()
    test_http_to_https_redirect()


if __name__ == "__main__":
    main()
