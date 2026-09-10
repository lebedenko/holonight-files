# Vim modal editing design

The approved review plan defines Stage 3: NORMAL/VISUAL/SEARCH/INSERT, synchronous
filesystem actions, no COMMAND palette or VISUAL filesystem actions.

DirectoryController owns integration and executes captured InsertCommitResult paths.
VimModeController owns mode, validation, selection and search values; it does not
execute filesystem actions. QML text fields call controller commit/cancel methods.
Creation uses QFile WriteOnly|NewOnly; validation includes dangling symlinks.
Rename uses Linux renameat2 with RENAME_NOREPLACE so directories and links are
renamed atomically without replacing a racing destination. Touch uses Linux utimensat with UTIME_OMIT/UTIME_NOW and AT_SYMLINK_NOFOLLOW.
Failures retain editing state and report translated operation-specific errors.

DirectoryModel suspends before rename/create and placeholder insertion. Suspension
cancels the walk and rejects data while completion bookkeeping still runs. Refresh
requests are deferred. Commit/cancel removes the placeholder before resume triggers
one fresh diff; navigation clears modal state then loads afresh. Generation checks
reject obsolete data; the existing two-slot bounded delivery mechanism remains.
Sort/filter toggles are disabled during INSERT. Proxy sorting uses a deterministic
filename tie-break and compensates placeholder before/after for descending order.

Search stores editable and committed queries separately. Controller listing revisions
invalidate cached match rows on row/name/layout/filter/directory changes. Repetition
recomputes before using indices. Escape restores filename identity with row fallback.
Current SEARCH positions are exposed to QML and rendered as contiguous matched/plain
runs using installed HnLabel controls and theme tokens in local delegate content.
Literal filenames, accessibility, metadata, clipping and editor remain intact.

FuzzyMatcher already has a subsequence prefilter. Its scoring DP is quadratic in
candidate length (times query length), with predecessor storage for positions.
Large-list search latency remains a measurement concern, not an assumed guarantee.

Navigation centrally clears editing, visual/search state, remembered query and key
chords before load; a navigation signal restores listing focus. NORMAL Escape closes
Quick Look first and otherwise retains fullscreen exit. n/N are literal in SEARCH
and repeat the committed search only in NORMAL.
