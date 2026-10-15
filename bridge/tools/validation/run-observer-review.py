"""Verify the copied observer matches source, then compile/run in a temporary directory."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("project", nargs="?", type=Path, default=here.parents[2])
parser.add_argument("--disable-leak-check", action="store_true", help="Only for environments where LeakSanitizer cannot run under tracing")
args = parser.parse_args()
source = (args.project / "bridge/extensions/headless-console/headless_console.c").read_text()
harness = (here / "observer-review.c").read_text()
start = "void headless_console_observe"
end = "#if CONFIG_MUSE_HEADLESS_HTTP_DEBUG\nstatic esp_err_t reply"
actual = source[source.index(start):source.index(end)].strip()
copy = harness[harness.index(start):harness.index("int main(void)")].strip()
if actual != copy:
    raise SystemExit("Observer source changed: review/update the harness before using this result")
temporary_root = args.project / "local/build/tmp"
temporary_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="muse-observer-review-", dir=temporary_root) as directory:
    binary = Path(directory) / "observer-review"
    command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-g", str(here / "observer-review.c"), "-o", str(binary)]
    subprocess.run(command, check=True)
    environment = os.environ.copy()
    if args.disable_leak_check:
        environment["ASAN_OPTIONS"] = "detect_leaks=0"
    subprocess.run([str(binary)], check=True, env=environment)
