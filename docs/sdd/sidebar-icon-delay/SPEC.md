# Sidebar icon delay

Status: implemented; automated project checks passed; isolated runtime and manual native acceptance remain open.

Approved scope: user-provided implementation plan, 2026-09-28. Internal delay: 150 ms; no setting.

- REQ-F-001: When a selection starts asynchronous loading, the sidebar shall leave its fixed preview area empty until an image, completion, failure or the 150 ms deadline.
- REQ-F-002: When a thumbnail arrives, the sidebar shall display it immediately, including while metadata remains busy.
- REQ-F-003: If loading fails or completes without an image, then the sidebar shall display its existing icon fallback immediately.
- REQ-F-004: While loading has exceeded 150 ms without an image, the sidebar shall display the fallback.
- REQ-F-005: When the selection changes or refreshes in place, the sidebar shall restart its presentation delay and discard previous presentation state.
- REQ-F-006: When a folder or entry requiring no asynchronous preview is selected, the sidebar shall display its icon immediately.
- REQ-C-001: The sidebar shall apply the same delay to theme icons, bundled icons and the question-mark placeholder.
- REQ-C-002: The application shall preserve asynchronous cache/decode/cancellation and Quick Look behavior.
- REQ-C-003: When an existing preview is resized, the sidebar shall retain the displayed image.
