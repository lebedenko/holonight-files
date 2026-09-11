#!/usr/bin/env bash
set -euo pipefail
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
unset QML_IMPORT_PATH QML2_IMPORT_PATH LD_LIBRARY_PATH HOLONIGHT_DEPENDENCY_PREFIX HOLONIGHT_QML_IMPORT_PATH
unset QT_QUICK_CONTROLS_STYLE QT_QUICK_CONTROLS_CONF QT_QUICK_CONTROLS_FALLBACK_STYLE
export QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software

# No workspace volume may be mounted into this container.
test ! -e /work/files/CMakeLists.txt
test ! -e /work/files/build

# Verification must exercise the installed permissions as an ordinary user.
test "$(id -un)" = files-test
test -w "$HOME"
test -d "$XDG_RUNTIME_DIR"
test "$(stat -c '%u:%a' "$XDG_RUNTIME_DIR")" = "$(id -u):700"

for file in /usr/bin/holonight-files \
  /usr/share/applications/org.holonight.Files.desktop \
  /usr/share/icons/hicolor/scalable/apps/org.holonight.Files.svg \
  /usr/share/licenses/holonight-files/LICENSE \
  /usr/share/licenses/holonight-files/GPL-3.0-or-later.txt; do
  mode=644
  if [[ "$file" == /usr/bin/holonight-files ]]; then mode=755; fi
  if [[ ! -f "$file" || -L "$file" || ! -s "$file" || ! -r "$file" ]] ||
    [[ "$(stat -c '%u:%g:%a' "$file")" != "0:0:$mode" ]]; then
    printf 'Invalid installed payload (expected root:root mode %s): %s\n' "$mode" "$file" >&2
    exit 1
  fi
done
test -x /usr/bin/holonight-files

# Installed runtime search paths must not refer to development locations.
while IFS= read -r -d '' file; do
  if readelf -d "$file" 2>/dev/null | rg '\((RPATH|RUNPATH)\)' | rg '(^|/)(work|home|build|tmp)(/|$)'; then
    printf 'Development runtime path in %s\n' "$file" >&2
    exit 1
  fi
done < <(find /usr/bin/holonight-files /usr/lib -type f \( -name 'holonight-files' -o -iname '*holonight*' \) -print0)

QT_QPA_PLATFORM=offscreen holonight-files --version

desktop-file-validate /usr/share/applications/org.holonight.Files.desktop

# gio returns before its child has finished startup. Observe process identity and
# lifetime independently, retaining diagnostics and using a pidfd for safe cleanup.
python3 - <<'PYTHON'
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

EXECUTABLE = "/usr/bin/holonight-files"


def matching_pids():
    found = []
    for entry in Path("/proc").iterdir():
        if entry.name.isdecimal():
            try:
                if (entry / "comm").read_text().strip() == "holonight-files":
                    found.append(int(entry.name))
            except (FileNotFoundError, ProcessLookupError):
                pass
    return found


def identity(pid):
    try:
        # comm may contain spaces or parentheses; fields after its final ')' start
        # at field 3 (state); starttime is field 22.
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return fields[19], fields[0]
    except (FileNotFoundError, ProcessLookupError):
        return None


pid = start = pidfd = None
with tempfile.TemporaryFile(mode="w+") as diagnostics:
    try:
        if matching_pids():
            raise RuntimeError("Pre-existing holonight-files process")
        deadline = time.monotonic() + 3
        subprocess.run(
            ["gio", "launch", "/usr/share/applications/org.holonight.Files.desktop"],
            stdout=diagnostics, stderr=diagnostics, check=True, timeout=3,
        )
        while time.monotonic() < deadline:
            candidates = matching_pids()
            if len(candidates) > 1:
                raise RuntimeError("Ambiguous desktop launch: multiple matching processes")
            if candidates:
                pid = candidates[0]
                current = identity(pid)
                if current is None or current[1] in ("Z", "X"):
                    raise RuntimeError("Desktop process exited during discovery")
                start = current[0]
                pidfd = os.pidfd_open(pid)
                confirmed = identity(pid)
                if confirmed is None or confirmed[0] != start or confirmed[1] in ("Z", "X"):
                    raise RuntimeError("Desktop process changed during discovery")
                if os.readlink(f"/proc/{pid}/exe") != EXECUTABLE:
                    raise RuntimeError("Desktop process executable is not " + EXECUTABLE)
                break
            time.sleep(0.05)
        else:
            raise RuntimeError("No desktop process discovered within three seconds")
        deadline = time.monotonic() + 3
        while True:
            current = identity(pid)
            if current is None or current[0] != start or current[1] in ("Z", "X"):
                raise RuntimeError("Desktop process exited or was replaced during observation")
            if os.readlink(f"/proc/{pid}/exe") != EXECUTABLE:
                raise RuntimeError("Desktop process changed executable during observation")
            if time.monotonic() >= deadline:
                break
            time.sleep(0.05)
        print(f"Desktop launch passed: PID {pid}, start {start}, observed for three seconds")
    finally:
        if pidfd is not None:
            try:
                current = identity(pid)
                if current is not None and current[0] == start:
                    signal.pidfd_send_signal(pidfd, signal.SIGKILL)
            except ProcessLookupError:
                pass
            finally:
                os.close(pidfd)
        diagnostics.seek(0)
        print(diagnostics.read(), end="", flush=True)
PYTHON
