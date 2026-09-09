# Project brief

HoloNight Files is a keyboard-driven, Vim-like file manager: modal navigation,
selection, search, and commands, with optional places and preview panes. Build a
small standalone application rather than a fork or a desktop-shell feature. The
right half of mockups/moc1.png is visual direction, not a feature commitment.

The product definition is **a graphical Vifm/Yazi expressed as a HoloNight
application, not a Dolphin alternative**: a real modal state machine (NORMAL,
VISUAL, COMMAND, SEARCH, INSERT) that operations compose against, rather than
`hjkl` bound onto a conventional mouse-oriented file manager. This is the niche
GUI file managers tend to miss and terminal Vim-style managers can't reach.

Future concepts include a places/devices sidebar, a sortable file listing with
metadata/EXIF preview, a NORMAL/command mode with a `:`-prefixed command palette
(mkdir, touch, rename, delete, sort, hidden, cd, open, help), quick look, and
visual (multi-)selection. Proposed bindings shown in the mockup remain proposals
except f, Escape, and q implemented by this scaffold. Avoid an editing suite,
albums, tagging, or a photo database — those belong to sibling applications.

v1 is scoped to the local filesystem only: no SFTP/SMB/MTP, archive-as-directory,
plugins, embedded terminal, or Git UI. The filesystem/task backend is owned in
explicit layers (navigation/model/filesystem/task/preview), not a thin wrapper
over `QFileSystemModel`, because recursive operations, conflicts, cancellation,
and progress on real (multi-GB) transfers are the hard part, not listing files.
Search filtering uses an in-process fuzzy matcher tuned like fzf's scoring
(consecutive runs, word-boundary bonus, gap penalty) rather than shelling out to
an external tool; thumbnails are read/written through the freedesktop Thumbnail
Managing Standard cache (`$XDG_CACHE_HOME/thumbnails/`) so caches stay
interoperable with other desktop applications instead of a Files-private cache.
Both start as in-tree components and only move into a shared HoloNight library
once a second consuming application actually needs them.

HoloNight Viewer is a separate, already-implemented sibling: a focused static
image viewer. Quick Look is a separate preview proposal for images, text, PDF,
media, directories, and archives shared across HoloNight applications; neither
belongs in Files. Adaptive CSD/SSD belongs in shared holonight-qt work; Files
currently inherits its native-decoration HnApplicationWindow contract.
