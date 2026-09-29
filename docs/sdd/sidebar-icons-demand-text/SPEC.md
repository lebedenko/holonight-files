# Stable sidebar icons and on-demand text

Approved scope: user-provided implementation plan, 2026-09-29.
Status: implemented; focused regressions, task check and manual native acceptance
passed. Isolated runtime acceptance remains blocked by existing container home permissions.
This cycle supersedes sidebar-icon-delay REQ-F-005's unconditional fallback reset,
and selection-time text loading and classification-only opening in quick-look-text-viewer.
The thumbnail fade, supported types, text limits, pinned navigation and image sizing remain applicable.

- REQ-F-001: When selection changes to the complete icon candidate chain of the displayed fallback, the sidebar shall keep that fallback visible during inspection.
- REQ-F-002: When the icon identity changes, selection clears, the pane hides or a thumbnail arrives, the sidebar shall clear fallback continuity.
- REQ-F-003: When a thumbnail arrives, the sidebar shall display it immediately.
- REQ-F-004: While Quick Look is closed, Files shall inspect only the selected file without loading text-preview lines.
- REQ-F-005: When Quick Look opens on supported text, Files shall load its text asynchronously.
- REQ-F-006: When Space is pressed during type detection, Files shall open a loading overlay pinned to the selection.
- REQ-F-007: If a pending selection proves unsupported or unreadable, then Quick Look shall show its compact unsupported/error presentation.
- REQ-F-008: When Quick Look closes, Files shall cancel its text request and discard loaded lines.
- REQ-F-009: When a pinned file changes, Files shall inspect it again and reload text if still supported.
- REQ-F-010: While no lines are loaded, line-navigation commands shall have no effect.
- REQ-F-011: When Quick Look reopens, Files shall reload text starting at the first line.
- REQ-C-001: Files shall retain the 150 ms delay for other fallback transitions and 120 ms outgoing-thumbnail fade.
- REQ-C-002: Files shall retain the 100 KiB text limit, binary check, UTF-8 replacement handling and truncation behavior.
- REQ-C-003: Files shall use the existing worker thread without scanning or prefetching unselected entries.
- REQ-C-004: Files shall reject obsolete text results after close, refresh, retarget or shutdown.
- REQ-C-005: Files shall revalidate the opened file's supported type before reading text-preview lines.
- REQ-C-006: Files shall preserve known unsupported, directory and invalid-selection Space behavior, including Markdown exclusion.
