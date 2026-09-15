# Executable rename and folder handling

Status: requirements, design and tasks authorized by the user's explicitly
approved implementation plan, supplied for implementation on 2026-09-16.

## Requirements (EARS)

- R1: When Files is built or freshly installed, the executable shall be
  `hn-files`, with no legacy command or compatibility alias installed.
- R2: When uninstall runs, it shall remove both executable names while retaining
  ordered failure propagation, repeatability and unrelated files.
- R3: When desktop metadata is installed and refreshed, Files shall advertise
  `inode/directory` with `Exec=hn-files -- %f`, without changing user defaults.
- R4: When launched for a single local folder, Files shall receive that exact
  path, including spaces and Unicode. Startup without arguments and invalid-path
  fallback shall retain existing behavior.
- R5: The project name, application identity, desktop ID, icon and license
  directory shall remain stable.
- R6: Documentation shall explain migration and verification shall record build,
  packaging, uninstall, MIME and launch evidence with host acceptance pending
  until performed.

Remote URIs, multiple folders, new browsing features and automatic default
selection are out of scope. Host reinstall is separate from repository work.
The scaffold restriction against folder-handler registration is superseded by
this approved scope; historical records remain unchanged.

Implementation and automated verification complete; native/host acceptance remains
pending in [verification](VERIFICATION.md).
