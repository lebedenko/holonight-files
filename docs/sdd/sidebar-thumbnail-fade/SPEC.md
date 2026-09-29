# Sidebar thumbnail fade

Status: implemented; focused regressions and task check passed; manual native timing
accepted by the user; isolated runtime acceptance blocked by container home permissions.
Approved scope: user-provided implementation plan, 2026-09-29.
This cycle supersedes the immediate thumbnail discard in sidebar-icon-delay REQ-F-001/005;
its 150 ms icon delay remains unchanged.

- REQ-F-001: When selection changes while a thumbnail is displayed and the next preview is loading, the sidebar shall fade the outgoing thumbnail to transparent over 120 ms with accelerating easing.
- REQ-F-002: When the current selection's thumbnail arrives, the sidebar shall display it immediately at full opacity.
- REQ-F-003: While navigation continues without another thumbnail arriving, the sidebar shall continue the existing fade without restarting it.
- REQ-F-004: When selection clears or an icon fallback becomes available, the sidebar shall discard the outgoing thumbnail immediately.
- REQ-F-005: If preview loading fails, then the sidebar shall discard the outgoing thumbnail immediately.
- REQ-F-006: When a fade ends or is interrupted, the sidebar shall release the retained outgoing pixels.
- REQ-F-007: When the pane becomes hidden, the sidebar shall clear its transition state.
- REQ-F-008: When selection changes, the sidebar shall update filename and metadata immediately.
- REQ-F-009: When resizing or receiving a resolution upgrade for the same selection, the sidebar shall update immediately without starting a fade.
- REQ-C-001: The sidebar shall preserve its fixed square frame, aspect fitting, rounded corners and existing 150 ms icon delay.
- REQ-C-002: The application shall preserve public APIs, decoding, Quick Look and settings behavior.

Acceptance: no selection may restore an expired image or its opacity. Automated offscreen
state tests establish behavior; user inspection of cached and uncached native navigation
must confirm whether 120 ms feels appropriate before native acceptance is complete.
