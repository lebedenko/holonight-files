#!/usr/bin/env python3
"""Exercise the installed verifier in disposable containers; retain logs in build/."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
WORK = Path(tempfile.mkdtemp(prefix="runtime-fixtures.", dir=ROOT / "build"))
if len(sys.argv) != 3:
    raise SystemExit("Usage: check-runtime-fixtures.py IMAGE INSTALLED_CONTEXT")
IMAGE = sys.argv[1]
CONTEXT = Path(sys.argv[2]).resolve()
INITIALIZE = """cp -a --no-preserve=ownership /payload/usr/. /usr/
chown -R root:root /usr/bin/hn-* /usr/share/applications/org.holonight.*
cp -a /checks /opt/check
ldconfig
update-desktop-database /usr/share/applications
useradd -m files-test
install -d -o files-test -g files-test -m 0700 /home/files-test /run/files-test
export HOME=/home/files-test XDG_RUNTIME_DIR=/run/files-test QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software
cd /opt/check
"""
EXECUTABLE = "/usr/bin/hn-files"
DESKTOP = "/usr/share/applications/org.holonight.Files.desktop"
CASES = {
    "healthy": ("true", None),
    "missing-markdown-plugin": ("rm /usr/lib/qt6/qml/QMarkdown/libQMarkdownPlugin.so", "Runtime verification failed"),
    "missing-markdown-library": ("rm /usr/lib/libQMarkdown.so.1", "Runtime verification failed"),
    "missing-markdown-metadata": ("rm /usr/lib/qt6/qml/QMarkdown/qmldir", "Runtime verification failed"),
    "missing-markdown-license": ("rm /usr/share/licenses/QMarkdown/LICENSE", "Runtime verification failed"),
    "unwritable-home": ("chmod 000 /home/files-test", 'test -w "$HOME"'),
    "delayed-exit": ("""cc -x c -o /usr/bin/hn-files - <<'C'
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) return 0;
    sleep(1);
    return 1;
}
C
chmod 755 /usr/bin/hn-files
""", "exited or was replaced during observation"),
    "missing-folder-argument": (
        f"sed -i 's/Exec=hn-files -- %f/Exec=hn-files/' {DESKTOP}",
        "Incorrect desktop folder arguments",
    ),
    "missing-executable": (f"rm -- {EXECUTABLE}", "Invalid installed payload"),
    "root-only-executable": (f"chmod 700 {EXECUTABLE}", "Invalid installed payload"),
    "unreadable-desktop": (f"chmod 600 {DESKTOP}", "Invalid installed payload"),
    "incorrect-ownership": (f"chown files-test:files-test {DESKTOP}", "Invalid installed payload"),
    "pre-existing-process": (
        f"runuser -u files-test -- {EXECUTABLE} >/tmp/pre-existing.log 2>&1 &\nsleep 1",
        "Pre-existing hn-files process",
    ),
}
for name, (setup, expected_error) in CASES.items():
    # Only fixture setup is privileged. The real verifier always runs as files-test.
    command = [
        "docker", "run", "--rm", "--pull=never", "--platform", "linux/amd64", "--network", "none", "--user", "root",
        '--mount', f'type=bind,src={CONTEXT / "payload"},dst=/payload,readonly',
        '--mount', f'type=bind,src={CONTEXT / "check"},dst=/checks,readonly',
        '--entrypoint', 'bash', IMAGE, "-euc",
        INITIALIZE + setup + "\nexec runuser -u files-test -- bash scripts/isolated-runtime.sh",
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
