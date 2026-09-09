# Browse a folder requirements

Status: requirements authorized by the user's direct instruction to lift CLI
positional directory arguments, implement asynchronous directory listing with
natural sorting, a fixed-places sidebar, keyboard-driven navigation (NORMAL mode
only), and live filesystem watching, culminating in a responsive file browser
that meets performance targets on large directories.

| ID | EARS requirement |
| --- | --- |
| REQ-C-001 | The system shall accept an optional positional directory-path argument on the command line. If the argument names a readable, existing directory, the system shall open the application with that directory loaded. If the argument is missing, not a directory, not readable, or inaccessible, the system shall open the application with the user's home directory and display a status message explaining why the requested directory was not opened. |
| REQ-F-001 | The system shall implement a DirectoryModel that populates asynchronously on a worker thread (e.g., QtConcurrent or a dedicated QObject moved to a thread). The model shall walk the target directory using worker-side POSIX opendir/readdir with explicit open/read error detection and insert rows incrementally as entries are discovered, never blocking the UI thread. |
| REQ-F-002 | While a DirectoryModel is populating, the system shall remain scrollable and interactive; the view shall not freeze or delay input processing due to directory-walk activity. |
| REQ-F-003 | The system shall sort directory entries by filename using QCollator with natural (numeric-aware) and case-insensitive ordering. Directories shall sort before files by default. Sorting shall be implemented via a QSortFilterProxyModel with dynamicSortFilter=true layered on top of the DirectoryModel, so re-sorting during incremental load is handled by the proxy without hand-rolled logic. |
| REQ-F-004 | If the target directory itself is unreadable or inaccessible (e.g., permission denied, or the directory disappeared between validation and open), the system shall show an inline error state in the listing area (not a silent redirect elsewhere) and keep the attempted path visible in a breadcrumb, status bar, or equivalent location so the user can see what was attempted. |
| REQ-F-005 | If an individual entry inside an otherwise-readable directory fails to stat (e.g., permission denied while following a symlink target with stat, broken symlink, or deleted mid-walk due to a race condition), the system shall include that entry in the listing with an error or placeholder marker rather than silently dropping it. |
| REQ-F-006 | When the user presses `l` or Enter on a directory entry, the system shall navigate into that directory, replacing the current listing with the new directory's contents. When the user presses `l` or Enter on a non-directory (regular file) entry, the system shall launch it via the platform default handler (QDesktopServices::openUrl on a file:// URL, equivalent to xdg-open) in an external process. |
| REQ-F-007 | The system shall maintain exactly one QFileSystemWatcher instance watching the currently-open directory's path (not per-entry watches). When a filesystem change notification is received, the system shall refresh the listing by re-diffing against the watched directory's current contents. |
| REQ-F-008 | The system shall populate a PlacesModel with a fixed, hardcoded list of standard places (Home, Documents, Downloads, Pictures, or equivalent locations derived from QStandardPaths) for this stage. The Places sidebar shall display this model. No removable-media discovery, device enumeration, MountService, or D-Bus integration shall be implemented in this stage. |
| REQ-F-009 | While in NORMAL mode, when the user presses `j`, the system shall move the selection cursor down one row. |
| REQ-F-010 | While in NORMAL mode, when the user presses `k`, the system shall move the selection cursor up one row. |
| REQ-F-011 | While in NORMAL mode, when the user presses a numeric count of ASCII 0–9 digits (e.g., 5), accumulated with saturation at INT_MAX and applied using widened arithmetic before clamping (zero remains a zero-row motion), followed by `j` or `k`, the system shall move the selection cursor down or up by that many rows in a single motion. |
| REQ-F-012 | While in NORMAL mode, when the user presses `g` then `g`, the system shall jump the selection to the first row of the listing. When the user presses `G`, the system shall jump the selection to the last row of the listing. |
| REQ-F-013 | While in NORMAL mode, when the user presses `h`, the system shall navigate to the parent directory of the currently-open directory (equivalent to moving to the parent entry in the directory tree). |
| REQ-F-014 | The system shall provide a keyboard-reachable `.` toggle binding, consuming pending counts/chords, to show or hide hidden files (dotfiles) in the current session. When activated, the toggle shall immediately refresh the listing to include or exclude entries whose names begin with a dot. No persistence of this preference is required in this stage. |
| REQ-F-015 | The system shall provide a keyboard-reachable toggle binding to change the current sort order (e.g., toggling between ascending and descending filename order, or between name/size/modification-time columns). The binding shall be `s`, consuming pending counts/chords. |
| REQ-NF-001 | When the user navigates to a local directory containing 10,000 or more entries, the system shall display the first visible rows within 150 milliseconds of the navigation action. The view shall remain scrollable and interactive at ordinarily interactive frame rates (no <10fps stuttering or blocking) while the background directory walk is still populating the remaining entries. Acceptance: on native Wayland, measure navigation-to-first-rendered-frame containing a visible populated delegate (≤150ms), frame gaps during active scrolling (≤100ms), and posted-key-to-cursor-update latency while loading (≤100ms); insufficient interaction samples are inconclusive. Measure watcher refresh separately. Use a reasonably configured native test environment and a 10k+ entry fixture, increasing size until sufficient interaction samples are obtained. |
| REQ-C-002 | Fixtures for DirectoryModel and loader behavior verification shall be created using real filesystem artifacts via QTemporaryDir (not a mocked or injected filesystem abstraction). Fixtures shall include: an empty directory, a large directory (10,000+ entries) for the performance criterion (REQ-NF-001), a directory containing an entry with all permissions removed (chmod 000) for the permission-denied target-stat case through a symlink into a non-searchable directory (chmod 000 on a regular file alone does not prevent stat), a directory containing a deliberately broken/dangling symlink, and a directory containing Unicode-named entries (e.g., combining characters, non-Latin scripts, emoji). |
| REQ-C-003 | When a test fixture requires checking permission-denied behavior via target stat or directory open with directory permissions removed (chmod 000), the test shall skip or handle that assertion differently when running as root (geteuid() == 0), since the root user bypasses filesystem permission bits. This testing constraint shall be explicitly noted in the test code. |

Non-goals: search and filtering (`/`), visual or multi-selection (`v`/`V`), file
operations (create/rename/delete/copy/move/trash), metadata and EXIF preview
pane, Quick Look overlay, `:` -prefixed command palette, any vim mode other
than NORMAL (VISUAL, COMMAND, SEARCH, INSERT are stage 3+), removable-media or
device discovery and places, back/forward navigation history, multi-register
clipboard (`"a`), marks (`ma`), or any non-local-filesystem source (SFTP, SMB,
MTP, archive-as-directory).

Review scope authorized by the supplied implementation plan: C-locale collation uses
an explicit English/US QCollator locale without changing the global locale. Failed
refresh enumeration retains unseen rows; unchanged metadata emits no dataChanged.
Native rendering acceptance is opt-in; offscreen tests are regression coverage only.
