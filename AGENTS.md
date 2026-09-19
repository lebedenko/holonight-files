# HoloNight Files

Files owns file browsing, modal commands, operations, preview coordination, application settings and presentation.
Shared Qt/QML primitives belong to holonight-qt. Preserve hn-files, org.holonight.Files and HolonightFiles.
Read CONTRIBUTING.md and the assigned docs/sdd cycle before editing. An approved implementation plan authorizes
its scope. Keep worker filesystem algorithms separate from application coordination and QML presentation.
Use runtime QtQuick.Controls as Controls and explicit Holonight.Core / Holonight.Controls imports.
Never automate pointer movement, window focus or native UI interaction. Ask the user for manual native checks.
Run focused regressions first, then task check and isolated runtime acceptance before publication.
System installation/removal belongs to the umbrella; standalone builds and DESTDIR staging remain supported.
Do not publish or update umbrella pins without authorization. Keep implementation commits local to this repository.
