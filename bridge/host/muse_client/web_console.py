"""Loopback-only web console backend. In-process mock; no devices or external APIs.

Run: python -m muse_client.web_console --port 8766
LAN exposure and real Muse natural-language transport are intentionally unsupported.
"""
import argparse
import hashlib
import json
import secrets
import threading
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from .client import Client, RemoteError, Response, UnknownOutcome, image_dimensions, strict_json
from .mock_server import State, TEST_SECRET
try:
    from .presentation import ActivityPresentation, PresentedClient
except Exception:
    ActivityPresentation = PresentedClient = None  # Optional feature never prevents console startup.


def presentation_snapshot(item):
    try:
        return dict(item["presentation"].snapshot(), mode="mock", device_linked=False)
    except Exception:
        return dict(v=1, kind="idle", enabled=False, sequence=0, ttl_ms=0, mode="mock", device_linked=False, unavailable=True)


MAX_BODY = 64 * 1024
OPS = {"capabilities", "runs.open", "runs.get", "runs.cancel", "tools.call", "calls.get"}


class Console:
    def __init__(self, static_dir=None):
        self.static_dir = Path(static_dir) if static_dir else Path(__file__).parents[1] / "web_console"
        self.state = State()
        self.sessions = {}
        self.lock = threading.Lock()

    def session(self, session_id, create=False):
        with self.lock:
            now = time.monotonic()
            expired = [key for key, item in self.sessions.items() if item["expires"] <= now]
            for key in expired:
                old = self.sessions.pop(key)
                try:
                    old["presentation"].close()
                except Exception:
                    pass
                client_id = old["client"].client_id
                # Mock-only session data has no recovery contract after expiry.
                # Drop private fixtures and keys instead of accumulating them.
                with self.state.lock:
                    self.state.keys.pop(client_id, None)
                    self.state.runs = {k: v for k, v in self.state.runs.items() if v["client"] != client_id}
                    self.state.calls = {k: v for k, v in self.state.calls.items() if k[0] != client_id}
                    self.state.images = {k: v for k, v in self.state.images.items() if k[0] != client_id}
                    self.state.nonces = {k: v for k, v in self.state.nonces.items() if k[0] != client_id}
            if session_id in self.sessions:
                return session_id, self.sessions[session_id]
            if not create or len(self.sessions) >= 64:
                return None, None
            session_id = secrets.token_urlsafe(32)
            client_id = "console-" + secrets.token_hex(16)
            # Public fixture key is only used inside this mock process.
            self.state.keys[client_id] = TEST_SECRET
            client = Client(client_id, TEST_SECRET, lambda body, headers, timeout: self.state.process(body, headers)[1])
            presentation = None
            try:
                presentation = ActivityPresentation()  # Default off, local state only.
                client = PresentedClient(client, presentation)
            except Exception:
                pass
            item = dict(csrf=secrets.token_urlsafe(32), expires=now + 3600, client=client, requests=[], presentation_requests=[], presentation=presentation)
            self.sessions[session_id] = item
            return session_id, item


def make_server(port=0, host="127.0.0.1", static_dir=None):
    if host != "127.0.0.1":
        raise ValueError("This prototype only binds 127.0.0.1; LAN mode requires a separately reviewed authenticated deployment")
    console = Console(static_dir)

    class Handler(BaseHTTPRequestHandler):
        server_version = "ActellMockConsole/1"

        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def rate_allowed(self, item, budget="requests"):
            with console.lock:
                now = time.monotonic()
                item[budget] = [stamp for stamp in item[budget] if stamp > now - 60]
                if len(item[budget]) >= 240:
                    return False
                item[budget].append(now)
                return True

        def log_message(self, *args):
            # No request bodies, URLs, cookies, signatures, or credentials in logs.
            pass

        def origin(self):
            return f"http://127.0.0.1:{self.server.server_port}"

        def safe_origin(self):
            return (self.headers.get("Host") == f"127.0.0.1:{self.server.server_port}"
                    and self.headers.get("Origin", self.origin()) == self.origin()
                    and self.headers.get("Sec-Fetch-Site", "same-origin") in ("same-origin", "none"))

        def reply(self, status, body, content_type="application/json", extra=None):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
            for key, value in (extra or {}).items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)

        def error(self, status, code, message):
            self.reply(status, dict(ok=False, error=dict(code=code, message=message, retryable=False)))

        def get_session(self, create=False):
            cookies = SimpleCookie()
            try:
                cookies.load(self.headers.get("Cookie", ""))
            except Exception:
                return None, None
            name = f"actell_mock_session_{self.server.server_port}"
            session_id = cookies[name].value if name in cookies else None
            return console.session(session_id, create)

        def do_GET(self):
            if not self.safe_origin():
                return self.error(403, "origin_rejected", "Use the exact loopback console URL")
            parsed = urlsplit(self.path)
            if parsed.path == "/api/session":
                session_id, item = self.get_session(create=True)
                if not item:
                    return self.error(429, "session_limit", "Restart the local mock to clear expired sessions")
                cookie = f"actell_mock_session_{self.server.server_port}={session_id}; HttpOnly; SameSite=Strict; Path=/; Max-Age=3600"
                return self.reply(200, dict(ok=True, csrf_token=item["csrf"], mode="mock",
                    natural_language_supported=False, hardware_connected=False, lan_enabled=False), extra={"Set-Cookie": cookie})
            if parsed.path == "/api/presentation":
                _, item = self.get_session()
                if not item:
                    return self.error(401, "session_required", "Open the console session first")
                if not self.rate_allowed(item, "presentation_requests"):
                    return self.error(429, "rate_limit", "Pause before requesting more mock operations")
                return self.reply(200, dict(ok=True, result=presentation_snapshot(item)))
            if parsed.path == "/api/image":
                _, item = self.get_session()
                if not item:
                    return self.error(401, "session_required", "Open the console session first")
                if not self.rate_allowed(item):
                    return self.error(429, "rate_limit", "Pause before requesting more mock operations")
                try:
                    query = parse_qs(parsed.query, strict_parsing=True)
                    if set(query) != {"run_id", "image_id", "generation"} or any(len(v) != 1 for v in query.values()):
                        raise ValueError("invalid image query")
                    run_id, image_id, generation = query["run_id"][0], query["image_id"][0], int(query["generation"][0])
                    response = item["client"].rpc("images.get", dict(run_id=run_id, image_id=image_id, generation=generation))
                    headers, data = response.headers, response.body
                    mime = headers["content-type"]
                    if (headers["x-actell-image-id"] != image_id or headers["x-actell-generation"] != str(generation)
                            or len(data) > 8 * 1024 * 1024 or str(len(data)) != headers["content-length"]
                            or hashlib.sha256(data).hexdigest() != headers["x-actell-sha256"]
                            or image_dimensions(data, mime) != (int(headers["x-actell-width"]), int(headers["x-actell-height"]))):
                        raise ValueError("invalid image metadata")
                    return self.reply(200, data, mime)
                except RemoteError as exc:
                    return self.error(400, exc.code, str(exc))
                except (ValueError, KeyError, AttributeError):
                    return self.error(400, "invalid_image", "Image unavailable or invalid")
            assets = {"/": ("index.html", "text/html; charset=utf-8"),
                      "/index.html": ("index.html", "text/html; charset=utf-8"),
                      "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                      "/presentation.js": ("presentation.js", "text/javascript; charset=utf-8"),
                      "/style.css": ("style.css", "text/css; charset=utf-8"),
                      "/styles.css": ("styles.css", "text/css; charset=utf-8")}
            if parsed.path not in assets:
                return self.error(404, "not_found", "Not found")
            name, mime = assets[parsed.path]
            try:
                body = (console.static_dir / name).read_bytes()
            except FileNotFoundError:
                return self.error(404, "asset_missing", "Console frontend is not installed")
            return self.reply(200, body, mime)

        def do_POST(self):
            if not self.safe_origin():
                return self.error(403, "origin_rejected", "Cross-origin requests are forbidden")
            _, item = self.get_session()
            token = self.headers.get("X-CSRF-Token", "")
            if not item:
                return self.error(401, "session_required", "Open the console session first")
            if not secrets.compare_digest(token, item["csrf"]):
                return self.error(403, "csrf_rejected", "Session CSRF token required")
            budget = "presentation_requests" if self.path == "/api/presentation" else "requests"
            if not self.rate_allowed(item, budget):
                return self.error(429, "rate_limit", "Pause before requesting more mock operations")
            if self.path == "/api/natural-language":
                return self.error(501, "unsupported", "Muse SDK text ingress is verified, but this adapter has not connected a real Muse session; no model request was sent")
            if self.path not in ("/api/rpc", "/api/presentation"):
                return self.error(404, "not_found", "Not found")
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json" or self.headers.get("Transfer-Encoding"):
                return self.error(400, "bad_request", "Expected bounded application/json")
            try:
                if len(self.headers.get_all("Content-Length", [])) != 1:
                    raise ValueError()
                size = int(self.headers.get("Content-Length", "-1"))
                if not 0 <= size <= MAX_BODY:
                    return self.error(413, "too_large", "Request exceeds console limit")
                request = strict_json(self.rfile.read(size))
                if self.path == "/api/presentation":
                    if not isinstance(request, dict) or set(request) != {"enabled"} or type(request["enabled"]) is not bool:
                        raise ValueError()
                    try:
                        item["presentation"].set_enabled(request["enabled"])
                    except Exception:
                        return self.error(503, "presentation_unavailable", "Optional presentation unavailable; tools remain usable")
                    return self.reply(200, dict(ok=True, result=presentation_snapshot(item)))
                if (not isinstance(request, dict) or set(request) != {"op", "params"}
                        or request["op"] not in OPS or not isinstance(request["params"], dict)):
                    raise ValueError()
                result = item["client"].rpc(request["op"], request["params"])
                return self.reply(200, dict(ok=True, result=result))
            except RemoteError as exc:
                return self.error(400, exc.code, str(exc))
            except UnknownOutcome:
                return self.error(409, "unknown_outcome", "Reconcile the original call ID; do not repeat the action")
            except (ValueError, TypeError, KeyError):
                return self.error(400, "bad_request", "Invalid RPC request")

    server = ThreadingHTTPServer((host, port), Handler)
    server.console = console
    original_close = server.server_close
    def close():
        for item in list(console.sessions.values()):
            try:
                item["presentation"].close()
            except Exception:
                pass
        original_close()
    server.server_close = close
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    server = make_server(args.port)
    print(f"MOCK ONLY: http://127.0.0.1:{server.server_port} | no hardware, no Muse model, no LAN", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
