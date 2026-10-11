# Markdown Quick Look

Approved scope: user-supplied implementation plan, 2026-10-11. That plan authorizes requirements, design and tasks under CONTRIBUTING.md.

- REQ-001: When a verified regular file has text/markdown or text/x-markdown MIME, Files shall offer rendered Markdown Quick Look. When sniffing yields plain text or an empty file, case-insensitive .md/.markdown shall also qualify. Binary content shall be rejected.
- REQ-002: While Quick Look is active, the existing worker shall decode at most 100 KiB as UTF-8, preserve source whitespace and trailing newlines, and retain truncation/error reporting and stale-result cancellation.
- REQ-003: While Markdown loads, Files shall show the full preview frame. Rendered content shall wrap, use HoloNight typography/palette/spacing, and have vertical keyboard and wheel scrolling without line numbers.
- REQ-004: When closed, Files shall destroy the Markdown view and cancel loading. Reopening shall start at the top; refreshing shall preserve and clamp scroll position. Selection shall remain pinned and Space/Escape shall close the overlay.
- REQ-005: Files shall authorize only PNG/JPEG resources within the document's canonical parent directory and descendants, set the document file URL as baseUrl, deny HTTPS/qrc and symlink escapes, and leave links inert. Denied/failed images shall retain descriptions.
- REQ-006: Files application targets shall consume installed shared QMarkdown 1.0, pinned to 595ca4fea32155766c2eafb3b670995d2ea75a2f in CI. Passive FileBrowser consumers shall remain independent. Runtime staging shall include the backing library, plugin, metadata and license notices.

Non-goals: AI/viewer integration, umbrella installation or pins, publication, source toggle and static embedding.
