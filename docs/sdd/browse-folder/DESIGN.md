# Browse-a-folder design

This design adds the first real model/navigation layer on top of the project-scaffold shell:
`main.cpp` gains directory-argument resolution, and the empty `Item` in `Main.qml` becomes a
two-pane layout (`PlacesPanel.qml` left, `DirectoryListing.qml` right) driven by new C++ types
under `apps/files`. It follows the same worker-thread idiom holonight-viewer already ships in
`apps/viewer/directory_model.{h,cpp}` (a persistent `QObject` moved to a `QThread`, cancellation
via a shared `std::atomic_bool`, staleness via a generation counter, cross-thread hand-off via
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`), extended with incremental batching to
meet REQ-NF-001. Reusing that idiom rather than `QtConcurrent::run` is a deliberate choice: this
codebase already has a proven, tested pattern for exactly this problem, `QtConcurrent::run`'s
one-shot future doesn't naturally support multiple incremental results back to the caller (you'd
still hand-roll the same `invokeMethod` hops to post each batch), and keeping one idiom across
sibling apps lowers the cost of maintaining both. Keep loading/model state in C++, presentation
and key feedback in QML, per `BACKLOG.md`.

`main.cpp` keeps `QCommandLineParser`'s `--help`/`--version` handling unchanged. The positional
argument handling changes: zero or one positional argument is accepted; more than one is still a
usage error printed to stderr with a nonzero exit, matching how `holonight-viewer`'s `main.cpp`
rejects more than one file argument. A single argument is resolved with `QFileInfo`: if it names
an existing, readable directory, that absolute path is the target; otherwise (missing, not a
directory, unreadable, or the zero-argument case) the target falls back to
`QStandardPaths::writableLocation(QStandardPaths::HomeLocation)`, and a human-readable reason
("no folder given", "not a directory", "permission denied", "path does not exist") is captured
alongside it. `main.cpp` constructs a `DirectoryController` before the `QQmlApplicationEngine`
(mirroring `ImageDocument` in the viewer), passes it to QML via
`engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}})`, and — after
`loadFromModule` succeeds, via `QTimer::singleShot(0, ...)` so QML bindings are connected first —
calls `controller.open(resolvedPath, fallbackReason)`, which is what actually starts the async
walk and, when a fallback occurred, populates the status message REQ-C-001 requires. Shutdown
mirrors the viewer too: `QGuiApplication::setQuitOnLastWindowClosed(false)`,
`lastWindowClosed` → `controller.shutdown()`, `DirectoryController::shutdownFinished` →
`QCoreApplication::quit()`, so the worker thread is drained before the process exits instead of
being torn down mid-walk.

`DirectoryModel` (`directory_model.h/.cpp`) is the raw, unsorted, unfiltered list of the current
directory's entries — a `QAbstractListModel` with roles `NameRole`, `IsDirRole`, `SizeRole`,
`ModifiedRole`, `IsHiddenRole` (name starts with `.`), `StatFailedRole`, and `StatErrorRole`. It
exposes C++ accessors `scanning()`, `directoryPath()` and `directoryError()`
(empty when enumeration succeeds), with a shared `changed()` signal; the
controller exposes the corresponding QML properties. Its surface is
`void load(const QString& path)` (starts a fresh walk, cancelling any in-flight one),
`void refresh()` (re-walks the same path in place, used by the filesystem watcher), and
`void shutdown()`. Internally it owns a `QThread` and a persistent worker `QObject`, exactly as
`DirectoryModel` in the viewer does, but `load`/`refresh` do not wait for one final result: the
worker opens the directory using POSIX `opendir`, owns its handle with RAII, and reads
with `readdir`, clearing/checking errno to distinguish EOF from a read error. Both
open and read errors are posted inline while retaining the attempted path. `stat`
follows symlink targets for metadata; after failure, `lstat` only identifies a link
so ENOENT/ENOTDIR can be labelled dangling, while EACCES/ELOOP retain their reason.
Cancellation and generation checks retain the existing incremental delivery protocol.
A private friend test seam snapshots a before-open callback and a read-error trigger
into the worker invocation; real temporary directories remain the source of entries.
Failed refreshes do not remove unseen entries. Successful refreshes compare complete
metadata before emitting dataChanged, batch new entries, and remove contiguous stale
ranges from the back.

Batching is the mechanism that makes REQ-NF-001 achievable rather than aspirational. Naively
calling `beginInsertRows`/`endInsertRows` once per discovered entry against a 10k-entry directory
is 10k signal emissions and 10k proxy-model re-sort insertions before the walk even finishes —
too much overhead to guarantee a 150ms first-paint, and it fights the "remain scrollable and
interactive" half of REQ-NF-001 by saturating the event loop with tiny updates. The opposite
extreme — buffer the entire walk, sort, and insert once, which is what today's
`holonight-viewer` `DirectoryModel` does via `beginResetModel`/`endResetModel` — is rejected for
this stage specifically because REQ-F-001 requires rows to appear incrementally as they're
discovered, and a full-buffer-then-emit design would delay the first visible row until the walk
completes, which is the wrong shape when directories can be large or slow (network-mounted, spun
disks). The chosen middle ground: the worker accumulates discovered entries into a
`QList<DirectoryEntry>` and flushes it — via one `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`
carrying the batch by value — whichever comes first of a count threshold (roughly 250 entries) or
a time threshold (roughly 25ms) since the last flush, plus an unconditional final flush when the
walk ends. On the UI thread, each flush becomes exactly one `beginInsertRows`/`endInsertRows` call
appending the whole batch to `DirectoryModel`'s internal `QList<DirectoryEntry>`. For a 10k-entry directory
that's on the order of a few dozen row-range insertions total, each cheap, with the first batch
targeted at the 150ms first-render limit; actual rendered-frame acceptance is
recorded in VERIFICATION.md. The `DirectoryEntry` struct (name, absolute path, `isDir`, `size`, `QDateTime modified`,
`statFailed`, `statError`) is a plain copyable value type specifically so it can cross the queued
connection by value. Shared synchronization consists of the bounded-delivery semaphore
and the `std::shared_ptr<std::atomic_bool>` cancellation flag (set on `load`/`refresh`/`shutdown`,
polled by the worker between entries and before each flush) and the generation counter compared
only on the UI thread inside the queued callback.

Sorting is a `QSortFilterProxyModel` subclass, `DirectoryProxyModel` (`directory_proxy_model.h/.cpp`),
layered on top of `DirectoryModel` with `dynamicSortFilter = true`, per REQ-F-003's explicit
mandate — hand-rolling incremental re-sort-during-load logic would just reimplement what the
proxy already does for free, including keeping the sort correct as new batches land mid-walk.
`lessThan()` is overridden to put directories before files, then compare names with a
member `QCollator` configured `setNumericMode(true)` and `setCaseSensitivity(Qt::CaseInsensitive)`
for natural, case-insensitive ordering. `filterAcceptsRow()` implements the hidden-files toggle by
reading `IsHiddenRole` from the source model and comparing against a `Q_PROPERTY(bool hiddenVisible ...)`
on the proxy; toggling it calls `beginFilterChange()`/`endFilterChange()`, which is an immediate, cheap re-filter since
no re-walk is needed (REQ-F-014). The sort-order toggle (REQ-F-015) is a second property,
`Q_PROPERTY(bool sortDescending ...)`, flipped by `DirectoryController::toggleSortDirection()`,
which calls `sort(0, order)` and lets the proxy re-sort; a name/size/mtime column toggle is not
implemented in this stage since REQ-F-015 only requires that some reachable toggle exist, and
ascending/descending name order is the cheapest one to get right first. `DirectoryListing.qml`'s
`ListView` binds its `model` directly to this proxy, so every row index the view and the keyboard
handling below deal in is a proxy row, never a source row — `DirectoryModel`'s row order is
walk-discovery order and is never meant to be read directly by QML.

`PlacesModel` (`places_model.h/.cpp`) is a small, synchronous `QAbstractListModel` — no worker
thread, since building it is a handful of `QStandardPaths::writableLocation()` calls, not
filesystem I/O. Its constructor populates a fixed list (Home, Documents, Downloads, Pictures) with
roles `NameRole` and `PathRole`; per REQ-F-008 there is no removable-media discovery, no
`MountService`, and no D-Bus integration in this stage, so the list never changes after
construction and needs no refresh path. `PlacesPanel.qml` is a `ListView` bound to it, with
delegate `onClicked` invoking `controller.open(delegate.path)`.

`DirectoryController` (`directory_controller.h/.cpp`) is the facade QML actually talks to,
registered the way `ImageDocument`/`WindowState` are — here via the `engine.setInitialProperties`
initial-property route used for `ImageDocument`, since it needs to exist before the engine loads
and there's exactly one instance for the process's lifetime, unlike `WindowState`'s
`QML_SINGLETON`. It owns one `DirectoryModel`, one `DirectoryProxyModel` wrapping it, one
`PlacesModel`, and one `QFileSystemWatcher`. Its QML-visible surface: `Q_PROPERTY(QString currentPath ...)`,
`Q_PROPERTY(QString statusMessage ...)` (the REQ-C-001 fallback explanation, and later reused for
watcher-driven refresh notices), `Q_PROPERTY(QString directoryError ...)` (forwarded from
`DirectoryModel`, REQ-F-004), `Q_PROPERTY(bool scanning ...)`, `Q_PROPERTY(DirectoryProxyModel* listing READ ... CONSTANT)`,
`Q_PROPERTY(PlacesModel* places READ ... CONSTANT)`, and `Q_PROPERTY(int cursorRow ...)` (the
NORMAL-mode selection index into `listing`, clamped whenever the row count changes so it never
points past the end mid-walk). Invokables: `void open(const QString& path, const QString& fallbackReason = {})`
(sets `currentPath`/`statusMessage`, calls `DirectoryModel::load`, re-points the
watcher), `void navigateInto(int proxyRow)`, `void navigateParent()` (resolves `QFileInfo(currentPath).absolutePath()` and calls `open()`), `void openEntry(int proxyRow)` (dispatches to `navigateInto`
for a directory row or `QDesktopServices::openUrl(QUrl::fromLocalFile(path))` for a file row —
REQ-F-006), `void toggleHidden()`, `void toggleSortDirection()`, and `bool handleKey(const QString& key)`.

`handleKey` is where the NORMAL-mode keyboard grammar (REQ-F-009 through REQ-F-013) lives, as a
small piece of C++ state rather than QML `Keys.onPressed` bindings, and rather than a full
`VimModeController` state machine. `Main.qml`'s top-level `Keys.onPressed` (or a dedicated
`Keys.onPressed` on `DirectoryListing.qml`, whichever keeps focus handling simplest) forwards raw
key text to `controller.handleKey(event.text || keyName)` and accepts the event when it returns
`true`. Internally `DirectoryController` holds two small pieces of private state: a numeric count
buffer (digits typed before a motion key, REQ-F-011) and a "pending `g`" flag with a short
deadline (a `QElapsedTimer` checked on the next keypress, cleared if it expires or a non-`g` key
arrives) for the `gg` two-press sequence. `j`/`k` move `cursorRow` by `count` (default 1) rows
clamped to `[0, listing.rowCount() - 1]`; `gg` jumps to row 0; `G` jumps to the last row; `h` calls
`navigateParent()`; `l` and `Enter`/`Return` call `openEntry(cursorRow)`. A full `VimModeController`
implementing NORMAL/VISUAL/COMMAND/SEARCH/INSERT is explicitly stage-3 scope per `BACKLOG.md` and
the spec's non-goals list every mode but NORMAL as out of scope here; building that state machine
now, for a single mode, would be speculative generality. Putting the count/chord parsing in C++
rather than QML property/state juggling is a deliberate choice too: it's the same kind of small,
input-driven logic `ImageDocument`/`ClipboardController` already put in C++ in the sibling app,
and it is directly unit-testable with `QTest`/GTest the way QML `Keys` handlers are not.

The filesystem watcher is a single `QFileSystemWatcher` owned by `DirectoryController`, watching
exactly `currentPath` — REQ-F-007 mandates one watch per open directory, not per entry, and every
call to `open()` first clears any existing watched path before adding the new one, so there is
never more than one watched path system-wide per window. On `directoryChanged`, the controller
calls `DirectoryModel::refresh()`, which re-walks the same path (same batching path as an initial
`load()`) and, on the UI-thread flush side, diffs the freshly-walked entry set against the model's
current contents by name — removed names become targeted `beginRemoveRows`/`endRemoveRows` calls
and newly-appeared names become `beginInsertRows`/`endInsertRows` calls, rather than a blanket
`beginResetModel`/`endResetModel` — so a `touch`/`rm` elsewhere doesn't reset scroll position or
the proxy's sort work for entries that didn't change. Two risks are worth flagging without being
in scope to fix here. First, `QFileSystemWatcher`'s backing inotify instance has a
`fs.inotify.max_user_watches` ceiling; since this stage keeps strictly one watch per open window
regardless of directory size, a single window is cheap, but a user running many `holonight-files`
windows simultaneously could approach the limit on a constrained system — flagged during
requirements grilling as non-blocking, and still non-blocking here since nothing in this design
adds more than one watch per window. Second, individual broken/circular symlinks are handled as
ordinary REQ-F-005 stat failures (`ELOOP` from target-following `stat`), and since the walk is single-level and
non-recursive, the classic recursive-symlink-loop hazard (a recursive walker chasing a
cycle forever) does not apply to this stage; it would need re-examining if a later stage adds
recursive listing or a tree view.

Error surfacing is deliberately two-layered so QML never has to guess which failure it's looking
at. Whole-directory failure (REQ-F-004) is model-level state: `DirectoryModel::directoryError`
(forwarded through `DirectoryController::directoryError`) is non-empty exactly when the directory
itself couldn't be opened, and `DirectoryListing.qml` shows an inline error state (reusing
`HnEmptyState`/`HnLoadingState`'s visual language, no navigation elsewhere) whenever it's set,
while `controller.currentPath` — always assigned before the walk starts, never cleared on failure
— keeps the attempted path visible in a small path/status label above the listing (there is no
shared `Breadcrumb` control in `holonight-qt` today, so this is a plain `HnLabel` bound to
`currentPath`/`statusMessage`, not a new shared component). Per-entry failure (REQ-F-005) is
row-level state: `StatFailedRole`/`StatErrorRole` on the affected row, rendered by
`DirectoryListing.qml`'s delegate as a placeholder marker (muted name, a small error glyph or
`HnStatusIndicator` in its warning state) instead of being filtered out, so a single bad entry
never hides the rest of an otherwise-readable directory.

Fixtures for `DirectoryModel`/loader tests live under `tests/`, following the same shape as
`holonight-viewer`'s `folder_browsing_test.cpp`: `QTemporaryDir` under a project-local fixture
directory (an analogous `FILES_FIXTURE_DIR` compile definition), no mocked filesystem, per
REQ-C-002 — empty directory, 10,000+ entry directory for the REQ-NF-001 timing/frame-rate
assertion, a symlink into a `chmod 000` directory for the target-stat permission-denied path, a deliberately dangling
symlink, and Unicode-named entries (combining characters, non-Latin scripts, emoji) to exercise
`QCollator` natural sort and role rendering. Per REQ-C-003, the permission-denied assertion is
skipped or handled differently under `geteuid() == 0` (CI containers commonly run as root, which
bypasses the `chmod 000` check entirely), with that constraint noted directly in the test code
rather than assumed silently.


Review implementation: initial-directory resolution moves to an internal helper shared
by main and tests, retaining parser excess-argument rejection. C collation selects
English/US locally on QCollator; numeric/case-insensitive sorting and descending
reversal stay unchanged. ASCII counts saturate at INT_MAX, movement uses qint64,
and `.` / `s` consume pending grammar state before toggling. Production-window key
tests cover navigation, URL interception, and toggles; watcher tests mutate real files.
Native performance acceptance observes frameSwapped after a visible delegate has been
synchronized, frame gaps while scrolling, and posted input latency while loading.
Use 10k+ entries, increase fixture size for adequate samples, and record insufficient
samples as inconclusive. Offscreen screenshots cover populated and inline-error states.
CI runs both C.UTF-8 and en_US.UTF-8 as an unprivileged user. Provider and image pinning
are deferred.

Native review measurements exposed queued-batch starvation at 192k entries. Bound
worker-to-UI delivery to two outstanding batches with a cancellable semaphore wait.
This keeps queued sorting work finite without blocking the UI thread; cancellation
lets shutdown finish even if the event loop no longer drains batches.

The listing explicitly positions the current index inside its viewport on keyboard
motion; animated highlight tracking must not leave the cursor outside the viewport
during continuous count-prefixed input while batches are arriving.
