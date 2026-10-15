"""Loopback-only, deterministic, process-local contract mock. NOT a device adapter."""
import struct
import zlib
import hashlib
import hmac
import json
import re
import uuid
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from .client import ID, Response, signature, strict_json

# Public test fixture; never a real credential.
TEST_SECRET = b"test-only-never-use-in-production-0000"
def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

PNG = (b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
       + _png_chunk(b"IDAT", zlib.compress(b"\x00\xff\xff\xff")) + _png_chunk(b"IEND", b""))

class Failure(Exception):
    def __init__(self, code):
        self.code = code

class State:
    def __init__(self, keys=None):
        self.keys = keys or {"test-client": TEST_SECRET, "other-client": TEST_SECRET}
        self.nonces, self.runs, self.calls, self.images = {}, {}, {}, {}
        self.generation, self.execution_count = 0, 0
        self.lock = threading.Lock()

    def process(self, body, headers):
        with self.lock:
            return self._process(body, {k.lower(): v for k, v in headers.items()})

    def _process(self, body, headers):
        request_id = "invalid"
        status = 200
        try:
            if len(body) > 1024 * 1024:
                return 413, Response(b'{}', {"content-type": "application/json"})
            try:
                envelope = strict_json(body)
                client, request_id = envelope["client_id"], envelope["request_id"]
                if type(envelope["v"]) is not int or envelope["v"] != 1 or not ID.fullmatch(client) or not ID.fullmatch(request_id) or not isinstance(envelope["params"], dict):
                    raise ValueError()
            except (ValueError, KeyError, TypeError, AttributeError):
                status = 400
                raise Failure("bad_request")
            try:
                stamp, nonce = headers["x-actell-time"], headers["x-actell-nonce"]
                if str(uuid.UUID(nonce)) != nonce.lower() or not re.fullmatch(r"[0-9]+", stamp) or str(int(stamp)) != stamp or abs(time.time() - int(stamp)) > 60:
                    raise ValueError()
                expected = signature(self.keys[client], client, request_id, stamp, nonce, body)
                if not hmac.compare_digest(expected, headers["x-actell-signature"]):
                    raise ValueError()
            except (ValueError, KeyError):
                status = 401
                raise Failure("unauthorized")
            now = time.time()
            self.nonces = {k: expiry for k, expiry in self.nonces.items() if expiry > now}
            nonce_key = (client, nonce.lower())
            if nonce_key in self.nonces:
                status = 401
                raise Failure("replay")
            self.nonces[nonce_key] = now + 120
            op, p = envelope["op"], envelope["params"]
            result = self.dispatch(client, op, p)
            if isinstance(result, Response):
                result.headers["x-actell-request-id"] = request_id
                return 200, result
            reply = dict(v=1, request_id=request_id, ok=True, result=result)
        except Failure as exc:
            reply = dict(v=1, request_id=request_id, ok=False,
                         error=dict(code=exc.code, message=exc.code.replace("_", " "), retryable=False))
        except (KeyError, ValueError, TypeError):
            reply = dict(v=1, request_id=request_id, ok=False,
                         error=dict(code="bad_request", message="invalid parameters", retryable=False))
        return status, Response(json.dumps(reply, separators=(",", ":")).encode(), {"content-type": "application/json"})

    def dispatch(self, client, op, p):
        now = int(time.time() * 1000)
        if op == "capabilities":
            return dict(tools=[dict(name="screenshot", description="Mock screenshot", parameters={"type": "object"}),
                               dict(name="list_apps", description="Synthetic app list; no device inspection", parameters={"type": "object"}),
                               dict(name="tap", description="Mock tap", parameters={"type": "object", "required": ["x", "y", "purpose"], "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "purpose": {"type": "string"}}})],
                        transport="https", approval="existing-app-policy", image_encoding="binary")
        run_id = p["run_id"]
        if not ID.fullmatch(run_id):
            raise Failure("bad_request")
        if op == "runs.open":
            if run_id in self.runs:
                existing = self.runs[run_id]
                if existing["client"] != client:
                    raise Failure("forbidden")
                if existing["deadline_ms"] != p["deadline_ms"]:
                    raise Failure("idempotency_conflict")
                return self.public_run(existing)
            if type(p["deadline_ms"]) is not int or not now < p["deadline_ms"] <= now + 3600000:
                raise Failure("deadline_exceeded")
            self.runs[run_id] = dict(client=client, run_id=run_id, state="open", deadline_ms=p["deadline_ms"])
            return self.public_run(self.runs[run_id])
        run = self.runs.get(run_id)
        if not run:
            raise Failure("not_found")
        if run["client"] != client:
            raise Failure("forbidden")
        if run["state"] == "open" and run["deadline_ms"] <= now:
            run["state"] = "expired"
        if op == "runs.get":
            return self.public_run(run)
        if op == "runs.cancel":
            if run["state"] == "open":
                run["state"] = "cancelled"
            return self.public_run(run)
        if op == "calls.get":
            call = self.calls.get((client, run_id, p["call_id"]))
            if not call:
                raise Failure("not_found")
            return call["record"]
        if op == "images.get":
            image = self.images.get((client, run_id, p["image_id"], p["generation"]))
            if image is None or run["state"] != "open":
                raise Failure("not_found")
            return Response(image, {"content-type": "image/png", "content-length": str(len(image)),
                "x-actell-image-id": p["image_id"], "x-actell-generation": str(p["generation"]),
                "x-actell-width": "1", "x-actell-height": "1", "x-actell-sha256": hashlib.sha256(image).hexdigest()})
        if op != "tools.call":
            raise Failure("bad_request")
        call_id = p["call_id"]
        if not ID.fullmatch(call_id) or not isinstance(p["arguments"], dict):
            raise Failure("bad_request")
        key = (client, run_id, call_id)
        if key in self.calls:
            if self.calls[key]["payload"] != p:
                raise Failure("idempotency_conflict")
            return self.calls[key]["record"]
        if run["state"] != "open":
            raise Failure("run_closed")
        if type(p["deadline_ms"]) is not int or not now < p["deadline_ms"] <= run["deadline_ms"]:
            raise Failure("deadline_exceeded")
        if p["tool"] not in ("screenshot", "tap", "list_apps"):
            raise Failure("tool_not_allowed")
        if p["tool"] == "tap":
            observation = p.get("observation")
            if (not isinstance(observation, dict) or not isinstance(observation.get("image_id"), str)
                    or type(observation.get("generation")) is not int):
                raise Failure("bad_request")
        # Mock has no real approval/executor. Production MUST preserve app approval queue.
        record = dict(run_id=run_id, call_id=call_id, state="running")
        self.calls[key] = dict(payload=json.loads(json.dumps(p)), record=record)
        if p["tool"] == "tap":
            observation = p.get("observation", {})
            image_key = (client, run_id, observation.get("image_id"), observation.get("generation"))
            if image_key not in self.images or observation.get("generation") != self.generation:
                record.update(state="failed", error={"code": "stale_observation"})
                return record
            if (not all(type(p["arguments"].get(k)) is int and p["arguments"][k] == 0 for k in ("x", "y"))
                    or not isinstance(p["arguments"].get("purpose"), str) or not p["arguments"]["purpose"]):
                record.update(state="failed", error={"code": "bad_request"})
                return record
            self.generation += 1
            record.update(state="succeeded", result={"mock_only": True})
        elif p["tool"] == "list_apps":
            record.update(state="succeeded", result={"mock_only": True, "apps": [
                {"bundle_id": "mock.example.notes", "name": "Mock Notes"},
                {"bundle_id": "mock.example.browser", "name": "Mock Browser"}]})
        else:
            self.generation += 1
            image_id = "image-" + str(self.generation)
            self.images[client, run_id, image_id, self.generation] = PNG
            record.update(state="succeeded", result=dict(image_id=image_id, generation=self.generation, width=1, height=1))
        self.execution_count += 1
        return record

    @staticmethod
    def public_run(run):
        return {k: v for k, v in run.items() if k != "client"}


def make_server(port=0, state=None):
    state = state or State()
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/v1/rpc":
                self.send_error(404)
                return
            try:
                size = int(self.headers.get("Content-Length", "-1"))
            except ValueError:
                size = -1
            if not 0 <= size <= 1024 * 1024:
                self.send_error(413)
                return
            status, response = state.process(self.rfile.read(size), dict(self.headers))
            self.send_response(status)
            for key, value in response.headers.items():
                self.send_header(key, value)
            if "content-length" not in response.headers:
                self.send_header("Content-Length", str(len(response.body)))
            self.end_headers()
            self.wfile.write(response.body)
        def log_message(self, *args):
            pass
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.state = state
    return server

if __name__ == "__main__":
    server = make_server(8765)
    print("TEST ONLY: mock listening on http://127.0.0.1:8765; no device connections", flush=True)
    server.serve_forever()
