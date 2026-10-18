import threading
import time
import unittest
from muse_client.presentation import ActivityPresentation, PresentedClient, NativeAvatarSink, classify_tool
from muse_client.client import UnknownOutcome

class Clock:
    now = 1000.0
    def __call__(self): return self.now

class StubClient:
    def __init__(self):
        self.calls = []
        self.result = None
        self.error = None
    def rpc(self, op, params, request_id=None):
        self.calls.append((op, params, request_id))
        if self.error:
            raise self.error
        return self.result

class PresentationTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.layer = ActivityPresentation(clock=self.clock, wall_clock=self.clock)
        self.stub = StubClient()
        self.client = PresentedClient(self.stub, self.layer)

    def tearDown(self):
        self.layer.close()

    def params(self, tool="screenshot", call="call", run="run"):
        return dict(run_id=run, call_id=call, deadline_ms=1010000, tool=tool, arguments={"private": "not emitted"})

    def result(self, state, call="call", run="run"):
        return dict(run_id=run, call_id=call, state=state)

    def start(self, tool="screenshot", call="call", run="run", state="running"):
        self.stub.result = self.result(state, call, run)
        return self.client.rpc("tools.call", self.params(tool, call, run))

    def test_default_off_and_no_rpc_changes(self):
        params = self.params()
        self.stub.result = self.result("running")
        actual = self.client.rpc("tools.call", params, request_id="fixed-request")
        self.assertIs(actual, self.stub.result)
        self.assertEqual(self.stub.calls, [("tools.call", params, "fixed-request")])
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.assertFalse(self.layer.snapshot()["enabled"])

    def test_exact_allowlist(self):
        for tool in ("screenshot", "list_apps"):
            self.assertEqual(classify_tool(tool), "phone_read")
        for tool in ("tap", "drag", "home", "launch_app", "type_text"):
            self.assertEqual(classify_tool(tool), "phone_write")
        for tool in ("wait", "capabilities", "shell", "Tap", None, {}, "screenshot_v2"):
            self.assertIsNone(classify_tool(tool))

    def test_queued_and_approval_are_not_running(self):
        self.layer.set_enabled(True)
        self.start("tap", state="queued")
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.layer.observe(self.result("pending_approval"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.layer.observe(self.result("running"))
        self.assertEqual(self.layer.snapshot()["kind"], "phone_write")

    def test_synchronous_success_has_short_nonblocking_hold(self):
        self.layer.set_enabled(True)
        self.start(state="succeeded")
        active = self.layer.snapshot()
        self.assertEqual(active["kind"], "phone_read")
        self.clock.now += .351
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.assertGreater(self.layer.snapshot()["sequence"], active["sequence"])
        self.layer.observe(self.result("succeeded"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")

    def test_concurrent_write_priority_then_read(self):
        self.layer.set_enabled(True)
        self.start("screenshot", "read")
        self.start("tap", "write")
        self.assertEqual(self.layer.snapshot()["kind"], "phone_write")
        self.layer.observe(self.result("cancelled", "write"))
        self.assertEqual(self.layer.snapshot()["kind"], "phone_read")
        self.layer.observe(self.result("failed", "read"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")

    def test_cancel_and_late_running_do_not_resurrect(self):
        self.layer.set_enabled(True)
        self.start("tap")
        self.stub.result = dict(run_id="run", state="cancelled")
        self.client.rpc("runs.cancel", {"run_id": "run"})
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.layer.observe(self.result("running"))
        self.layer.observe(self.result("succeeded"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")

    def test_deadline_disable_and_close_clear(self):
        self.layer.set_enabled(True)
        self.start()
        self.clock.now += 11
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.clock.now = 1000
        self.start(call="new")
        self.layer.set_enabled(False)
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.layer.set_enabled(True)
        self.layer.observe(self.result("running", "new"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.start(call="third")
        self.layer.close()
        self.assertFalse(self.layer.snapshot()["enabled"])
        self.assertEqual(self.layer.snapshot()["kind"], "idle")

    def test_run_cancel_does_not_clear_other_run(self):
        self.layer.set_enabled(True)
        self.start("tap", "write", "one")
        self.start("screenshot", "read", "two")
        self.layer.clear_run("one")
        self.assertEqual(self.layer.snapshot()["kind"], "phone_read")

    def test_unknown_exception_identity_and_no_retry(self):
        self.layer.set_enabled(True)
        original = UnknownOutcome("original")
        self.stub.error = original
        with self.assertRaises(UnknownOutcome) as caught:
            self.client.rpc("tools.call", self.params("tap"))
        self.assertIs(caught.exception, original)
        self.assertEqual(len(self.stub.calls), 1)
        self.assertEqual(self.layer.snapshot()["kind"], "idle")
        self.layer.observe(self.result("running"))
        self.assertEqual(self.layer.snapshot()["kind"], "idle")

    def test_presentation_exceptions_cannot_block_tool(self):
        class Broken:
            def __getattr__(self, name): raise RuntimeError("optional feature failure")
        client = PresentedClient(self.stub, Broken())
        self.stub.result = self.result("succeeded")
        self.assertIs(client.rpc("tools.call", self.params()), self.stub.result)
        self.assertEqual(len(self.stub.calls), 1)

    def test_hung_sink_does_not_wait_on_rpc_or_close(self):
        entered, release = threading.Event(), threading.Event()
        def blocked(event):
            entered.set()
            release.wait(2)
        layer = ActivityPresentation(blocked, enabled=True)
        self.assertTrue(entered.wait(1))
        client = PresentedClient(self.stub, layer)
        self.stub.result = self.result("running")
        start = time.monotonic()
        client.rpc("tools.call", dict(self.params(), deadline_ms=int(time.time()*1000)+5000))
        layer.close()
        elapsed = time.monotonic()-start
        release.set()
        self.assertLess(elapsed, .1)
        self.assertEqual(len(self.stub.calls), 1)

    def test_event_privacy_worker_and_expiry(self):
        events = []
        active, expired = threading.Event(), threading.Event()
        def sink(event):
            events.append(event)
            if event["kind"] == "phone_read": active.set()
            if active.is_set() and event["kind"] == "idle": expired.set()
        layer = ActivityPresentation(sink, enabled=True, visible_ms=0)
        try:
            params = dict(self.params(), deadline_ms=int(time.time()*1000)+150)
            layer.begin(params)
            layer.observe(self.result("running"))
            self.assertTrue(active.wait(1))
            self.assertTrue(expired.wait(1))
            for event in events:
                self.assertEqual(set(event), {"v", "kind", "enabled", "sequence", "ttl_ms"})
                self.assertNotIn("celebrate", event["kind"])
                self.assertLessEqual(event["ttl_ms"], 1500)
        finally:
            layer.close()

    def test_same_layer_cannot_merge_different_clients(self):
        self.layer.set_enabled(True)
        self.start("tap")
        other = StubClient()
        other.result = self.result("succeeded")
        wrapped = PresentedClient(other, self.layer)
        wrapped.rpc("tools.call", self.params())
        self.assertIsNot(wrapped.presentation, self.layer)
        self.assertFalse(wrapped.presentation.snapshot()["enabled"])
        self.assertEqual(self.layer.snapshot()["kind"], "phone_write")
        wrapped.presentation.close()

    def test_callback_failure_does_not_affect_result(self):
        called = threading.Event()
        def broken_sink(event):
            called.set()
            raise RuntimeError("private callback failure must not be logged")
        layer = ActivityPresentation(broken_sink, enabled=True)
        try:
            self.assertTrue(called.wait(1))
            client = PresentedClient(self.stub, layer)
            self.stub.result = self.result("succeeded")
            self.assertIs(client.rpc("tools.call", dict(self.params(), deadline_ms=int(time.time()*1000)+1000)), self.stub.result)
            self.assertEqual(len(self.stub.calls), 1)
        finally:
            layer.close()

    def test_native_mapping_stale_sequence_and_disabled(self):
        class Native:
            calls=[]
            def actell_avatar_set_enabled(self, flag): self.calls.append(("enabled", flag))
            def actell_avatar_set_activity(self, activity, ttl): self.calls.append((activity, ttl))
        native=Native();sink=NativeAvatarSink(native)
        event=dict(v=1, kind="phone_write", enabled=True, sequence=5, ttl_ms=9000)
        sink(event)
        self.assertEqual(native.calls, [("enabled", True),(2,1500)])
        sink(dict(event,sequence=4,kind="phone_read"))
        self.assertEqual(len(native.calls),2)
        sink(dict(event,sequence=6,enabled=False))
        self.assertEqual(native.calls[-2:],[("enabled",False),(0,0)])

if __name__ == "__main__":
    unittest.main()
