# Design

Use repository-owned scripts/ci/run.py for local and remote build, isolation and
licensing lanes. Preserve workflow triggers, separate matrix results and artifact
upload. Use the immutable shared SDK and fsfe REUSE image plus checksum-pinned
supplemental archives; generate both locales inside disposable containers.

Snapshot tracked edits/deletions and non-ignored new files using a no-hardlinks
Git clone with real HEAD and an index of current inputs. Preserve symlinks/modes.
The snapshot is read-only; execution copies it to private /work/files. Privileged
bootstrap only installs verified tools, then drops to files-test for all checks.

The host builds the captured installed-runtime payload using the same SDK and
offline archives, then runs it without networking or host mounts. Nine existing
fixture checks retain their privileged setup inside disposable containers and
unprivileged verifier. Select Docker/Podman through FILES_CONTAINER_RUNTIME.

Isolation builds only its two executables in a separate fresh lane. Copy its
workspace into a second disposable container at the same compiled paths, reinstall
verified supplements offline, and run CTest as files-test with required isolation,
network disabled and seccomp relaxed only there. Where AppArmor is active, require
the existing holonight-files-isolation profile. Only hosted disposable runners load
the repository profile; the local launcher never changes host policy.

Retain all six existing provider commits. Native clang-tidy corrections preserve
public APIs and test expectations. Explicit HOLONIGHT_TIDY_DATABASE allows fresh
compiler contexts; scope coverage and configured Qt tool resolution remain checked.

## Diagnostic corrections

Owned getters explicitly return nodiscard results; internal fields follow existing
snake_case naming. Narrow enum storage retains numeric identities. Qt integer model
roles/QML properties retain unscoped enums with documented declaration-local
exceptions. QObject-derived classes explicitly delete already-unavailable copy/move
operations. Three helper references remain bound to their owning controller for
their entire lifetime; declaration-local exceptions preserve that invariant.
Four public fake-classifier controls retain documented test-only exceptions.
No checker family is disabled. Single-line comma policy ignores optional commas;
multiline enum/initializer checks remain enabled.

Preview fixtures use libexif allocator ownership, RAII for serialized buffers,
checked typed arrays/spans and explicit byte conversions. A baseline/current
standalone probe compares 61 output hashes. Literal review retains all C++ string
and character values. Three bookmark tests retain persistent model indexes while
asynchronous XDG removals shift rows; original availability/navigation assertions
remain exact. No timing tolerance or skipped assertion is added.

The SDK's MIME data (2.4) lacks the Python/Cython ancestry asserted by existing
checks. Pin shared-mime-info 2.5.1-2 and regenerate its cache after archive
extraction, both in build and offline runtime environments. A small SDK probe
confirms the exact expected ancestry. Fallback continuity now reuses the existing
one-icon temporary theme fixture from icon-provider tests rather than relying on
an installed host theme; all three original tier assertions remain enabled.
