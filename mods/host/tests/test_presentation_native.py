"""Local ABI smoke test, compiling only this project's original activity module."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
from muse_client.presentation import ActivityPresentation, NativeAvatarSink

class NativePresentationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_python_worker_to_local_c_activity_abi(self):
        root = Path(__file__).parents[2]
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "avatar.so"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                "-I", str(root / "extensions/avatar/include"),
                str(root / "extensions/avatar/src/actell_avatar.c"), "-o", str(library)], check=True, capture_output=True)
            native = ctypes.CDLL(str(library))
            native.actell_avatar_set_enabled.argtypes = [ctypes.c_bool]
            native.actell_avatar_set_enabled.restype = None
            native.actell_avatar_set_activity.argtypes = [ctypes.c_int, ctypes.c_uint32]
            native.actell_avatar_set_activity.restype = None
            native.actell_avatar_enabled.restype = ctypes.c_bool
            native.actell_avatar_activity.restype = ctypes.c_int
            self.assertFalse(native.actell_avatar_enabled())
            layer = ActivityPresentation(NativeAvatarSink(native), enabled=True)
            def wait_for(predicate):
                limit = time.monotonic() + 1
                while time.monotonic() < limit and not predicate():
                    time.sleep(.005)
                self.assertTrue(predicate())
            try:
                params = dict(run_id="local-abi", call_id="read", tool="screenshot", deadline_ms=int(time.time()*1000)+3000)
                layer.begin(params)
                layer.observe(dict(run_id="local-abi", call_id="read", state="running"))
                wait_for(lambda: native.actell_avatar_activity() == 1)
                layer.begin(dict(params, call_id="write", tool="tap"))
                layer.observe(dict(run_id="local-abi", call_id="write", state="running"))
                wait_for(lambda: native.actell_avatar_activity() == 2)
                layer.set_enabled(False)
                wait_for(lambda: not native.actell_avatar_enabled() and native.actell_avatar_activity() == 0)
            finally:
                layer.close()
