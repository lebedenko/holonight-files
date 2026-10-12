# Markdown Quick Look

Approved scope: user-supplied implementation plan, 2026-10-11. That plan authorizes requirements, design and tasks under CONTRIBUTING.md.

- REQ-001: When a verified regular file has text/markdown or text/x-markdown MIME, Files shall offer rendered Markdown Quick Look. When sniffing yields plain text or an empty file, case-insensitive .md/.markdown shall also qualify. Binary content shall be rejected.
- REQ-002: While Quick Look is active, the existing worker shall decode at most 100 KiB as UTF-8, preserve source whitespace and trailing newlines, and retain truncation/error reporting and stale-result cancellation.
- REQ-003: While Markdown loads, Files shall show the full preview frame. Rendered content shall wrap, use HoloNight typography and palette with QMarkdown Polished spacing and decoration, and have vertical keyboard and wheel scrolling without line numbers.
- REQ-004: When closed, Files shall destroy the Markdown view and cancel loading. Reopening shall start at the top; refreshing shall preserve and clamp scroll position. Selection shall remain pinned and Space/Escape shall close the overlay.
- REQ-005: Files shall authorize only PNG/JPEG resources within the document's canonical parent directory and descendants, set the document file URL as baseUrl, deny HTTPS/qrc and symlink escapes, and leave links inert. Denied/failed images shall retain descriptions.
- REQ-006: Files application targets shall consume installed shared QMarkdown 1.1, pinned to b71740c7e0f26b0709f5e929a79e8d01dba56071 in CI. Passive FileBrowser consumers shall remain independent. Runtime staging shall include the backing library, plugin, metadata and license notices.

- REQ-007: While a Markdown preview remains open, Files shall select PolishedDark when the effective surface lightness is below 0.5 and PolishedLight otherwise, retain live host font/color bindings across theme and typography changes, and use preset spacing, H1/H2 separators and padded code surfaces. Inline code shall inherit surrounding size, weight and foreground.

Polished adoption authorized by the user-supplied implementation plan, 2026-10-12.

Non-goals: AI/viewer integration, umbrella installation or pins, publication, source toggle and static embedding.
