"""Exercise the real HTTP client and sync engine against a local mock server.

Run via CTest with -DENABLE_PROTOCOL_TESTING=ON. No desktop clipboard is touched.
"""
import base64
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.responses = []
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                owner.requests.append(("GET", self.path, dict(self.headers), b""))
                status, body = owner.responses.pop(0) if owner.responses else (304, b"")
                self.send_response(status)
                self.send_header("ETag", '"revision-1"')
                self.end_headers()
                self.wfile.write(body)

            def do_PUT(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                owner.requests.append(("PUT", self.path, dict(self.headers), body))
                self.send_response(200)
                self.send_header("ETag", '"put-revision"')
                self.end_headers()

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def profile(self, text, **fields):
        value = {"Type": "Text", "Text": text, "HasData": False, **fields}
        self.responses.append((200, json.dumps(value, ensure_ascii=False).encode()))

    def run_probe(self, action, **config):
        value = {"serverUrl": self.url, "username": "user", "password": "pass",
                 "pollIntervalMs": 250, **config}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            proc = subprocess.run([EXE, str(path), action], capture_output=True,
                                  encoding="utf-8", timeout=10, check=True)
        return json.loads(proc.stdout.splitlines()[-1])

    def test_upload_format_hash_and_utf8_size(self):
        for text in ["", "abc", "中文🐧\n123", "a\u0000b"]:
            with self.subTest(text=text):
                result = self.run_probe("push", text=text)
                self.assertTrue(result["ok"])
                method, path, headers, body = self.requests[-1]
                self.assertEqual((method, path), ("PUT", "/SyncClipboard.json"))
                self.assertEqual(headers["Authorization"], "Basic " + base64.b64encode(b"user:pass").decode())
                self.assertEqual(json.loads(body), {
                    "Type": "Text", "Text": text, "Hash": hashlib.sha256(text.encode()).hexdigest(),
                    "HasData": False, "DataName": "", "Size": len(text.encode())})

    def test_directory_and_explicit_json_urls(self):
        for suffix, expected in [("/nested/", "/nested/SyncClipboard.json"),
                                 ("/nested/SyncClipboard.json", "/nested/SyncClipboard.json")]:
            with self.subTest(suffix=suffix):
                self.run_probe("push", serverUrl=self.url + suffix, text="x")
                self.assertEqual(self.requests[-1][1], expected)

    def test_pull_and_conditional_get(self):
        self.profile("中文🐧")
        result = self.run_probe("pull")
        self.assertEqual(result["text"], "中文🐧")
        self.assertEqual(result["hash"], hashlib.sha256("中文🐧".encode()).hexdigest())
        self.assertTrue(result["secondEmpty"])
        self.assertEqual(self.requests[1][2]["If-None-Match"], '"revision-1"')

    def test_startup_receives_without_echo(self):
        for text in ["remote text", ""]:
            with self.subTest(text=text):
                self.requests.clear()
                self.profile(text)
                result = self.run_probe("startup")
                self.assertTrue(result["ok"])
                self.assertEqual(result["text"], text)
                self.assertEqual([r[0] for r in self.requests], ["GET"])

    def test_404_bootstraps_local(self):
        self.responses.append((404, b""))
        result = self.run_probe("startup")
        self.assertTrue(result["ok"])
        self.assertEqual([r[0] for r in self.requests], ["GET", "PUT"])
        self.assertEqual(json.loads(self.requests[1][3])["Text"], "local text")

    def test_failures_do_not_upload_or_replace_clipboard(self):
        fixtures = [(401, b""), (403, b""), (500, b""), (200, b"broken"),
                    (200, b"{}"), (200, b'[]'), (200, b'{"Text":123}'),
                    (200, b'{"Type":"File","Text":"file.txt","HasData":true}'),
                    (200, b'{"Type":"Image","Text":"image.png"}')]
        for response in fixtures:
            with self.subTest(response=response):
                self.requests.clear()
                self.responses.append(response)
                result = self.run_probe("startup")
                self.assertFalse(result["ok"])
                self.assertEqual(result["text"], "local text")
                self.assertEqual([r[0] for r in self.requests], ["GET"])

    def test_poll_recovers_after_invalid_json(self):
        self.profile("initial")
        self.responses.append((200, b"broken"))
        self.profile("recovered")
        result = self.run_probe("poll")
        self.assertEqual(result["text"], "recovered")
        self.assertTrue(all(r[0] == "GET" for r in self.requests))

    def test_clipboard_write_failure_retries_without_etag(self):
        self.profile("remote")
        self.profile("remote")
        result = self.run_probe("retry")
        self.assertFalse(result["ok"])
        self.assertEqual(result["text"], "remote")
        self.assertNotIn("If-None-Match", self.requests[1][2])
        self.assertTrue(all(r[0] == "GET" for r in self.requests))

    def test_legacy_config_has_migration_error(self):
        result = self.run_probe("config", serverUrl="", davUrl=self.url, remotePath="old")
        self.assertIn("legacy WebDAV config", result["error"])

    def test_poll_interval_clamped(self):
        self.assertEqual(self.run_probe("config", pollIntervalMs=1)["interval"], 250)


if __name__ == "__main__":
    unittest.main()
