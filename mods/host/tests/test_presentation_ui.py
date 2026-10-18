import shutil
import subprocess
from pathlib import Path
import unittest

class PresentationUITests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node unavailable for UI ordering test")
    def test_out_of_order_and_uncertain_toggle(self):
        root = Path(__file__).parents[1]
        subprocess.run(["node", str(root / "tests/presentation_ui_order.js"),
                        str(root / "web_console/presentation.js")], check=True, capture_output=True)
