# External dependency management — holonight-files

Work package: EDM-015
Baseline: `e8af0a8312133609f2ddd30e87d084be78971a8c`
Umbrella audit: `38146d8f474317f3f8d72699f60e6d16a71dd165`
Status: Completed locally; publication and umbrella pinning remain separate.
Implementation baseline: `b1022c9297712878114ba3d827f970d7b16f3ce6` (clean canonical origin/main).
Umbrella handoff checkpoint: `5438cecea111cd54de33b6bdb1bf477555707811`.

## Requirements

- Configuration and normal build commands shall never acquire dependencies or prepare source providers.
- `task deps` shall explicitly prepare supplied local providers and check installed external prerequisites.
- Internal providers shall resolve only from approved prefixes; external packages shall resolve from an explicit
  external prefix or normal system locations.
- Enabled dependency-dependent features shall require their dependencies; minimal profiles shall disable them explicitly.
- Where production components use Qt, discovery shall require >=6.11; private Qt components shall match
  the selected public version exactly.
- Where tests use GTest, consumers shall require installed GTest >=1.14 CONFIG and any GMock targets they use.
- Existing public APIs, component switches, install-tree consumers and acceptance coverage shall be preserved.

## Design

Adopt canonical `tooling/modules/holonight-files.json` and `.cmake`, then synchronize bundle 2.0.0 into this repository.
The copied metadata is owned by this standalone checkout. CMake performs authoritative discovery; shared probes
provide prerequisite diagnostics and record resolved versions/paths. Incompatible caches and local provider install
trees are archived before reuse. Package acquisition occurs only in explicit environment preparation. CI switches
build/runtime/release lanes together to qualified immutable images and runs acceptance offline after preparation.

Remove QMarkdown from internal-only provider lookup. Require installed shared QMarkdown >=1.1 and public/private QML metadata. Preserve QMARKDOWN_SOURCE solely for explicit local preparation. Restrict internal lookup at the component scope so Core-only and Quick-only builds keep their existing minimal provider requirements (Qt and its transitive Config). Keep toml++ >=3.4 and minimal file-browser components.

## Implementation and verification tasks

- [x] Confirm the exact baseline and canonical handoff before implementation.
- [x] Adopt declarations, prefix helpers, explicit preparation and compatibility signatures.
- [x] Apply the repository-specific discovery changes above.
- [x] Update Task, CI preparation/entrypoints, Dockerfiles, release lanes and contributor instructions.
- [x] Run declaration/CMake/profile consistency checks and offline missing-package regressions.
- [x] Run narrow regressions, clean acceptance, installed-package consumers and required quality checks.
- [x] Inspect complete logs; record commands, versions, features, warnings and manual checks.
- [x] Make a repository-local commit only after local verification; publish only when authorized.

Implementation is in progress. Acceptance evidence is recorded below when executed.

## Development verification — 2026-10-13

- `python3 scripts/ci/test_launcher.py`: 13 tests pass (snapshot, exact revisions,
  offline/read-only/rootless acceptance, missing runtime, failure propagation,
  stale/tampered payload and mode checks, preservation of archived artifacts).
- `python3 scripts/ci/test_tooling_tidy.py`: 3 tests pass (explicit database,
  selected-source coverage and configured Qt library-tool preference).
- Qualified offline focused container: 17 dependency tests pass, including old/missing
  Qt/GTest/QMarkdown, static QMarkdown, all component scopes, direct/Task Core without
  internal providers, transitive leakage, spaces, ordinary commands without acquisition,
  compatible configuration reuse and incompatible cache archival.
- Canonical `tooling/test_tooling.py` (11) and `tooling/test_external.py` (21) pass;
  declaration/bundle drift and generated install profile checks pass.
- Preparation `build/ci/20261013T143232Z-uch59gkc/prepare.log` builds all six fixed
  revisions offline. `20261013T144951Z-mn0mvjc9/prepare.log` reuses every compatible
  source/toolchain/installed-package identity after contract changes. These precede
  the final preparation identity; clean acceptance remains pending.
- Qt's exported package requires Config transitively. Quick restricts both packages
  to approved internal prefixes; this avoids transitive leakage without changing its API.
- Files-specific tidy and Qt tool selection adapters preserve prior behavior around
  the unmodified canonical bundle. The obsolete supplement installer/bootstrap scripts
  are removed; historical audit evidence remains in older SDD cycles.

- The previous provider-revision shell fixture assumed remembered source overrides and
  selective validation-time rebuilds. It was replaced with a real-CMake explicit-preparation
  fixture (29.186 seconds, pass), retaining checks for all six provider revisions and shared
  QMarkdown while verifying compatible reuse, changed-source archival and invalid overrides.
  CTest keeps the existing `files-provider-revisions` registration with a 180-second timeout.
- Development Debug and Release builds pass; 24/25 CTest entries passed before updating
  that obsolete fixture. Clean acceptance includes the corrected registration.
- Preparation `20261013T153050Z-kt70w0bl`: pass with current identity, 1,959 content/mode
  hashes and exact published provider revisions. Full preparation log diagnostic review found
  only the four expected Qt private-module ABI warnings from Qt's provider; all providers and
  runtime use the same Qt 6.11.2 package build. Optional Vulkan headers are absent; no Vulkan
  component is requested. No compiler warning or error was emitted.

- Final review tightened Storage discovery to CONFIG mode. A caller-supplied Find module
  cannot supersede the approved installed package. The final 19-test dependency suite passes
  (`build/edm015-development/dependencies-review.log`, 18.119 seconds), covering this case
  and full-app configurations without GTest when tests are disabled. Adapter help and
  missing-command usage checks pass. These two discovery/usage corrections do not change
  the resolved provider target, compiler options or installed application behavior; the
  current clean acceptance snapshot predates them and their affected checks are rerun
  separately. Preparation is refreshed after acceptance to bind the final input identity.

- Clean offline run `build/ci/20261013T153428Z-acms82pf`: Core 2/2, Quick 4/4,
  full CTest 25/25, format-check, licensing and mandatory namespace acceptance pass.
  Mandatory namespace runs execute all 16 worker + 3 window cases with no skips.
  The build lane stops at strict tidy coverage because the explicit full-test database
  omits the installed consumer source. This failure is retained in results.json.
- Correction: Task tidy explicitly adds the three fresh installed consumer databases,
  covering their different compile definitions while excluding archived contexts.
  Four focused tidy regressions pass. Preparation inputs now include Taskfile.yml.
- Final explicit preparation `build/ci/20261013T181133Z-0z3oeirw`: pass, all six provider
  build trees reused with canonical source/toolchain/options/package checks. Current
  identity and content/mode hashes validate. Resume uses the verified isolation build
  plus compatible completed Debug/Release compiler artifacts, preserving original
  absolute /work/files paths, provider revisions, Qt/toolchain and feature options.
  The remaining `task check`, second locale and installed-runtime gates are pending.
- The full application binary reports 766 passed / 7 skipped out of 773 cases.
  Existing native/performance probes and the privileged reserved-block capacity probe
  retain their skips in the standard lane. Filesystem namespace binaries skip there
  under normal seccomp, then run without skips in the mandatory dedicated lane.
  No new native interaction matrix or performance campaign is claimed.

- Consumer cleanup is isolated in local commit `d94aa5298d46394c1168c8568096be88279e81c5`:
  explicit null comparison and braces around three returns, preserving behavior.
  All three installed consumers pass; canonical format and strict consumer tidy across
  all three contexts pass. No broad product formatting or refactor was necessary.
- Final dependency suite: 20 tests pass (`build/edm015-resume/dependencies-final.log`,
  20.803 seconds), including the adapter's `--preset=test` component selection.
- Strict tidy: all 49 application and 8 library units passed before the consumer finding;
  all 64 test units pass after correction, including each installed consumer context.
  Only external-header/NOLINT diagnostics are suppressed; no actionable diagnostic remains.
- QML lint, import policy, metadata, REUSE and staged Release installation pass in
  `build/edm015-resume/*-final.log`. The en_US.UTF-8 and installed runtime checks remain
  pending until their completion is recorded below.

## Final local acceptance — 2026-10-13

All automated adoption gates are verified. The initial clean offline run and its retained
failures are supplemented by focused corrections; unaffected expensive checks are reused,
not relabelled. The final tested implementation inputs match the working tree byte for byte.

- C.UTF-8 and en_US.UTF-8: 25/25 CTest registrations each; Core 2/2, Quick 4/4,
  all three installed Core/Quick/QML-plugin consumers pass.
- Debug/Release, canonical formatting, strict tidy across all 121 owned C++ units,
  QML lint/import/metadata, REUSE, staged installation and desktop association pass.
- Mandatory namespace runs: 16 worker and 3 window cases pass without skips.
- Installed runtime startup, desktop launch, root ownership/read permissions, writable
  user home/runtime directory and absence of development RPATHs pass. All 13 healthy/fault
  fixtures pass. Files stages no QMarkdown files; the qualified runtime package supplies
  its shared library, public/private QML imports and licenses.
- Final preparation `build/ci/20261013T192112Z-4q_bfkjs` passes, reusing all six providers;
  final input identity and content/mode hashes validate. Provider identities/options,
  package/toolchain versions, runtime image ID, changed files and complete log hashes are
  recorded in [VERIFICATION.json](VERIFICATION.json).
- Final launcher 13, tidy adapter 4, dependency 20, provider preparation fixture 1,
  canonical tooling 11 and canonical external 21 regressions pass. Declaration equality,
  bundle drift, generated installer profile and final whitespace/diff checks pass.
- Complete logs reviewed: only expected Qt private-module ABI warnings, missing optional
  Vulkan headers, external-header/NOLINT diagnostics and intentional negative fixture
  failures remain. The corrected missing-consumer-context and four consumer style errors
  are retained in historical logs; final affected checks have no actionable diagnostics.
- Existing seven native/performance/reserved-block probe skips remain documented above;
  no new manual native matrix or performance campaign is claimed. No host isolation policy
  was installed. Publication, canonical remote confirmation of the new commits, hosted CI
  query and umbrella pin update are separate later authorized steps.

Changed files are grouped into canonical declaration/bundle copies; component-aware CMake
and Task adapters; qualified CI preparation, workflows, runtime staging and fixtures;
provider/dependency/tidy regressions; contributor documentation and this SDD. Detailed
relative paths are in VERIFICATION.json. The behavior-preserving consumer cleanup is a
separate commit; the adoption commit is recorded by the umbrella coordinator.
