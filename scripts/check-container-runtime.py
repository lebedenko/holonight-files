#!/usr/bin/env python3
"""Build current working sources and installed runtime against one immutable CI image."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
LOG = None


def run(command, **kwargs):
    print("+ " + " ".join(map(str, command)), flush=True)
    with LOG.open("a") as log:
        log.write("+ " + " ".join(map(str, command)) + "\n")
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", **kwargs) as process:
            for line in process.stdout:
                print(line, end="", flush=True)
                log.write(line)
            if process.wait():
                raise subprocess.CalledProcessError(process.returncode, command)


def snapshot(source, destination):
    # Preserve HEAD for provider revision checks, but copy the actual working tree.
    run(["git", "clone", "--quiet", "--no-checkout", "--no-hardlinks", str(source), str(destination)])
    names = subprocess.check_output(
        ["git", "-C", str(source), "ls-files", "--cached", "--others", "--exclude-standard", "-z"]
    ).split(b"\0")
    for name in set(names) - {b""}:
        relative = Path(os.fsdecode(name))
        original = source / relative
        target = destination / relative
        if original.is_symlink() or original.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target, follow_symlinks=False)


def main():
    global LOG
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="container-runtime.", dir=ROOT / "build"))
    LOG = work / "verification.log"
    print(f"Container evidence and sources: {work}", flush=True)
    sources = work / "work"
    sources.mkdir()
    snapshot(ROOT, sources / "files")
    for provider in ("config", "qt", "images", "system-services"):
        variable = "HOLONIGHT_" + provider.upper().replace("-", "_") + "_SOURCE"
        source = Path(os.environ.get(variable, ROOT.parent / f"holonight-{provider}")).resolve()
        snapshot(source, sources / f"holonight-{provider}")
    ci_id = work / "ci-image-id"
    run(["docker", "build", "-t", "files-ci", "--iidfile", str(ci_id), "-f",
         str(sources / "files/packaging/Dockerfile.ci"), str(sources / "files")])
    image = ci_id.read_text().strip()
    run(["docker", "run", "--rm", "--network", "none", "--user", f"{os.getuid()}:{os.getgid()}",
         "-e", "HOME=/tmp", "-e", f"JOBS={os.environ.get('JOBS', '4')}",
         "-e", f"CMAKE_BUILD_PARALLEL_LEVEL={os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', '4')}",
         "-v", f"{sources}:/work", image, "bash", "-euc",
         "task build PRESET=release; bash scripts/prepare-runtime-check.sh"])
    context = sources / "files" / (sources / "files/build/runtime-check-context").read_text().strip()
    runtime_id = work / "runtime-image-id"
    run(["docker", "build", "-t", "holonight-files-runtime-check", "--build-arg", f"CI_IMAGE={image}",
         "--iidfile", str(runtime_id), str(context)])
    run(["docker", "run", "--rm", "--network", "none", runtime_id.read_text().strip()])
    print(f"Runtime acceptance passed. Evidence: {work}", flush=True)


if __name__ == "__main__":
    main()
