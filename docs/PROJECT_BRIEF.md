# Project brief

HoloNight Files is a keyboard-driven, Vim-like file manager: modal navigation,
selection, search, and commands, with optional places and preview panes. Build a
small standalone application rather than a fork or a desktop-shell feature. The
right half of mockups/moc1.png is visual direction, not a feature commitment.

Future concepts include a places/devices sidebar, a sortable file listing with
metadata/EXIF preview, a NORMAL/command mode with a `:`-prefixed command palette
(mkdir, touch, rename, delete, sort, hidden, cd, open, help), quick look, and
visual (multi-)selection. Proposed bindings shown in the mockup remain proposals
except f, Escape, and q implemented by this scaffold. Avoid an editing suite,
albums, tagging, or a photo database — those belong to sibling applications.

HoloNight Viewer is a separate, already-implemented sibling: a focused static
image viewer. Quick Look is a separate preview proposal for images, text, PDF,
media, directories, and archives shared across HoloNight applications; neither
belongs in Files. Adaptive CSD/SSD belongs in shared holonight-qt work; Files
currently inherits its native-decoration HnApplicationWindow contract.
