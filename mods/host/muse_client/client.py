"""Dependency-free wire client; transport injection is required for Muse HomeLink."""
import hashlib
import hmac
import json
import os
import re
import ssl
import struct
import zlib
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from dataclasses import dataclass

@dataclass
class Response:
    body: bytes
    headers: dict

MAX_RESPONSE = 12 * 1024 * 1024
ID = re.compile(r"^[A-Za-z0-9_-]{1,128}$")

class RemoteError(Exception):
    def __init__(self, code, message):
        self.code = code
        super().__init__(f"{code}: {message}")

class UnknownOutcome(Exception):
    """Submission may have executed. Reconcile its original call ID; never replay blindly."""


def strict_json(raw):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError("duplicate JSON key")
            out[key] = value
        return out
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite JSON")))


def signature(key, client_id, request_id, timestamp, nonce, body):
    prefix = f"ACTELL1\n{client_id}\n{request_id}\n{timestamp}\n{nonce}\n".encode()
    return hmac.new(key, prefix + body, hashlib.sha256).hexdigest()


def image_dimensions(data, mime):
    """Validate container metadata, not a full pixel decoder."""
    if mime == "image/png":
        offset, dimensions, ended = 8, None, False
        while offset + 12 <= len(data):
            length = struct.unpack(">I", data[offset:offset + 4])[0]
            end = offset + 12 + length
            if end > len(data):
                raise ValueError("truncated PNG")
            kind, chunk = data[offset + 4:offset + 8], data[offset + 8:end - 4]
            crc = struct.unpack(">I", data[end - 4:end])[0]
            if zlib.crc32(kind + chunk) != crc:
                raise ValueError("PNG CRC mismatch")
            if offset == 8:
                if kind != b"IHDR" or length != 13:
                    raise ValueError("invalid PNG header")
                dimensions = struct.unpack(">II", chunk[:8])
            offset = end
            if kind == b"IEND":
                ended = length == 0 and offset == len(data)
                break
        if not ended or dimensions is None:
            raise ValueError("invalid PNG end")
        return dimensions
    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 255:
            raise ValueError("invalid JPEG marker")
        while offset < len(data) and data[offset] == 255:
            offset += 1
        marker = data[offset]
        offset += 1
        if marker in (0xD9, 0xDA):
            break
        length = int.from_bytes(data[offset:offset + 2], "big")
        if length < 2 or offset + length > len(data):
            raise ValueError("truncated JPEG")
        if marker in (0xC0, 0xC1, 0xC2):
            if length < 8:
                raise ValueError("invalid JPEG frame")
            height, width = struct.unpack(">HH", data[offset + 3:offset + 7])
            return width, height
        offset += length
    raise ValueError("unsupported/missing JPEG frame")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError("redirects are forbidden")


class DirectHTTPTransport:
    """For a verified network host, NOT an assumed Muse VM/HomeLink connection."""
    def __init__(self, endpoint, ca_file=None, *, test_only_loopback_http=False):
        parsed = urllib.parse.urlsplit(endpoint)
        if parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("endpoint must be a bare origin without credentials")
        if not parsed.hostname:
            raise ValueError("endpoint hostname required")
        if parsed.scheme != "https":
            if not (test_only_loopback_http and parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "::1")):
                raise ValueError("HTTPS required; only numeric loopback supports explicit test HTTP")
        self.endpoint = endpoint.rstrip("/") + "/v1/rpc"
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect(),
            urllib.request.HTTPSHandler(context=ssl.create_default_context(cafile=ca_file)))

    def __call__(self, body, headers, timeout):
        req = urllib.request.Request(self.endpoint, data=body, headers=headers, method="POST")
        with self.opener.open(req, timeout=timeout) as response:
            raw = response.read(MAX_RESPONSE + 1)
            if len(raw) > MAX_RESPONSE:
                raise ValueError("response too large")
            return Response(raw, {k.lower(): v for k, v in response.headers.items()})


class Client:
    def __init__(self, client_id, secret, transport, timeout=15):
        if not ID.fullmatch(client_id) or not isinstance(secret, bytes) or len(secret) < 32:
            raise ValueError("client ID and >=32 byte secret required")
        self.client_id, self.secret, self.transport, self.timeout = client_id, secret, transport, timeout

    def rpc(self, op, params, request_id=None):
        request_id = request_id or str(uuid.uuid4())
        if not ID.fullmatch(request_id):
            raise ValueError("invalid request_id")
        body = json.dumps(dict(v=1, client_id=self.client_id, request_id=request_id, op=op, params=params),
                          ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
        if len(body) > 1024 * 1024:
            raise ValueError("request too large")
        timestamp, nonce = str(int(time.time())), str(uuid.uuid4())
        headers = {"Content-Type": "application/json", "X-Actell-Time": timestamp, "X-Actell-Nonce": nonce,
                   "X-Actell-Signature": signature(self.secret, self.client_id, request_id, timestamp, nonce, body)}
        try:
            response = self.transport(body, headers, self.timeout)
            content_type = response.headers.get("content-type", "").split(";")[0]
            if op == "images.get" and content_type in ("image/png", "image/jpeg"):
                if response.headers.get("x-actell-request-id") != request_id:
                    raise ValueError("image request mismatch")
                return response
            if content_type != "application/json":
                raise ValueError("unexpected response content type")
            reply = strict_json(response.body)
            if type(reply.get("v")) is not int or reply.get("v") != 1 or reply.get("request_id") != request_id or type(reply.get("ok")) is not bool:
                raise ValueError("response envelope mismatch")
            if reply["ok"] and not isinstance(reply.get("result"), dict):
                raise ValueError("missing/invalid result")
            if reply["ok"] and op in ("tools.call", "calls.get", "runs.open", "runs.get", "runs.cancel"):
                if reply["result"].get("run_id") != params.get("run_id"):
                    raise ValueError("run response identity mismatch")
                if op in ("tools.call", "calls.get") and reply["result"].get("call_id") != params.get("call_id"):
                    raise ValueError("call response identity mismatch")
            if not reply["ok"]:
                error = reply.get("error")
                if (not isinstance(error, dict) or not isinstance(error.get("code"), str)
                        or not isinstance(error.get("message"), str) or type(error.get("retryable")) is not bool):
                    raise ValueError("missing/invalid error")
        except Exception as exc:
            if op in ("tools.call", "runs.open", "runs.cancel"):
                raise UnknownOutcome(f"{op} outcome unknown; reconcile original IDs, do not resubmit as a new action") from exc
            raise
        if not reply["ok"]:
            error = reply.get("error", {})
            raise RemoteError(error.get("code", "invalid_error"), error.get("message", ""))
        return reply["result"]

    def capabilities(self):
        return self.rpc("capabilities", {})

    def open_run(self, run_id, deadline_ms):
        return self.rpc("runs.open", dict(run_id=run_id, deadline_ms=deadline_ms))

    def call(self, run_id, call_id, deadline_ms, tool, arguments, observation=None):
        params = dict(run_id=run_id, call_id=call_id, deadline_ms=deadline_ms, tool=tool, arguments=arguments)
        if observation is not None:
            params["observation"] = observation
        return self.rpc("tools.call", params)

    def get_call(self, run_id, call_id):
        return self.rpc("calls.get", dict(run_id=run_id, call_id=call_id))

    def save_image(self, run_id, image_id, generation, destination):
        response = self.rpc("images.get", dict(run_id=run_id, image_id=image_id, generation=generation))
        headers, data = response.headers, response.body
        if headers.get("x-actell-image-id") != image_id or headers.get("x-actell-generation") != str(generation):
            raise ValueError("image metadata mismatch")
        width, height = int(headers["x-actell-width"]), int(headers["x-actell-height"])
        if not (0 < width <= 16384 and 0 < height <= 16384):
            raise ValueError("invalid image dimensions")
        mime = headers.get("content-type", "").split(";")[0]
        magic_ok = ((mime == "image/png" and data.startswith(b"\x89PNG\r\n\x1a\n")) or
                    (mime == "image/jpeg" and data.startswith(b"\xff\xd8\xff") and data.endswith(b"\xff\xd9")))
        if len(data) > 8 * 1024 * 1024 or not magic_ok:
            raise ValueError("invalid or oversized image")
        if image_dimensions(data, mime) != (width, height):
            raise ValueError("image/header dimensions mismatch")
        if str(len(data)) != headers.get("content-length") or hashlib.sha256(data).hexdigest() != headers.get("x-actell-sha256"):
            raise ValueError("image length/hash mismatch")
        fd = os.open(Path(destination), os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        return dict(path=str(destination), image_id=image_id, generation=generation, model_visible=False)
