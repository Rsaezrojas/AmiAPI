#!/usr/bin/env python3

import http.server
import os
import shutil
import socketserver
import ssl
import subprocess
import tempfile
import threading
import time
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


STREAM_BODY = b"stream\x00binary\xffbody"
LARGE_STREAM_BODY = bytes(range(256)) * 40


class StreamingHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", "99")
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path == "/length":
            body = STREAM_BODY
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("X-Stream-Test", "length")
            self.send_header("X-Duplicate", "first")
            self.send_header("X-Duplicate", "second")
            self.send_header("X-Final", "yes")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/large":
            self.send_response(200)
            self.send_header("Content-Length", str(len(LARGE_STREAM_BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(LARGE_STREAM_BODY)
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for part in (STREAM_BODY[:4], STREAM_BODY[4:11], STREAM_BODY[11:]):
                self.wfile.write(f"{len(part):x};test=yes\r\n".encode())
                self.wfile.write(part + b"\r\n")
                self.wfile.flush()
            self.wfile.write(b"0\r\nX-Trailer: hidden\r\n\r\n")
        elif self.path == "/eof":
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(STREAM_BODY)
        elif self.path == "/redirect":
            body = b"NEVER"
            self.send_response(302)
            self.send_header("Location", "/length")
            self.send_header("X-Intermediate", "must-not-survive")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def run_streamget(method, url, max_body=None, fail_call=None, env=None):
    command = ["./build/streamget", method, url]
    if max_body is not None:
        command.append(str(max_body))
    if fail_call is not None:
        command.append(str(fail_call))
    return subprocess.run(
        command, text=True, capture_output=True, timeout=15, env=env
    )


def run_progressget(method, mode, url, cancel_call=0, env=None):
    return subprocess.run(
        ["./build/progressget", method, mode, url, str(cancel_call)],
        text=True, capture_output=True, timeout=15, env=env,
    )


def assert_stream(result, body, label, multiple=False):
    if result.returncode != 0:
        raise AssertionError(
            f"{label} unexpectedly failed\nstdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    assert result.stdout.startswith(body.hex() + "\n"), result.stdout
    assert f"callback-bytes: {len(body)}" in result.stdout, result.stdout
    assert "response-body-null: 1" in result.stdout, result.stdout
    assert "response-body-size: 0" in result.stdout, result.stdout
    if multiple:
        calls = int(result.stdout.split("callback-calls: ", 1)[1].splitlines()[0])
        assert calls > 1, result.stdout


def test_streaming_responses():
    with TestServer(("127.0.0.1", 0), StreamingHandler) as server:
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{port}"
        try:
            assert_stream(run_streamget("GET", base + "/length"), STREAM_BODY,
                          "Content-Length stream")
            length = run_streamget("GET", base + "/length")
            assert "HTTP 200" in length.stdout, length.stdout
            assert "x-stream-test: length" in length.stdout, length.stdout
            assert_stream(run_streamget("GET", base + "/chunked"), STREAM_BODY,
                          "chunked stream")
            assert_stream(run_streamget("GET", base + "/eof"), STREAM_BODY,
                          "EOF stream")
            assert_stream(run_streamget("GET", base + "/large"), LARGE_STREAM_BODY,
                          "large stream", multiple=True)
            assert_stream(run_streamget("GET", base + "/redirect"), STREAM_BODY,
                          "redirect stream")

            head = run_streamget("HEAD", base + "/length")
            assert_stream(head, b"", "HEAD stream")
            assert "callback-calls: 0" in head.stdout, head.stdout

            failed = run_streamget("GET", base + "/length",
                                   AMIAPI_DEFAULT_MAX_BODY_PY, 1)
            assert failed.returncode == 20, failed.stdout + failed.stderr
            assert "error: 3" in failed.stderr, failed.stderr

            oversized = run_streamget("GET", base + "/length", 4)
            assert oversized.returncode == 20, oversized.stdout + oversized.stderr
            assert "error: 11" in oversized.stderr, oversized.stderr
        finally:
            server.shutdown()
            thread.join()

    print("Streaming response tests: OK")


def assert_progress(result, received, total, label):
    if result.returncode != 0:
        raise AssertionError(
            f"{label} unexpectedly failed\nstdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    assert f"received: {received}" in result.stdout, result.stdout
    assert f"total: {total}" in result.stdout, result.stdout


def test_progress_and_response_headers():
    with TestServer(("127.0.0.1", 0), StreamingHandler) as server:
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{port}"
        try:
            length = run_progressget("GET", "buffer", base + "/length")
            assert_progress(length, len(STREAM_BODY), len(STREAM_BODY),
                            "buffered Content-Length progress")
            assert f"body-size: {len(STREAM_BODY)}" in length.stdout
            assert "Content-Type: application/octet-stream" in length.stdout
            assert "content-type: application/octet-stream" in length.stdout
            assert "X-Duplicate: first" in length.stdout
            assert "X-Missing: (null)" in length.stdout
            assert "invalid-header-args: 1" in length.stdout

            large = run_progressget("GET", "buffer", base + "/large")
            assert_progress(large, len(LARGE_STREAM_BODY),
                            len(LARGE_STREAM_BODY), "large progress")
            calls = int(large.stdout.split("progress-calls: ", 1)[1].splitlines()[0])
            assert calls > 1, large.stdout

            streamed = run_progressget("GET", "stream", base + "/length")
            assert_progress(streamed, len(STREAM_BODY), len(STREAM_BODY),
                            "streamed progress")
            assert f"streamed: {len(STREAM_BODY)}" in streamed.stdout
            assert "body-size: 0" in streamed.stdout

            failed_body = run_progressget(
                "GET", "failstream", base + "/length"
            )
            assert failed_body.returncode == 20, failed_body.stdout
            assert "last-error: 3" in failed_body.stdout, failed_body.stdout
            assert "progress-calls: 0" in failed_body.stdout, failed_body.stdout

            chunked = run_progressget("GET", "buffer", base + "/chunked")
            assert_progress(chunked, len(STREAM_BODY), 0, "chunked progress")
            eof = run_progressget("GET", "buffer", base + "/eof")
            assert_progress(eof, len(STREAM_BODY), 0, "EOF progress")

            redirected = run_progressget("GET", "buffer", base + "/redirect")
            assert_progress(redirected, len(STREAM_BODY), len(STREAM_BODY),
                            "redirect progress")
            assert "X-Final: yes" in redirected.stdout
            assert "X-Intermediate: (null)" in redirected.stdout

            head = run_progressget("HEAD", "buffer", base + "/length")
            assert_progress(head, 0, 0, "HEAD progress")
            assert "progress-calls: 0" in head.stdout

            for mode in ("buffer", "stream"):
                cancelled = run_progressget(
                    "GET", mode, base + "/large", cancel_call=1
                )
                assert cancelled.returncode == 20, cancelled.stdout
                assert "last-error: 15" in cancelled.stdout, cancelled.stdout
                assert "progress-calls: 1" in cancelled.stdout, cancelled.stdout
        finally:
            server.shutdown()
            thread.join()

    print("Progress, cancellation, and response header tests: OK")


AMIAPI_DEFAULT_MAX_BODY_PY = 1024 * 1024


def test_https_streaming():
    with tempfile.TemporaryDirectory(prefix="amiapi-https-stream-") as tmp:
        ca_cert, server_cert, server_key = make_certificates(tmp)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile=server_cert, keyfile=server_key)
        with TestServer(("127.0.0.1", 0), StreamingHandler) as server:
            server.socket = context.wrap_socket(server.socket, server_side=True)
            port = server.server_address[1]
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            env = os.environ.copy()
            env["SSL_CERT_FILE"] = str(ca_cert)
            try:
                result = run_streamget(
                    "GET", f"https://localhost:{port}/chunked", env=env
                )
                progress = run_progressget(
                    "GET", "buffer", f"https://localhost:{port}/length",
                    env=env,
                )
            finally:
                server.shutdown()
                thread.join()
    assert_stream(result, STREAM_BODY, "HTTPS stream")
    assert_progress(progress, len(STREAM_BODY), len(STREAM_BODY),
                    "HTTPS progress")
    assert "Content-Type: application/octet-stream" in progress.stdout
    print("HTTPS streaming response test: OK")


DOWNLOAD_BODY = bytes(range(256)) + b"download\x00\xff"
LARGE_DOWNLOAD_BODY = bytes(range(256)) * 2048


class DownloadHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", str(len(DOWNLOAD_BODY)))
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path in ("/length", "/final"):
            body = DOWNLOAD_BODY
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/large":
            self.send_response(200)
            self.send_header("Content-Length", str(len(LARGE_DOWNLOAD_BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                for offset in range(0, len(LARGE_DOWNLOAD_BODY), 4096):
                    self.wfile.write(LARGE_DOWNLOAD_BODY[offset:offset + 4096])
            except (BrokenPipeError, ConnectionResetError):
                pass
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for part in (DOWNLOAD_BODY[:17], DOWNLOAD_BODY[17:200],
                         DOWNLOAD_BODY[200:]):
                self.wfile.write(f"{len(part):x}\r\n".encode())
                self.wfile.write(part + b"\r\n")
            self.wfile.write(b"0\r\nIgnored: trailer\r\n\r\n")
        elif self.path == "/eof":
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(DOWNLOAD_BODY)
        elif self.path == "/redirect":
            body = b"redirect body must not be downloaded"
            self.send_response(302)
            self.send_header("Location", "/final")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/error":
            body = b"not a successful download"
            self.send_response(404)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/header":
            if self.headers.get("X-Download-Token") != "allowed":
                self.send_error(403)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(DOWNLOAD_BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(DOWNLOAD_BODY)
        elif self.path == "/slow":
            self.send_response(200)
            self.send_header("Content-Length", str(len(DOWNLOAD_BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(DOWNLOAD_BODY[:8])
            self.wfile.flush()
            time.sleep(2)
            try:
                self.wfile.write(DOWNLOAD_BODY[8:])
            except (BrokenPipeError, ConnectionResetError):
                pass
        elif self.path == "/cancel-preserve":
            Path(self.headers["X-Create-Destination"]).write_bytes(b"preserved")
            self.send_response(200)
            self.send_header("Content-Length", str(len(LARGE_DOWNLOAD_BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                self.wfile.write(LARGE_DOWNLOAD_BODY)
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def run_download(method, url, destination, max_body=AMIAPI_DEFAULT_MAX_BODY_PY,
                 timeout=5, cancel_call=0, headers=(), env=None):
    command = [
        "./build/downloadget", method, url, str(destination),
        str(max_body), str(timeout), str(cancel_call),
    ]
    for name, value in headers:
        command.extend((name, value))
    return subprocess.run(
        command, text=True, capture_output=True, timeout=15, env=env
    )


def assert_no_download_temporary(destination):
    leftovers = list(destination.parent.glob(destination.name + ".amiapi.tmp.*"))
    assert not leftovers, leftovers


def test_file_downloads():
    with tempfile.TemporaryDirectory(prefix="amiapi-download-") as tmp:
        directory = Path(tmp)
        with TestServer(("127.0.0.1", 0), DownloadHandler) as server:
            port = server.server_address[1]
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{port}"
            try:
                for endpoint in ("length", "chunked", "eof"):
                    destination = directory / endpoint
                    result = run_download("GET", base + "/" + endpoint, destination)
                    assert result.returncode == 0, result.stdout + result.stderr
                    assert destination.read_bytes() == DOWNLOAD_BODY
                    assert_no_download_temporary(destination)
                    expected_total = len(DOWNLOAD_BODY) if endpoint == "length" else 0
                    assert f"received: {len(DOWNLOAD_BODY)}" in result.stdout
                    assert f"total: {expected_total}" in result.stdout

                large = directory / "large"
                result = run_download("GET", base + "/large", large,
                                      max_body=len(LARGE_DOWNLOAD_BODY))
                assert result.returncode == 0, result.stdout + result.stderr
                assert large.read_bytes() == LARGE_DOWNLOAD_BODY
                assert_no_download_temporary(large)

                redirected = directory / "redirected"
                result = run_download("GET", base + "/redirect", redirected)
                assert result.returncode == 0, result.stdout + result.stderr
                assert redirected.read_bytes() == DOWNLOAD_BODY
                assert_no_download_temporary(redirected)

                configured = directory / "configured"
                result = run_download(
                    "GET", base + "/header", configured,
                    headers=(("X-Download-Token", "allowed"),),
                )
                assert result.returncode == 0, result.stdout + result.stderr
                assert configured.read_bytes() == DOWNLOAD_BODY

                convenience = directory / "convenience"
                result = run_download("CONVENIENCE", base + "/length", convenience)
                assert result.returncode == 0, result.stdout + result.stderr
                assert convenience.read_bytes() == DOWNLOAD_BODY

                for endpoint, max_body, timeout in (
                    ("error", AMIAPI_DEFAULT_MAX_BODY_PY, 5),
                    ("length", 8, 5),
                    ("slow", AMIAPI_DEFAULT_MAX_BODY_PY, 1),
                ):
                    destination = directory / ("failed-" + endpoint)
                    result = run_download("GET", base + "/" + endpoint,
                                          destination, max_body, timeout)
                    assert result.returncode == 20, result.stdout + result.stderr
                    assert not destination.exists()
                    assert_no_download_temporary(destination)

                existing = directory / "existing"
                existing.write_bytes(b"preserved")
                result = run_download("GET", base + "/length", existing)
                assert result.returncode == 20, result.stdout + result.stderr
                assert existing.read_bytes() == b"preserved"
                assert_no_download_temporary(existing)

                cancelled = directory / "cancelled"
                result = run_download(
                    "GET", base + "/large", cancelled,
                    max_body=len(LARGE_DOWNLOAD_BODY), cancel_call=1,
                )
                assert result.returncode == 20, result.stdout + result.stderr
                assert "error: 15" in result.stdout, result.stdout
                assert "last-error: 15" in result.stdout, result.stdout
                assert not cancelled.exists()
                assert_no_download_temporary(cancelled)

                preserved = directory / "cancel-preserved"
                result = run_download(
                    "GET", base + "/cancel-preserve", preserved,
                    max_body=len(LARGE_DOWNLOAD_BODY), cancel_call=1,
                    headers=(("X-Create-Destination", str(preserved)),),
                )
                assert result.returncode == 20, result.stdout + result.stderr
                assert "error: 15" in result.stdout, result.stdout
                assert preserved.read_bytes() == b"preserved"
                assert_no_download_temporary(preserved)

                missing_parent = directory / "missing" / "file"
                result = run_download("GET", base + "/length", missing_parent)
                assert result.returncode == 20, result.stdout + result.stderr
                assert "error: 14" in result.stdout, result.stdout
                assert not missing_parent.exists()

                head = directory / "head"
                result = run_download("HEAD", base + "/length", head)
                assert result.returncode == 20, result.stdout + result.stderr
                assert not head.exists()
                assert_no_download_temporary(head)
            finally:
                server.shutdown()
                thread.join()

    print("Safe file download tests: OK")


def test_https_file_download():
    with tempfile.TemporaryDirectory(prefix="amiapi-https-download-") as tmp:
        directory = Path(tmp)
        ca_cert, server_cert, server_key = make_certificates(directory)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile=server_cert, keyfile=server_key)
        with TestServer(("127.0.0.1", 0), DownloadHandler) as server:
            server.socket = context.wrap_socket(server.socket, server_side=True)
            port = server.server_address[1]
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            env = os.environ.copy()
            env["SSL_CERT_FILE"] = str(ca_cert)
            destination = directory / "https-download"
            try:
                result = run_download(
                    "GET", f"https://localhost:{port}/chunked",
                    destination, env=env,
                )
            finally:
                server.shutdown()
                thread.join()
        assert result.returncode == 0, result.stdout + result.stderr
        assert destination.read_bytes() == DOWNLOAD_BODY
        assert_no_download_temporary(destination)

    print("HTTPS file download test: OK")


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


class MethodBodyHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def record(self):
        size = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(size)

        type(self).seen.append({
            "method": self.command,
            "length": size,
            "body": body,
        })

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def do_POST(self):
        self.record()

    def do_PUT(self):
        self.record()

    def do_PATCH(self):
        self.record()

    def do_DELETE(self):
        self.record()

    def do_HEAD(self):
        type(self).seen.append({
            "method": self.command,
            "length": int(self.headers.get("Content-Length", "0")),
            "body": b"",
        })

        self.send_response(200)
        self.send_header("Content-Length", "123")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def run_method_request(method, url, body_hex):
    return subprocess.run(
        [
            "./build/requestsend",
            method,
            url,
            body_hex,
        ],
        text=True,
        capture_output=True,
        timeout=15,
    )


def test_request_methods_and_bodies():
    MethodBodyHandler.seen = []

    with TestServer(("127.0.0.1", 0), MethodBodyHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            cases = [
                ("POST", "7b226f6b223a747275657d"),  # {"ok":true}
                ("PUT", "0001ff80"),
                ("PATCH", "41424300ff"),
                ("DELETE", "deadbeef"),
                ("HEAD", ""),
            ]

            results = []

            for method, body in cases:
                results.append(
                    run_method_request(
                        method,
                        f"http://127.0.0.1:{port}/method",
                        body,
                    )
                )
        finally:
            server.shutdown()
            thread.join()

    for result in results:
        if result.returncode != 0:
            raise AssertionError(
                "method/body request failed\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    expected = [
        ("POST", bytes.fromhex("7b226f6b223a747275657d")),
        ("PUT", bytes.fromhex("0001ff80")),
        ("PATCH", bytes.fromhex("41424300ff")),
        ("DELETE", bytes.fromhex("deadbeef")),
        ("HEAD", b""),
    ]

    assert len(MethodBodyHandler.seen) == len(expected)

    for seen, (method, body) in zip(
        MethodBodyHandler.seen,
        expected,
    ):
        assert seen["method"] == method, seen
        assert seen["body"] == body, seen

        if method == "HEAD":
            assert seen["length"] == 0, seen
        else:
            assert seen["length"] == len(body), seen

    print("Request method/body tests: OK")


def test_get_head_body_rejected():
    for method in ("GET", "HEAD"):
        result = run_method_request(
            method,
            "http://127.0.0.1:1/",
            "00ff",
        )

        if result.returncode != 6:
            raise AssertionError(
                f"{method} body was not rejected\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    print("GET/HEAD body rejection tests: OK")


class MethodRedirectHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def read_body(self):
        size = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(size)

    def redirect(self, status, location):
        self.send_response(status)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def finish_ok(self):
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def do_POST(self):
        body = self.read_body()

        type(self).seen.append(
            (self.path, "POST", body)
        )

        if self.path == "/303/start":
            self.redirect(303, "/303/final")
            return

        if self.path == "/307/start":
            self.redirect(307, "/307/final")
            return

        if self.path == "/308/start":
            self.redirect(308, "/308/final")
            return

        if self.path in ("/307/final", "/308/final"):
            self.finish_ok()
            return

        self.send_error(404)

    def do_GET(self):
        type(self).seen.append(
            (self.path, "GET", b"")
        )

        if self.path == "/303/final":
            self.finish_ok()
            return

        self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def test_method_redirect_semantics():
    payload = "0001ff80414243"
    expected_body = bytes.fromhex(payload)

    MethodRedirectHandler.seen = []

    with TestServer(("127.0.0.1", 0), MethodRedirectHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            results = []

            for status in ("303", "307", "308"):
                results.append(
                    run_method_request(
                        "POST",
                        f"http://127.0.0.1:{port}/{status}/start",
                        payload,
                    )
                )
        finally:
            server.shutdown()
            thread.join()

    for result in results:
        if result.returncode != 0:
            raise AssertionError(
                "redirected method request failed\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    assert MethodRedirectHandler.seen == [
        ("/303/start", "POST", expected_body),
        ("/303/final", "GET", b""),
        ("/307/start", "POST", expected_body),
        ("/307/final", "POST", expected_body),
        ("/308/start", "POST", expected_body),
        ("/308/final", "POST", expected_body),
    ], MethodRedirectHandler.seen

    print("Request method redirect tests: OK")


class JSONHTTPHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        cases = {
            "/json": (
                "application/json",
                b'{"name":"AmiAPI"}',
            ),
            "/json-charset": (
                "application/json; charset=utf-8",
                b'{"name":"AmiAPI"}',
            ),
            "/problem": (
                "application/problem+json",
                b'{"name":"AmiAPI"}',
            ),
            "/vendor": (
                "application/vnd.example+json",
                b'{"name":"AmiAPI"}',
            ),
            "/plain-json": (
                "text/plain",
                b'{"name":"AmiAPI"}',
            ),
            "/bad-json-type": (
                "application/jsonp",
                b'{"name":"AmiAPI"}',
            ),
            "/invalid-json": (
                "application/json",
                b'{"name":',
            ),
        }

        if self.path not in cases:
            self.send_error(404)
            return

        content_type, body = cases[self.path]

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def test_json_http_integration():
    with TestServer(("127.0.0.1", 0), JSONHTTPHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            cases = [
                ("/json", True, True),
                ("/json-charset", True, True),
                ("/problem", True, True),
                ("/vendor", True, True),
                ("/plain-json", False, True),
                ("/bad-json-type", False, True),
                ("/invalid-json", True, False),
            ]

            for path, expected_is_json, expected_parse in cases:
                result = subprocess.run(
                    [
                        "./build/jsonhttp",
                        f"http://127.0.0.1:{port}{path}",
                    ],
                    text=True,
                    capture_output=True,
                    timeout=15,
                )

                if expected_parse:
                    if result.returncode != 0:
                        raise AssertionError(
                            f"JSON HTTP case failed for {path}\n"
                            f"exit={result.returncode}\n"
                            f"stdout:\n{result.stdout}\n"
                            f"stderr:\n{result.stderr}"
                        )

                    expected_line = (
                        "is-json: 1"
                        if expected_is_json
                        else "is-json: 0"
                    )

                    assert expected_line in result.stdout, result.stdout
                    assert "name: AmiAPI" in result.stdout, result.stdout
                else:
                    if result.returncode != 21:
                        raise AssertionError(
                            f"invalid JSON unexpectedly parsed for {path}\n"
                            f"exit={result.returncode}\n"
                            f"stdout:\n{result.stdout}\n"
                            f"stderr:\n{result.stderr}"
                        )

        finally:
            server.shutdown()
            thread.join()

    print("JSON/HTTP integration tests: OK")


class JSONRequestHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(size)

        type(self).seen.append({
            "content_type": self.headers.get("Content-Type"),
            "accept": self.headers.get("Accept"),
            "body": body,
        })

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_json_request_helper():
    JSONRequestHandler.seen = []

    with TestServer(("127.0.0.1", 0), JSONRequestHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/jsonsend",
                    f"http://127.0.0.1:{port}/json",
                    '{"name":"AmiAPI","ok":true}',
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
            "JSON request helper failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert len(JSONRequestHandler.seen) == 1

    seen = JSONRequestHandler.seen[0]

    assert seen["content_type"] == "application/json", seen
    assert seen["accept"] == "*/*", seen
    assert seen["body"] == b'{"name":"AmiAPI","ok":true}', seen

    print("JSON request helper test: OK")


class FormRequestHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(size)

        type(self).seen.append({
            "content_type": self.headers.get("Content-Type"),
            "body": body,
            "length": size,
        })

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_form_request_helper():
    FormRequestHandler.seen = []

    with TestServer(("127.0.0.1", 0), FormRequestHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/formsend",
                    f"http://127.0.0.1:{port}/token",
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
            "form request helper failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert len(FormRequestHandler.seen) == 1

    seen = FormRequestHandler.seen[0]

    expected = (
        b"grant_type=authorization+code&"
        b"special=a%2Bb%26c%3Dd"
    )

    assert (
        seen["content_type"]
        == "application/x-www-form-urlencoded"
    ), seen

    assert seen["body"] == expected, seen
    assert seen["length"] == len(expected), seen

    print("Form request helper test: OK")


class BearerAuthHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_GET(self):
        type(self).seen.append(
            self.headers.get("Authorization")
        )

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


class BearerRedirectHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    target_port = None

    def do_GET(self):
        self.send_response(302)
        self.send_header(
            "Location",
            f"http://127.0.0.1:{type(self).target_port}/final",
        )
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_bearer_auth_helper():
    BearerAuthHandler.seen = []

    with TestServer(("127.0.0.1", 0), BearerAuthHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/authget",
                    f"http://127.0.0.1:{port}/auth",
                    "secret-token",
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
            "Bearer auth helper failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert BearerAuthHandler.seen == [
        "Bearer secret-token"
    ], BearerAuthHandler.seen

    print("Bearer auth helper test: OK")


def test_bearer_auth_invalid_tokens():
    cases = [
        "",
        "good\r\nInjected: bad",
        "good\nInjected: bad",
    ]

    for token in cases:
        result = subprocess.run(
            [
                "./build/authget",
                "http://127.0.0.1:1/",
                token,
            ],
            text=True,
            capture_output=True,
            timeout=5,
        )

        if result.returncode != 5:
            raise AssertionError(
                "invalid Bearer token was not rejected\n"
                f"token={token!r}\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    print("Bearer auth validation tests: OK")


def test_bearer_auth_cross_origin_redirect():
    BearerAuthHandler.seen = []

    with TestServer(("127.0.0.1", 0), BearerAuthHandler) as target:
        target_port = target.server_address[1]

        target_thread = threading.Thread(
            target=target.serve_forever,
            daemon=True,
        )
        target_thread.start()

        BearerRedirectHandler.target_port = target_port

        with TestServer(
            ("127.0.0.1", 0),
            BearerRedirectHandler,
        ) as source:
            source_port = source.server_address[1]

            source_thread = threading.Thread(
                target=source.serve_forever,
                daemon=True,
            )
            source_thread.start()

            try:
                result = subprocess.run(
                    [
                        "./build/authget",
                        f"http://127.0.0.1:{source_port}/start",
                        "secret-token",
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
            "Bearer redirect test failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert BearerAuthHandler.seen == [None], BearerAuthHandler.seen

    print("Bearer cross-origin redirect test: OK")


class BasicAuthHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def do_GET(self):
        type(self).seen.append(
            self.headers.get("Authorization")
        )

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


class BasicRedirectHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    target_port = None

    def do_GET(self):
        self.send_response(302)
        self.send_header(
            "Location",
            f"http://127.0.0.1:{type(self).target_port}/final",
        )
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        pass


def test_basic_auth_helper():
    BasicAuthHandler.seen = []

    with TestServer(("127.0.0.1", 0), BasicAuthHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        try:
            result = subprocess.run(
                [
                    "./build/basicget",
                    f"http://127.0.0.1:{port}/auth",
                    "Aladdin",
                    "open sesame",
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
            "Basic auth helper failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert BasicAuthHandler.seen == [
        "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="
    ], BasicAuthHandler.seen

    print("Basic auth helper test: OK")


def test_basic_auth_edge_cases():
    cases = [
        ("user", ""),
        ("", "password"),
    ]

    expected = [
        "Basic dXNlcjo=",
        "Basic OnBhc3N3b3Jk",
    ]

    for (username, password), expected_header in zip(cases, expected):
        BasicAuthHandler.seen = []

        with TestServer(("127.0.0.1", 0), BasicAuthHandler) as server:
            port = server.server_address[1]

            thread = threading.Thread(
                target=server.serve_forever,
                daemon=True,
            )
            thread.start()

            try:
                result = subprocess.run(
                    [
                        "./build/basicget",
                        f"http://127.0.0.1:{port}/auth",
                        username,
                        password,
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
                "Basic auth edge case failed\n"
                f"user={username!r}\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

        assert BasicAuthHandler.seen == [
            expected_header
        ], BasicAuthHandler.seen

    print("Basic auth edge-case tests: OK")


def test_basic_auth_invalid_credentials():
    cases = [
        ("bad:user", "password"),
        ("good\r\nInjected", "password"),
        ("good\nInjected", "password"),
        ("user", "bad\r\nInjected"),
        ("user", "bad\nInjected"),
    ]

    for username, password in cases:
        result = subprocess.run(
            [
                "./build/basicget",
                "http://127.0.0.1:1/",
                username,
                password,
            ],
            text=True,
            capture_output=True,
            timeout=5,
        )

        if result.returncode != 5:
            raise AssertionError(
                "invalid Basic credentials were not rejected\n"
                f"user={username!r}\n"
                f"password={password!r}\n"
                f"exit={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    print("Basic auth validation tests: OK")


def test_basic_auth_cross_origin_redirect():
    BasicAuthHandler.seen = []

    with TestServer(("127.0.0.1", 0), BasicAuthHandler) as target:
        target_port = target.server_address[1]

        target_thread = threading.Thread(
            target=target.serve_forever,
            daemon=True,
        )
        target_thread.start()

        BasicRedirectHandler.target_port = target_port

        with TestServer(
            ("127.0.0.1", 0),
            BasicRedirectHandler,
        ) as source:
            source_port = source.server_address[1]

            source_thread = threading.Thread(
                target=source.serve_forever,
                daemon=True,
            )
            source_thread.start()

            try:
                result = subprocess.run(
                    [
                        "./build/basicget",
                        f"http://127.0.0.1:{source_port}/start",
                        "Aladdin",
                        "open sesame",
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
            "Basic auth redirect test failed\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    assert BasicAuthHandler.seen == [None], BasicAuthHandler.seen

    print("Basic cross-origin redirect test: OK")


class SlowResponseHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        # Deliberately exceed the client's one-second receive timeout.
        time.sleep(3.0)

        try:
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, fmt, *args):
        pass


def test_receive_timeout():
    with TestServer(("127.0.0.1", 0), SlowResponseHandler) as server:
        port = server.server_address[1]

        thread = threading.Thread(
            target=server.serve_forever,
            daemon=True,
        )
        thread.start()

        started = time.monotonic()

        try:
            result = subprocess.run(
                [
                    "./build/timeoutget",
                    f"http://127.0.0.1:{port}/slow",
                    "1",
                ],
                text=True,
                capture_output=True,
                timeout=4,
            )

            elapsed = time.monotonic() - started
        finally:
            server.shutdown()
            thread.join()

    if result.returncode != 20:
        raise AssertionError(
            "slow response did not trigger AmiAPI timeout\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    if "receive failed" not in result.stderr:
        raise AssertionError(
            "slow response produced unexpected AmiAPI error\n"
            f"stderr:\n{result.stderr}"
        )

    # It must fail well before the server's deliberate 3-second delay.
    if elapsed >= 2.5:
        raise AssertionError(
            f"receive timeout took too long: {elapsed:.3f}s"
        )

    if elapsed < 0.5:
        raise AssertionError(
            f"receive timeout fired implausibly early: {elapsed:.3f}s"
        )

    print("HTTP receive timeout test: OK")


def test_https_receive_timeout():
    with tempfile.TemporaryDirectory(
        prefix="amiapi-https-timeout-"
    ) as tmp:
        ca_cert, server_cert, server_key = make_certificates(tmp)

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(
            certfile=server_cert,
            keyfile=server_key,
        )

        with TestServer(
            ("127.0.0.1", 0),
            SlowResponseHandler,
        ) as server:
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

            started = time.monotonic()

            try:
                result = subprocess.run(
                    [
                        "./build/timeoutget",
                        f"https://localhost:{port}/slow",
                        "1",
                    ],
                    text=True,
                    capture_output=True,
                    timeout=4,
                    env=env,
                )

                elapsed = time.monotonic() - started
            finally:
                server.shutdown()
                thread.join()

    if result.returncode != 20:
        raise AssertionError(
            "slow HTTPS response did not trigger AmiAPI timeout\n"
            f"exit={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    if "receive failed" not in result.stderr:
        raise AssertionError(
            "slow HTTPS response produced unexpected AmiAPI error\n"
            f"stderr:\n{result.stderr}"
        )

    if elapsed >= 2.5:
        raise AssertionError(
            f"HTTPS receive timeout took too long: {elapsed:.3f}s"
        )

    if elapsed < 0.5:
        raise AssertionError(
            f"HTTPS receive timeout fired implausibly early: "
            f"{elapsed:.3f}s"
        )

    print("HTTPS receive timeout test: OK")


def main():
    test_receive_timeout()
    test_https_receive_timeout()
    test_basic_auth_helper()
    test_basic_auth_edge_cases()
    test_basic_auth_invalid_credentials()
    test_basic_auth_cross_origin_redirect()
    test_bearer_auth_helper()
    test_bearer_auth_invalid_tokens()
    test_bearer_auth_cross_origin_redirect()
    test_form_request_helper()
    test_json_http_integration()
    test_json_request_helper()
    test_request_headers()
    test_method_redirect_semantics()
    test_request_methods_and_bodies()
    test_get_head_body_rejected()
    test_sensitive_header_same_origin_redirect()
    test_sensitive_header_cross_origin_redirect()
    test_reserved_headers()
    test_header_injection()
    test_http()
    test_https()
    test_content_length_framing()
    test_chunked_transfer()
    test_https_chunked()
    test_streaming_responses()
    test_progress_and_response_headers()
    test_https_streaming()
    test_file_downloads()
    test_https_file_download()
    test_redirects()
    test_http_to_https_redirect()


if __name__ == "__main__":
    main()
