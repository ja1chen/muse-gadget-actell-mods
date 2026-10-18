"""Optional, failure-isolated local presentation. Never a tool or transport policy.

The C avatar sink is not automatically reachable from the Muse VM. A sink is a
local callback and MUST only update presentation; no network bridge is implied.
"""
from dataclasses import dataclass
import threading
import time
from .client import Client

READ_TOOLS = frozenset({"screenshot", "list_apps"})
WRITE_TOOLS = frozenset({"tap", "drag", "home", "launch_app", "type_text"})
TERMINAL = frozenset({"succeeded", "failed", "cancelled", "expired", "unknown"})


def classify_tool(name):
    if not isinstance(name, str):
        return None
    if name in READ_TOOLS:
        return "phone_read"
    if name in WRITE_TOOLS:
        return "phone_write"
    return None  # wait, capabilities, unknown/new tools have no phone animation.


@dataclass
class _Call:
    kind: str
    deadline: float
    active: bool = False
    hold_until: float = 0.0


class ActivityPresentation:
    """Default-off read/write layer; the renderer's seven base states are untouched.

    At most 256 tracked calls. One daemon callback worker, latest state only,
    short leases, and no joins/sleeps on the RPC caller. Callback errors are
    swallowed; a hung callback cannot block tools (receiver TTL must expire).
    """
    def __init__(self, sink=None, *, enabled=False, visible_ms=350,
                 clock=time.monotonic, wall_clock=time.time):
        self._sink = sink
        self._clock, self._wall_clock = clock, wall_clock
        self._visible = max(0, min(int(visible_ms), 500)) / 1000
        self._condition = threading.Condition()
        self._calls = {}
        self._terminal = {}
        self._enabled = False
        self._closed = False
        self._sequence = 0
        self._thread = None
        self._failures = 0
        self._owner_client = None
        if enabled:
            self.set_enabled(True)

    def claim(self, client):
        """A layer belongs to one client identity; never merge independent clients."""
        with self._condition:
            if self._owner_client is None:
                self._owner_client = client
            return self._owner_client is client

    def _changed(self):
        self._sequence += 1
        self._condition.notify_all()

    def _purge(self):
        now = self._clock()
        changed = False
        for key, call in list(self._calls.items()):
            if now >= call.deadline or (call.hold_until and now >= call.hold_until):
                del self._calls[key]
                changed = True
        if changed:
            self._changed()
        self._terminal = {key: expiry for key, expiry in self._terminal.items() if expiry > now}

    def set_enabled(self, enabled):
        if type(enabled) is not bool:
            raise ValueError("enabled must be boolean")
        with self._condition:
            if self._closed:
                return
            self._enabled = enabled
            if not enabled:
                self._calls.clear()
                self._terminal.clear()
            self._changed()
            if self._sink is not None and self._thread is None:
                self._thread = threading.Thread(target=self._deliver, name="actell-presentation", daemon=True)
                self._thread.start()

    def begin(self, params):
        """Remember a submitted call; submission/queued/approval are NOT running."""
        with self._condition:
            self._purge()
            if not self._enabled or self._closed or not isinstance(params, dict):
                return
            kind = classify_tool(params.get("tool"))
            key = (params.get("run_id"), params.get("call_id"))
            if not kind or not all(isinstance(x, str) for x in key) or key in self._terminal or key in self._calls:
                return
            deadline_ms = params.get("deadline_ms")
            if type(deadline_ms) is not int or len(self._calls) + len(self._terminal) >= 256:
                return
            remaining = min(3600, deadline_ms / 1000 - self._wall_clock())
            if remaining <= 0:
                return
            self._calls[key] = _Call(kind, self._clock() + remaining)
            self._changed()

    def observe(self, result):
        with self._condition:
            self._purge()
            if not self._enabled or self._closed or not isinstance(result, dict):
                return
            key = (result.get("run_id"), result.get("call_id"))
            if not all(isinstance(x, str) for x in key) or key in self._terminal:
                return  # late running/duplicate success never resurrects a completed call.
            call = self._calls.get(key)
            if call is None:
                return  # metadata absent: no inference from arbitrary poll results.
            state = result.get("state")
            if not isinstance(state, str):
                self._calls.pop(key, None)
                self._terminal[key] = call.deadline
                self._changed()
                return
            if state == "running":
                call.active = True
            elif state in ("queued", "pending_approval"):
                call.active = False
            elif state in TERMINAL:
                self._terminal[key] = call.deadline
                if state == "succeeded" and self._visible:
                    # A confirmed synchronous completion can be visible briefly.
                    # No success/celebration state and no delay on the RPC path.
                    call.active = True
                    call.hold_until = min(call.deadline, self._clock() + self._visible)
                else:
                    self._calls.pop(key, None)
            else:
                self._calls.pop(key, None)
            self._changed()

    def clear_call(self, run_id, call_id):
        with self._condition:
            key = (run_id, call_id)
            call = self._calls.pop(key, None)
            if call:
                self._terminal[key] = call.deadline
            self._changed()

    def clear_run(self, run_id):
        with self._condition:
            for key, call in list(self._calls.items()):
                if key[0] == run_id:
                    self._terminal[key] = call.deadline
                    del self._calls[key]
            self._changed()

    def _snapshot(self):
        self._purge()
        kinds = {call.kind for call in self._calls.values() if call.active}
        kind = "idle"
        if self._enabled and not self._closed:
            if "phone_write" in kinds:
                kind = "phone_write"
            elif "phone_read" in kinds:
                kind = "phone_read"
        return {"v": 1, "kind": kind, "enabled": self._enabled and not self._closed,
                "sequence": self._sequence, "ttl_ms": 1500 if kind != "idle" else 0}

    def snapshot(self):
        with self._condition:
            return self._snapshot()

    def _deliver(self):
        last = None
        next_lease = 0.0
        while True:
            with self._condition:
                event = self._snapshot()
                now = self._clock()
                state = (event["kind"], event["enabled"], event["sequence"])
                if state == last and (event["kind"] == "idle" or now < next_lease):
                    if self._closed:
                        return
                    self._condition.wait(timeout=0.05)
                    continue
                # Sequence monotonically increases for both changes and leases.
                self._sequence += 1
                event["sequence"] = self._sequence
                last = (event["kind"], event["enabled"], event["sequence"])
                next_lease = now + 0.5
                closed = self._closed
            try:
                self._sink(event)
            except Exception:
                self._failures += 1
            if closed:
                return

    def close(self):
        with self._condition:
            self._enabled = False
            self._closed = True
            self._calls.clear()
            self._terminal.clear()
            self._changed()
        # Deliberately do not join a callback that could be slow or broken.


class PresentedClient(Client):
    """Drop-in facade over an existing Client; forwards each RPC exactly once."""
    def __init__(self, client, presentation=None):
        self._client = client
        self.presentation = presentation or ActivityPresentation()
        try:
            if not self.presentation.claim(client):
                self.presentation = ActivityPresentation()  # Shared layer misuse falls back off.
                self.presentation.claim(client)
        except Exception:
            pass  # A broken optional layer must not stop construction of the tool facade.

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _safe(self, method, *args):
        try:
            getattr(self.presentation, method)(*args)
        except Exception:
            pass  # presentation can never decide whether a tool request happens.

    def rpc(self, op, params, request_id=None):
        if op == "tools.call":
            self._safe("begin", params)
        elif op == "runs.cancel" and isinstance(params, dict):
            self._safe("clear_run", params.get("run_id"))
        try:
            result = self._client.rpc(op, params, request_id=request_id)
        except BaseException:
            if op in ("tools.call", "calls.get") and isinstance(params, dict):
                self._safe("clear_call", params.get("run_id"), params.get("call_id"))
            raise  # preserve original exception object/type and unknown-outcome semantics.
        if op in ("tools.call", "calls.get"):
            self._safe("observe", result)
        elif op in ("runs.get", "runs.cancel") and isinstance(result, dict) and result.get("state") in ("cancelled", "expired"):
            self._safe("clear_run", result.get("run_id"))
        return result


class NativeAvatarSink:
    """Map events to an explicitly supplied local C binding, not a network path.

    Supply a trusted native binding exposing the two C functions. No library is
    loaded here. Call this sink ONLY from ActivityPresentation's single worker;
    never call the C setters concurrently from UI/network threads.
    """
    def __init__(self, native):
        self._native = native
        self._sequence = -1
        self._enabled = None

    def __call__(self, event):
        if (not isinstance(event, dict) or type(event.get("v")) is not int or event.get("v") != 1 or type(event.get("sequence")) is not int
                or type(event.get("enabled")) is not bool or type(event.get("ttl_ms")) is not int
                or event.get("kind") not in ("idle", "phone_read", "phone_write")):
            return
        if event["sequence"] <= self._sequence:
            return
        self._sequence = event["sequence"]
        enabled = event["enabled"]
        if enabled != self._enabled:
            self._native.actell_avatar_set_enabled(enabled)
            self._enabled = enabled
        activity = {"idle": 0, "phone_read": 1, "phone_write": 2}[event["kind"]] if enabled else 0
        ttl = max(0, min(event["ttl_ms"], 1500)) if activity else 0
        if activity and ttl == 0:
            activity = 0
        self._native.actell_avatar_set_activity(activity, ttl)
