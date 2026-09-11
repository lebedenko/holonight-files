#!/usr/bin/env python3
"""Exercise real staged removals and mock privileged command ordering."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
(ROOT / "build").mkdir(exist_ok=True)
WORK = Path(tempfile.mkdtemp(prefix="uninstall-check.", dir=ROOT / "build"))
MOCK = WORK / "commands"
MOCK.mkdir()
LOG = WORK / "commands.log"
PAYLOAD = (
    "bin/holonight-files",
    "share/applications/org.holonight.Files.desktop",
    "share/icons/hicolor/scalable/apps/org.holonight.Files.svg",
    "share/licenses/holonight-files/LICENSE",
    "share/licenses/holonight-files/GPL-3.0-or-later.txt",
)


def command(name, body):
    path = MOCK / name
    path.write_text("#!/usr/bin/env bash\nset -eu\n" + body)
    path.chmod(0o755)


command("update-desktop-database", 'echo "database:$*" >> "$CHECK_LOG"\nexit "${DATABASE_STATUS:-0}"\n')
command("rm", 'echo removal >> "$CHECK_LOG"\nif [[ ${REMOVE_STATUS:-0} != 0 && ( -z ${REMOVE_PATH:-} || ${!#} == "$REMOVE_PATH" ) ]]; then exit "$REMOVE_STATUS"; fi\nexec /usr/bin/rm "$@"\n')
command("rmdir", 'echo directory >> "$CHECK_LOG"\nif [[ ${DIRECTORY_STATUS:-0} != 0 ]]; then exit "$DIRECTORY_STATUS"; fi\nexec /usr/bin/rmdir "$@"\n')
env = dict(os.environ, PATH=f"{MOCK}:{os.environ['PATH']}", CHECK_LOG=str(LOG))


def seed(stage, paths):
    for relative in paths:
        path = stage / "usr" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(relative)


def run(stage, status=0, **overrides):
    LOG.write_text("")
    result = subprocess.run(
        ["bash", str(ROOT / "scripts/uninstall.sh")],
        cwd=WORK, env=dict(env, DESTDIR=str(stage), **overrides),
        capture_output=True, text=True,
    )
    assert result.returncode == status, result.stderr
    return LOG.read_text().splitlines()


stage = WORK / "full stage"
preserved = (
    "lib/libHolonightQt.so", "lib/qt6/qml/Holonight/Controls/qmldir",
    "bin/other", "share/applications/other.desktop",
    "share/icons/hicolor/scalable/apps/other.svg",
    "share/licenses/holonight-files/unrelated",
)
seed(stage, PAYLOAD + preserved)
user = stage / "home/user/.config/HoloNight/Files.conf"
user.parent.mkdir(parents=True)
user.write_text("settings")
lines = run(stage)
assert lines == ["removal"] * 5 + ["directory", f"database:{stage}/usr/share/applications"]
assert all(not (stage / "usr" / p).exists() for p in PAYLOAD)
assert all((stage / "usr" / p).read_text() == p for p in preserved)
assert user.read_text() == "settings"
assert run(stage) == lines

partial = WORK / "partial"
seed(partial, PAYLOAD[:2])
run(partial)
assert not (partial / "usr/share/licenses/holonight-files").exists()
assert not (partial / "usr/bin/holonight-files").exists()
run(partial)
assert run(WORK / "absent") == ["removal"] * 5

failed = WORK / "failure"
seed(failed, PAYLOAD)
assert run(failed, 23, REMOVE_STATUS="23") == ["removal"]
assert all((failed / "usr" / p).exists() for p in PAYLOAD)
assert run(failed, 24, DIRECTORY_STATUS="24") == ["removal"] * 5 + ["directory"]
seed(failed, PAYLOAD)
assert run(failed, 25, DATABASE_STATUS="25")[-1].startswith("database:")
assert all(not (failed / "usr" / p).exists() for p in PAYLOAD)
# A blocked first or middle file must preserve every later payload.
for index in (0, 2):
    blocked = WORK / f"blocked-{index}"
    seed(blocked, PAYLOAD[:index] + PAYLOAD[index + 1:])
    target = blocked / "usr" / PAYLOAD[index]
    target.mkdir(parents=True)
    assert run(blocked, 1) == ["removal"] * (index + 1)
    assert target.is_dir()
    assert all(not (blocked / "usr" / p).exists() for p in PAYLOAD[:index])
    assert all((blocked / "usr" / p).read_text() == p for p in PAYLOAD[index + 1:])

middle = WORK / "middle-failure"
seed(middle, PAYLOAD)
assert run(middle, 29, REMOVE_STATUS="29",
           REMOVE_PATH=str(middle / "usr" / PAYLOAD[2])) == ["removal"] * 3
assert all(not (middle / "usr" / p).exists() for p in PAYLOAD[:2])
assert all((middle / "usr" / p).read_text() == p for p in PAYLOAD[2:])

# Exercise the real task entry; fake sudo validates its full command and redirects
# only this test invocation to a disposable tree after checking DESTDIR clearing.
command("sudo", '''echo "sudo:$*" >> "$CHECK_LOG"
if [[ ${SUDO_STATUS:-0} != 0 ]]; then exit "$SUDO_STATUS"; fi
[[ $# == 5 && $1 == env && $2 == -u && $3 == DESTDIR && $4 == bash && $5 == scripts/uninstall.sh ]]
exec env -u DESTDIR DESTDIR="$TASK_STAGE" bash scripts/uninstall.sh
''')
task_stage = WORK / "task"
seed(task_stage, PAYLOAD)
for sudo_status in (26, 0):
    LOG.write_text("")
    result = subprocess.run(
        ["task", "uninstall"], cwd=ROOT,
        env=dict(env, DESTDIR="/must-not-be-used", TASK_STAGE=str(task_stage),
                 SUDO_STATUS=str(sudo_status)), capture_output=True, text=True,
    )
    lines = LOG.read_text().splitlines()
    assert lines[0] == "sudo:env -u DESTDIR bash scripts/uninstall.sh"
    if sudo_status:
        assert result.returncode != 0 and len(lines) == 1, result.stderr
        assert all((task_stage / "usr" / p).exists() for p in PAYLOAD)
    else:
        assert result.returncode == 0, result.stderr
        assert lines[1:] == ["removal"] * 5 + ["directory", f"database:{task_stage}/usr/share/applications"]
        assert all(not (task_stage / "usr" / p).exists() for p in PAYLOAD)
for failure in ("REMOVE_STATUS", "DIRECTORY_STATUS", "DATABASE_STATUS"):
    seed(task_stage, PAYLOAD)
    LOG.write_text("")
    result = subprocess.run(
        ["task", "uninstall"], cwd=ROOT,
        env=dict(env, TASK_STAGE=str(task_stage), **{failure: "27"}),
        capture_output=True, text=True,
    )
    assert result.returncode != 0, result.stderr
    lines = LOG.read_text().splitlines()
    assert lines[0].startswith("sudo:") and lines[1] == "removal"
    if failure == "REMOVE_STATUS":
        assert len(lines) == 2
    elif failure == "DIRECTORY_STATUS":
        assert lines[1:] == ["removal"] * 5 + ["directory"]
    else:
        assert lines[1:] == ["removal"] * 5 + ["directory", f"database:{task_stage}/usr/share/applications"]
print(f"Uninstall checks passed: {WORK}")
