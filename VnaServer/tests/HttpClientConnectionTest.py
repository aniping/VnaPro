"""Exercise HttpClient connection reuse against a real serial HTTP/1.1 peer."""

import json
import socket
import threading
import unittest
from http.client import RemoteDisconnected
from http.server import BaseHTTPRequestHandler, HTTPServer

from WebHttpIntegrationTest import HttpClient

LARGE_PAYLOAD = "x" * (1024 * 1024)


class KeepAliveHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def finish(self):
        try:
            super().finish()
        finally:
            self.server.closed.set()

    def respond(self, status, value):
        body = json.dumps(value, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.server.received.append(
            (self.command, self.path, self.headers["Connection"])
        )
        if self.path == "/drop-after-mutation":
            self.server.mutations += 1
            self.close_connection = True
            return
        self.respond(200, {"received": len(json.loads(body)["payload"])})

    def do_GET(self):
        self.server.received.append(
            (self.command, self.path, self.headers["Connection"])
        )
        if self.path == "/large-response":
            self.respond(200, {"payload": LARGE_PAYLOAD})
        elif self.path == "/api-error":
            self.respond(
                400, {"error": {"code": "fixture", "message": "Expected error"}}
            )
        else:
            self.respond(200, {"ok": True})


class KeepAlivePeer(HTTPServer):
    """Serve serial requests and count accepts, including an otherwise invisible reconnect."""

    def __init__(self):
        super().__init__(("127.0.0.1", 0), KeepAliveHandler)
        self.accepted = []
        self.received = []
        self.mutations = 0
        self.closed = threading.Event()
        self.errors = []
        self.thread = threading.Thread(
            target=self.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True
        )

    def get_request(self):
        stream, address = super().get_request()
        stream.settimeout(5)
        self.accepted.append(stream)
        return stream, address

    def handle_error(self, *_):
        self.errors.append("Unexpected peer error")

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        for stream in self.accepted:
            try:
                stream.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.shutdown()
        self.server_close()
        self.thread.join(timeout=2)
        assert not self.thread.is_alive(), "Private HTTP peer did not stop"
        assert not self.errors, self.errors


class HttpClientConnectionTest(unittest.TestCase):
    def test_large_post_and_responses_share_one_connection_until_explicit_close(self):
        with KeepAlivePeer() as peer:
            client = HttpClient(peer.server_port)
            try:
                self.assertEqual(
                    client.request(
                        "POST", "/large-post", body={"payload": LARGE_PAYLOAD}
                    ),
                    {"received": len(LARGE_PAYLOAD)},
                )
                self.assertEqual(
                    client.request("GET", "/large-response"), {"payload": LARGE_PAYLOAD}
                )
                self.assertEqual(
                    client.request("GET", "/api-error", expected=400),
                    {"error": {"code": "fixture", "message": "Expected error"}},
                )
                self.assertEqual(client.request("GET", "/after-error"), {"ok": True})
                self.assertEqual(
                    len(peer.accepted),
                    1,
                    "Four requests must use one physical connection",
                )
                self.assertEqual(
                    [(method, path) for method, path, _ in peer.received],
                    [
                        ("POST", "/large-post"),
                        ("GET", "/large-response"),
                        ("GET", "/api-error"),
                        ("GET", "/after-error"),
                    ],
                )
                self.assertTrue(
                    all(
                        header.lower() == "keep-alive" for _, _, header in peer.received
                    ),
                    "Each request must opt in to the server's keep-alive contract",
                )
                self.assertFalse(peer.closed.is_set())
                client.close()
                self.assertTrue(
                    peer.closed.wait(1), "Explicit close must release the connection"
                )
            finally:
                client.close()

    def test_a_mutation_with_a_lost_response_is_not_replayed(self):
        with KeepAlivePeer() as peer:
            client = HttpClient(peer.server_port)
            try:
                with self.assertRaises((RemoteDisconnected, ConnectionResetError)):
                    client.request("POST", "/drop-after-mutation", body={"value": 1})
                self.assertEqual(peer.mutations, 1)
                self.assertEqual(len(peer.accepted), 1)
                self.assertEqual(len(peer.received), 1)
            finally:
                client.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
