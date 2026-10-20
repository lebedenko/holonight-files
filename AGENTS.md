# HoloNight Files

Use Conventional Commits for every new commit: `type(scope): imperative summary`, or `type: imperative summary` when a scope adds no clarity.

Files owns file browsing, modal commands, operations, preview coordination, application settings and presentation.
Shared Qt/QML primitives belong to holonight-qt. Preserve hn-files, org.holonight.Files and HolonightFiles.
Read CONTRIBUTING.md before editing. An approved implementation plan authorizes its scope. Keep worker filesystem algorithms separate from application coordination and QML presentation.
Use runtime QtQuick.Controls as Controls and explicit Holonight.Core / Holonight.Controls imports.
Never automate pointer movement, window focus or native UI interaction. Ask the user for manual native checks.
Run focused regressions first, then task check and isolated runtime acceptance before publication.
System installation/removal belongs to the umbrella; standalone builds and DESTDIR staging remain supported.
Do not publish or update umbrella pins without authorization. Keep implementation commits local to this repository.

## Development and documentation

Use `task build` and `task test` for incremental native CMake/CTest work. Native builds
consume installed packages; use `CMAKE_PREFIX_PATH` for an explicit development prefix.
Use `task format` for the pinned formatter and `task check` for container acceptance.
Run `task check:integration` for installation/runtime changes. Container builds use
fresh build/prefix directories, separate from `build/dev` and `build/release`.
clangd reads `build/dev/compile_commands.json` directly.

Routine changes need tests and a commit explanation; update user documentation when
behavior changes. Substantial designs need one local SDD describing intent, decisions,
interfaces and acceptance. Cross-project changes need one umbrella note linking local
designs and stating contracts, dependency order and integration checks. Historical SDDs
remain historical. No separate planning, readiness, handoff or closure commits are required.
Keep routine logs and generated evidence in ignored build directories or CI artifacts.
Provider and consumer implementation may proceed together; publish providers before
consumer commits that reference them. Update umbrella pins per coherent milestone.

## Gotchas

- Prefer Qt-recognized underlying-type spellings while preserving enum values, width, and signedness.
