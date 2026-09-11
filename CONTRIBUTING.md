# Contributing

Use the same specification-driven development sequence as the sibling projects:

1. Clarify intent, scope, non-goals, and unresolved questions.
2. Write EARS requirements in docs/sdd/<cycle>/SPEC.md and obtain approval.
3. Write DESIGN.md and traceable TASKS.md; obtain design and task approval.
4. Implement the approved work in small, reviewable changes.
5. Verify requirements with focused tests, build and quality checks, and visual
   inspection where applicable. Record commands, outcomes, and limitations.
6. Update the specification, design, task status, README, and backlog. Mark work
   complete only when its acceptance checks pass; retain pending checks explicitly.

An explicitly approved implementation plan authorizes its specified scope;
routine implementation choices do not need repeated approval. Scope changes that
alter requirements return to the relevant approval step.

Start with `task deps`, then `task build` and `task test`. Before submitting, run
`task check` (alias `task verify`), which sequentially runs `task build`
(debug then release), `task test`, `task format-check`, `task lint` (`tidy` +
`qml-lint`), `task license-check`, `task install-check`, and
`task uninstall-check`, stopping on the first failure — equivalent to running
each individually. Run `task format` to format sources. Run
`task isolated-runtime-check` (requires Docker) before pushing changes that
touch install payloads, `CMakeLists.txt` install() rules, or the desktop
entry, since it is slower and not part of `task check`.
Use C++23, Qt 6.11+, shared HoloNight controls and tokens, translated UI strings,
and native decorations. Add GPL-3.0-or-later metadata for new material and preserve
third-party attribution. Do not include build output or local dependency installs.
