"""Optional live FT integration, using a local Python HTTP server and real TLS.

Build http_live_test with PICO_SDK_PATH set, then pass --runner PATH.
--https also downloads the supplied LOVE.BAS and compares every transferred byte.
"""
import argparse
import functools
import hashlib
import http.server
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import urllib.request

LOVE_URL = (
    "https://raw.githubusercontent.com/AzureSphereCloudEnabledAltair8800/"
    "RetroGames/refs/heads/main/LOVE.BAS"
)
BINARY = bytes(range(256)) * 300 + b"\x00\xffend"


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/BINARY.BIN")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for offset in range(0, len(BINARY), 349):
                data = BINARY[offset:offset + 349]
                self.wfile.write(f"{len(data):x};test=yes\r\n".encode() + data + b"\r\n")
            self.wfile.write(b"0\r\nX-Test-Trailer: yes\r\n\r\n")
        else:
            super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--https", action="store_true")
    args = parser.parse_args()
    runner = args.runner.resolve()
    with tempfile.TemporaryDirectory(prefix="z80-http-ft-") as tmp:
        root = Path(tmp)
        (root / "BINARY.BIN").write_bytes(BINARY)
        (root / "EMPTY.BIN").write_bytes(b"")
        server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(Handler, directory=tmp)
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def transfer(url, expected):
            output = root / "received.bin"
            subprocess.run([str(runner), url, str(output)], check=True, timeout=30)
            actual = output.read_bytes()
            assert actual == expected, f"byte mismatch: {url}"
            print(f"PASS {url}: {len(actual)} bytes SHA256={hashlib.sha256(actual).hexdigest()}")

        base = f"http://127.0.0.1:{server.server_port}"
        try:
            for name, expected in (
                ("BINARY.BIN", BINARY), ("EMPTY.BIN", b""),
                ("redirect", BINARY), ("chunked", BINARY),
            ):
                transfer(f"{base}/{name}", expected)
            result = subprocess.run(
                [str(runner), f"{base}/missing", str(root / "missing.bin")],
                check=False, timeout=30,
            )
            assert result.returncode == 1
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

        # A self-signed local HTTPS server must fail with the firmware trust
        # bundle. This also detects accidental optional/disabled verification.
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "1",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
        ], check=True, capture_output=True)
        server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(Handler, directory=tmp)
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run([
                str(runner), f"https://localhost:{server.server_port}/BINARY.BIN",
                str(root / "untrusted.bin"),
            ], check=False, timeout=30)
            assert result.returncode == 1
            print("PASS: untrusted HTTPS certificate rejected")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
        if args.https:
            with urllib.request.urlopen(LOVE_URL, timeout=20) as response:
                expected = response.read()
            transfer(LOVE_URL, expected)


if __name__ == "__main__":
    main()
