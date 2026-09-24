# Parent Directory Navigation Tasks

- [x] T-001 (PDN-001..003): Add `is_parent` and `IsParentRole` to `DirectoryModel`; synthesize parent entry for non-root paths; preserve across refresh diffs.
- [x] T-002 (PDN-004..005): Update `DirectoryProxyModel` sorting and filtering to pin `..` to row 0 and keep it visible under all sort orders and hidden toggles.
- [x] T-003 (PDN-006..007): Route `..` activation (click, Enter, `l`) to `navigateParent()` in `NavigationSession`; guard root parent navigation.
- [x] T-004 (PDN-008..009): Protect `..` from rename, yank, cut, trash, visual selection, and Quick Look; exclude from search matching.
- [x] T-005 (PDN-010): Update preview target derivation and QML delegate for `..`.
- [x] T-006 (PDN-001..010): Update existing unit and integration tests and add dedicated test coverage for parent directory navigation.
- [x] T-007 (all): Run full verification (`task test`, `task check`), update SDD status, README and backlog.

