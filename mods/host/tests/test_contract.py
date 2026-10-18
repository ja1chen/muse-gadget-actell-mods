import hashlib
import json
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from muse_client.client import Client, DirectHTTPTransport, RemoteError, Response, UnknownOutcome, signature, strict_json, image_dimensions
from muse_client.mock_server import State, TEST_SECRET, PNG, make_server

class ContractTests(unittest.TestCase):
    def setUp(self):
        self.state = State()
        self.client = Client("test-client", TEST_SECRET, lambda b, h, t: self.state.process(b, h)[1])
        self.deadline = int(time.time() * 1000) + 60000
        self.client.open_run("run-1", self.deadline)

    def screenshot(self, call_id="screen-1"):
        return self.client.call("run-1", call_id, self.deadline, "screenshot", {})["result"]

    def assertCode(self, code, fn):
        with self.assertRaises(RemoteError) as context:
            fn()
        self.assertEqual(context.exception.code, code)

    def test_capabilities(self):
        self.assertEqual(self.client.capabilities()["image_encoding"], "binary")

    def test_idempotent_call(self):
        one = self.screenshot()
        self.assertEqual(one, self.screenshot())
        self.assertEqual(self.state.execution_count, 1)

    def test_conflicting_call(self):
        self.screenshot()
        self.assertCode("idempotency_conflict", lambda: self.client.call("run-1", "screen-1", self.deadline, "tap", {}))

    def test_run_idempotency(self):
        self.client.open_run("run-1", self.deadline)
        self.assertCode("idempotency_conflict", lambda: self.client.open_run("run-1", self.deadline + 1))

    def test_client_ownership(self):
        other = Client("other-client", TEST_SECRET, self.client.transport)
        self.assertCode("forbidden", lambda: other.rpc("runs.get", {"run_id": "run-1"}))
        self.assertCode("forbidden", lambda: other.open_run("run-1", self.deadline))

    def test_run_ownership_of_image(self):
        image = self.screenshot()
        self.client.open_run("run-2", self.deadline)
        self.assertCode("not_found", lambda: self.client.rpc("images.get", dict(run_id="run-2", image_id=image["image_id"], generation=image["generation"])))

    def test_stale_coordinate_and_idempotent_failure(self):
        image = self.screenshot()
        self.screenshot("screen-2")
        call = lambda: self.client.call("run-1", "tap-1", self.deadline, "tap", {"x": 0, "y": 0, "purpose": "test"}, image)
        result = call()
        self.assertEqual(result["error"]["code"], "stale_observation")
        self.assertEqual(result, call())
        self.assertEqual(self.state.execution_count, 2)

    def test_tap_invalidates_observation(self):
        image = self.screenshot()
        result = self.client.call("run-1", "tap-1", self.deadline, "tap", {"x": 0, "y": 0, "purpose": "test"}, image)
        self.assertEqual(result["state"], "succeeded")
        result = self.client.call("run-1", "tap-2", self.deadline, "tap", {"x": 0, "y": 0, "purpose": "test"}, image)
        self.assertEqual(result["error"]["code"], "stale_observation")

    def test_cancel_blocks_new_preserves_old(self):
        self.screenshot()
        self.client.rpc("runs.cancel", {"run_id": "run-1"})
        self.assertEqual(self.client.get_call("run-1", "screen-1")["state"], "succeeded")
        self.assertCode("run_closed", lambda: self.screenshot("screen-2"))
        self.screenshot()  # original accepted call still deduplicates
        self.assertEqual(self.state.execution_count, 1)

    def test_deadline(self):
        self.assertCode("deadline_exceeded", lambda: self.client.call("run-1", "late", 1, "screenshot", {}))

    def test_unknown_outcome_no_retry(self):
        attempts = []
        def timeout(body, headers, timeout):
            attempts.append(1)
            self.state.process(body, headers)
            raise TimeoutError()
        client = Client("test-client", TEST_SECRET, timeout)
        with self.assertRaises(UnknownOutcome):
            client.call("run-1", "uncertain", self.deadline, "screenshot", {})
        self.assertEqual(len(attempts), 1)
        self.assertEqual(self.client.get_call("run-1", "uncertain")["state"], "succeeded")

    def test_image_saved_private_and_no_overwrite(self):
        image = self.screenshot()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "screen.png"
            result = self.client.save_image("run-1", image["image_id"], image["generation"], path)
            self.assertFalse(result["model_visible"])
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                self.client.save_image("run-1", image["image_id"], image["generation"], path)

    def test_image_hash_rejected(self):
        image = self.screenshot()
        transport = self.client.transport
        def corrupt(body, headers, timeout):
            response = transport(body, headers, timeout)
            response.body += b"tampered"
            return response
        client = Client("test-client", TEST_SECRET, corrupt)
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
            client.save_image("run-1", image["image_id"], image["generation"], Path(directory) / "x")

    def test_auth_exact_bytes_replay_and_time(self):
        body = b'{ "v":1,"client_id":"test-client","request_id":"req-1","op":"capabilities","params":{}}'
        stamp, nonce = str(int(time.time())), "00000000-0000-4000-8000-000000000001"
        headers = {"X-Actell-Time": stamp, "X-Actell-Nonce": nonce,
                   "X-Actell-Signature": signature(TEST_SECRET, "test-client", "req-1", stamp, nonce, body)}
        self.assertEqual(self.state.process(body.replace(b'{ ', b'{'), headers)[0], 401)
        self.assertTrue(strict_json(self.state.process(body, headers)[1].body)["ok"])
        self.assertEqual(strict_json(self.state.process(body, headers)[1].body)["error"]["code"], "replay")
        headers["X-Actell-Time"] = "1"
        self.assertEqual(self.state.process(body, headers)[0], 401)

    def test_nonce_case_replay_and_canonical_timestamp(self):
        body = b'{"v":1,"client_id":"test-client","request_id":"req-2","op":"capabilities","params":{}}'
        stamp = str(int(time.time()))
        nonce = "abcdefab-cdef-4abc-8def-abcdefabcdef"
        def send(timestamp, nonce_value):
            headers = {"X-Actell-Time": timestamp, "X-Actell-Nonce": nonce_value,
                "X-Actell-Signature": signature(TEST_SECRET, "test-client", "req-2", timestamp, nonce_value, body)}
            return self.state.process(body, headers)
        self.assertEqual(send("0" + stamp, nonce)[0], 401)
        self.assertTrue(strict_json(send(stamp, nonce)[1].body)["ok"])
        self.assertEqual(strict_json(send(stamp, nonce.upper())[1].body)["error"]["code"], "replay")

    def test_duplicate_json_keys_rejected(self):
        self.assertEqual(self.state.process(b'{"v":1,"v":1}', {})[0], 400)

    def test_disallowed_tool(self):
        self.assertCode("tool_not_allowed", lambda: self.client.call("run-1", "x", self.deadline, "shell", {"command": "anything"}))

    def test_http_requires_explicit_loopback(self):
        for endpoint in ("http://example.com", "http://localhost", "https://user:pass@example.com", "https://example.com/path"):
            with self.assertRaises(ValueError):
                DirectHTTPTransport(endpoint, test_only_loopback_http=True)
        with self.assertRaises(ValueError):
            DirectHTTPTransport("http://127.0.0.1:8765")

    def test_auth_golden_vector(self):
        vector = json.loads((Path(__file__).parents[3] / "docs/protocol/auth-vector.json").read_text())
        envelope = json.loads(vector["body_utf8"])
        actual = signature(vector["key_utf8"].encode(), envelope["client_id"], envelope["request_id"],
                           vector["timestamp"], vector["nonce"], vector["body_utf8"].encode())
        self.assertEqual(actual, vector["signature_hex"])

    def test_malformed_mutation_reply_is_unknown(self):
        def transport(body, headers, timeout):
            request = strict_json(body)
            return Response(json.dumps(dict(v=1, request_id=request["request_id"], ok=True)).encode(),
                            {"content-type": "application/json"})
        client = Client("test-client", TEST_SECRET, transport)
        with self.assertRaises(UnknownOutcome):
            client.call("run-1", "bad-reply", self.deadline, "screenshot", {})

    def test_wrong_call_identity_is_unknown(self):
        def transport(body, headers, timeout):
            request = strict_json(body)
            return Response(json.dumps(dict(v=1, request_id=request["request_id"], ok=True,
                result=dict(run_id="run-1", call_id="wrong-call", state="succeeded"))).encode(),
                {"content-type": "application/json"})
        client = Client("test-client", TEST_SECRET, transport)
        with self.assertRaises(UnknownOutcome):
            client.call("run-1", "right-call", self.deadline, "screenshot", {})

    def test_png_container_crc(self):
        self.assertEqual(image_dimensions(PNG, "image/png"), (1, 1))
        corrupt = PNG[:45] + bytes([PNG[45] ^ 1]) + PNG[46:]
        with self.assertRaises(ValueError):
            image_dimensions(corrupt, "image/png")

    def test_malformed_observation_not_accepted(self):
        self.assertCode("bad_request", lambda: self.client.call("run-1", "broken", self.deadline, "tap", {}, []))
        self.assertCode("not_found", lambda: self.client.get_call("run-1", "broken"))

    def test_boolean_version_rejected(self):
        body = b'{"v":true,"client_id":"test-client","request_id":"req","params":{}}'
        self.assertEqual(self.state.process(body, {})[0], 400)

    def test_real_loopback_http(self):
        server = make_server()
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            transport = DirectHTTPTransport(f"http://127.0.0.1:{server.server_port}", test_only_loopback_http=True)
            client = Client("test-client", TEST_SECRET, transport)
            self.assertEqual(client.capabilities()["image_encoding"], "binary")
            client.open_run("http-run", self.deadline)
            image = client.call("http-run", "http-call", self.deadline, "screenshot", {})["result"]
            with tempfile.TemporaryDirectory() as d:
                client.save_image("http-run", image["image_id"], image["generation"], Path(d) / "image.png")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

if __name__ == "__main__":
    unittest.main()
