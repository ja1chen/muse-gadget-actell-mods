import http.client
import json
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from pathlib import Path
from muse_client.web_console import make_server

class ConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        Path(cls.assets.name, "index.html").write_text("<!doctype html><title>test fixture</title>")
        cls.server = make_server(static_dir=cls.assets.name)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.origin = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.assets.cleanup()

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def session(self):
        status, headers, body = self.request("GET", "/api/session")
        self.assertEqual(status, 200)
        session = json.loads(body)
        return {"Cookie": headers["Set-Cookie"].split(";")[0],
                "X-CSRF-Token": session["csrf_token"], "Origin": self.origin, "Content-Type": "application/json"}

    def rpc(self, headers, op, params):
        status, _, body = self.request("POST", "/api/rpc", json.dumps(dict(op=op, params=params)), headers)
        return status, json.loads(body)

    def test_default_loopback_only(self):
        self.assertEqual(self.server.server_address[0], "127.0.0.1")
        with self.assertRaises(ValueError):
            make_server(host="0.0.0.0")
        with self.assertRaises(ValueError):
            make_server(host="192.168.1.10")

    def test_session_cookie_and_security_headers(self):
        status, headers, body = self.request("GET", "/api/session")
        self.assertEqual(status, 200)
        self.assertIn("HttpOnly", headers["Set-Cookie"])
        self.assertIn("SameSite=Strict", headers["Set-Cookie"])
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        session = json.loads(body)
        self.assertEqual(session["mode"], "mock")
        self.assertFalse(session["natural_language_supported"])
        self.assertFalse(session["hardware_connected"])
        self.assertNotIn("secret", session)

    def test_auth_csrf_and_cross_origin(self):
        self.assertEqual(self.request("POST", "/api/rpc", "{}", {"Content-Type": "application/json"})[0], 401)
        headers = self.session()
        without_csrf = {k: v for k, v in headers.items() if k != "X-CSRF-Token"}
        self.assertEqual(self.rpc(without_csrf, "capabilities", {})[0], 403)
        for changes in ({"Origin": "https://attacker.example"}, {"Host": "attacker.example"}, {"Sec-Fetch-Site": "cross-site"}):
            self.assertEqual(self.rpc(dict(headers, **changes), "capabilities", {})[0], 403)

    def test_get_host_and_cross_site_rejected(self):
        self.assertEqual(self.request("GET", "/api/session", headers={"Host": "rebinding.example"})[0], 403)
        self.assertEqual(self.request("GET", "/api/session", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)

    def test_natural_language_is_honest_unsupported(self):
        status, _, body = self.request("POST", "/api/natural-language", '{"text":"hello"}', self.session())
        self.assertEqual(status, 501)
        self.assertEqual(json.loads(body)["error"]["code"], "unsupported")
        self.assertIn("no model request", json.loads(body)["error"]["message"])

    def test_mock_tools_end_to_end_and_cancel(self):
        headers = self.session()
        deadline = int(time.time() * 1000) + 60000
        self.assertEqual(self.rpc(headers, "capabilities", {})[0], 200)
        self.assertEqual(self.rpc(headers, "runs.open", dict(run_id="web-run", deadline_ms=deadline))[0], 200)
        def call(call_id, tool, arguments, **extra):
            return self.rpc(headers, "tools.call", dict(run_id="web-run", call_id=call_id,
                deadline_ms=deadline, tool=tool, arguments=arguments, **extra))
        apps = call("apps", "list_apps", {})[1]["result"]["result"]
        self.assertTrue(apps["mock_only"])
        screen = call("screen", "screenshot", {})[1]["result"]["result"]
        url = f'/api/image?run_id=web-run&image_id={screen["image_id"]}&generation={screen["generation"]}'
        status, image_headers, image = self.request("GET", url, headers=headers)
        self.assertEqual(status, 200)
        self.assertEqual(image_headers["Content-Type"], "image/png")
        self.assertTrue(image.startswith(b"\x89PNG"))
        self.assertEqual(call("tap", "tap", {"x": 0, "y": 0, "purpose": "mock test"}, observation=screen)[1]["result"]["state"], "succeeded")
        self.assertEqual(self.rpc(headers, "runs.cancel", {"run_id": "web-run"})[1]["result"]["state"], "cancelled")
        self.assertEqual(call("after-cancel", "list_apps", {})[1]["error"]["code"], "run_closed")

    def test_sessions_isolate_runs(self):
        owner, other = self.session(), self.session()
        self.rpc(owner, "runs.open", dict(run_id="private-run", deadline_ms=int(time.time() * 1000) + 60000))
        status, result = self.rpc(other, "runs.get", {"run_id": "private-run"})
        self.assertEqual(status, 400)
        self.assertEqual(result["error"]["code"], "forbidden")

    def test_no_arbitrary_endpoint_and_request_limit(self):
        headers = self.session()
        payload = json.dumps(dict(op="capabilities", params={}, endpoint="http://private.example"))
        self.assertEqual(self.request("POST", "/api/rpc", payload, headers)[0], 400)
        self.assertEqual(self.request("POST", "/api/rpc", "x" * 65537, headers)[0], 413)
        self.assertEqual(self.rpc(headers, "arbitrary.operation", {})[0], 400)

    def test_expired_session_purges_its_mock_data(self):
        headers = self.session()
        self.rpc(headers, "runs.open", dict(run_id="expiring-run", deadline_ms=int(time.time() * 1000) + 60000))
        item = next(item for item in self.server.console.sessions.values() if item["csrf"] == headers["X-CSRF-Token"])
        client_id = item["client"].client_id
        item["expires"] = time.monotonic() - 1
        self.assertEqual(self.rpc(headers, "runs.get", {"run_id": "expiring-run"})[0], 401)
        self.assertNotIn(client_id, self.server.console.state.keys)
        self.assertNotIn("expiring-run", self.server.console.state.runs)

    def test_optional_presentation_api_defaults_off_and_isolated(self):
        headers = self.session()
        other = self.session()
        status, _, body = self.request("GET", "/api/presentation", headers=headers)
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(body)["result"]["enabled"])
        bad = {k: v for k, v in headers.items() if k != "X-CSRF-Token"}
        self.assertEqual(self.request("POST", "/api/presentation", '{"enabled":true}', bad)[0], 403)
        self.assertEqual(self.request("POST", "/api/presentation", '{"enabled":"yes"}', headers)[0], 400)
        self.assertEqual(self.request("POST", "/api/presentation", '{"enabled":true}', headers)[0], 200)
        deadline = int(time.time()*1000)+60000
        self.rpc(headers, "runs.open", dict(run_id="avatar-run", deadline_ms=deadline))
        self.rpc(headers, "tools.call", dict(run_id="avatar-run", call_id="read", deadline_ms=deadline, tool="list_apps", arguments={}))
        state = json.loads(self.request("GET", "/api/presentation", headers=headers)[2])["result"]
        self.assertEqual(state["kind"], "phone_read")
        self.assertFalse(state["device_linked"])
        self.assertFalse(json.loads(self.request("GET", "/api/presentation", headers=other)[2])["result"]["enabled"])
        self.rpc(headers, "runs.cancel", {"run_id":"avatar-run"})
        self.assertEqual(json.loads(self.request("GET", "/api/presentation", headers=headers)[2])["result"]["kind"], "idle")
        self.request("POST", "/api/presentation", '{"enabled":false}', headers)
        self.assertFalse(json.loads(self.request("GET", "/api/presentation", headers=headers)[2])["result"]["enabled"])

    def test_presentation_budget_never_consumes_tool_budget(self):
        headers = self.session()
        item = next(item for item in self.server.console.sessions.values() if item["csrf"] == headers["X-CSRF-Token"])
        for _ in range(245):
            self.request("GET", "/api/presentation", headers=headers)
        self.assertEqual(item["requests"], [])
        self.assertEqual(self.request("POST", "/api/presentation", '{"enabled":true}', headers)[0], 429)
        self.assertEqual(item["requests"], [])
        self.assertEqual(self.rpc(headers, "capabilities", {})[0], 200)
        self.assertEqual(len(item["requests"]), 1)

    def test_missing_optional_module_does_not_break_tools(self):
        with patch("muse_client.web_console.ActivityPresentation", None):
            headers = self.session()
        self.assertEqual(self.rpc(headers, "capabilities", {})[0], 200)
        self.assertEqual(self.request("POST", "/api/presentation", '{"enabled":true}', headers)[0], 503)
        self.assertEqual(self.rpc(headers, "capabilities", {})[0], 200)

    def test_static_path_allowlist(self):
        self.assertEqual(self.request("GET", "/")[0], 200)
        for path in ("/../muse_client/client.py", "/.env", "/%2e%2e/LICENSE"):
            self.assertEqual(self.request("GET", path)[0], 404)

    def test_rate_limit(self):
        headers = self.session()
        for item in self.server.console.sessions.values():
            if item["csrf"] == headers["X-CSRF-Token"]:
                item["requests"] = [time.monotonic()] * 240
        self.assertEqual(self.rpc(headers, "capabilities", {})[0], 429)

if __name__ == "__main__":
    unittest.main()
