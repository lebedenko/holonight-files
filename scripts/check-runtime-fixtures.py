#!/usr/bin/env python3
"""Exercise the installed verifier in disposable containers; retain logs in build/."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
WORK = Path(tempfile.mkdtemp(prefix="runtime-fixtures.", dir=ROOT / "build"))
IMAGE = sys.argv[1] if len(sys.argv) > 1 else "holonight-files-runtime-check"
EXECUTABLE = "/usr/bin/holonight-files"
DESKTOP = "/usr/share/applications/org.holonight.Files.desktop"
CASES = {
    "healthy": ("true", None),
    "delayed-exit": ("""cc -x c -o /usr/bin/holonight-files - <<'C'
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) return 0;
    sleep(1);
    return 1;
}
C
chmod 755 /usr/bin/holonight-files
""", "exited or was replaced during observation"),
    "missing-executable": (f"rm -- {EXECUTABLE}", "Invalid installed payload"),
    "root-only-executable": (f"chmod 700 {EXECUTABLE}", "Invalid installed payload"),
    "unreadable-desktop": (f"chmod 600 {DESKTOP}", "Invalid installed payload"),
    "incorrect-ownership": (f"chown files-test:files-test {DESKTOP}", "Invalid installed payload"),
    "pre-existing-process": (
        f"runuser -u files-test -- {EXECUTABLE} >/tmp/pre-existing.log 2>&1 &\nsleep 1",
        "Pre-existing holonight-files process",
    ),
}
for name, (setup, expected_error) in CASES.items():
    # Only fixture setup is privileged. The real verifier always runs as files-test.
    command = [
        "docker", "run", "--rm", "--network", "none", "--user", "root",
        IMAGE, "bash", "-euc",
        setup + "\nexec runuser -u files-test -- bash scripts/isolated-runtime.sh",
    ]
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    elapsed = time.monotonic() - started
    output = result.stdout + result.stderr
    (WORK / f"{name}.log").write_text(
        f"exit={result.returncode} elapsed={elapsed:.3f}s\n" + output
    )
    if expected_error is None:
        assert result.returncode == 0 and elapsed >= 3, output
        assert "observed for three seconds" in output, output
    else:
        assert result.returncode != 0 and expected_error in output, output
    print(f"{name}: passed (exit {result.returncode}, {elapsed:.3f}s)", flush=True)
print(f"Runtime fixture evidence: {WORK}")
