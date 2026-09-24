# Tasks

| Task | Requirements | State |
|---|---|---|
| C++ model and filtering | R1–R3 | Done |
| Actions and presentation | R4, R6 | Done |
| Activation and removal recovery | R5 | Done |
| Automated acceptance | R1–R6 | Done |
| Manual hardware acceptance | R1–R6 | In Progress |

- `task deps`: passed with the accepted Storage provider.
- Debug, Release, and clean Release acceptance build: passed. Clean build corrections were rebuilt.
- Final `task test` outside the sandbox: 27/27 CTest suites passed (59.50 seconds).
- `task format-check`, `task lint`, scoped clang-tidy for final corrections, REUSE lint,
  `task install-check`, QML import/metadata checks: passed. These individually cover `task check`;
  its initial sandboxed test run could not create private D-Bus sockets or OpenGL contexts.
- Final `task isolated-runtime-check`: passed; staged desktop application launched in Docker without network.
- Storage regressions cover filtering, empty readers, locked devices, activation supersession, modal guards,
  unmount/removal recovery, stale successful mount replies, and canonical paths reached through symlinks.

Verified on 2026-09-24 against published provider
`5f2ecda7eea653995f4c860bfb7f3a3f53beb279`, with Qt 6.11.2 and GCC 16.2.1.

Manual USB/optical/polkit and cross-application acceptance remains pending. The first reported USB insertion
showed nothing in either application. Read-only host inspection found only internal NVMe devices in both
`lsblk` and `udisksctl status`, the installed `/usr/bin/holonight-shell` running, and no `hn-files` process.
The user was asked to leave the stick attached, try another port, and run the local builds. This is not a
successful hardware acceptance result. No system installation was performed.

Manual follow-up: the user reports that connecting the stick directly to the laptop works as expected in both
applications. Host inspection confirms the Transcend JetFlash and its mounted ARCH_202609 filesystem.
Connection through the USB hub remains under investigation: neither the initial host snapshot nor the boot
kernel log showed an external hub. Separate unmount/recovery, optical and polkit scenarios remain unconfirmed.

Hub investigation resolved: the user found the unpowered hub was physically disconnected. After reconnecting
it and inserting the USB stick through it, both applications worked as expected. USB detection/display is
manually confirmed for direct and hub connections; no hub-specific code correction is needed. Detailed
unmount/removal recovery, optical-media and polkit checks are not inferred from this report.
