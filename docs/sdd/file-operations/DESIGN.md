# File operations design

Approved safety revision, 2026-09-10. SPEC.md is authoritative; the user-supplied
implementation plan authorizes this revision and its traceable tasks.

ClipboardRegister remains a plain path list and cut flag owned by DirectoryController.
TaskManager owns a GUI-thread FIFO and one persistent worker thread. Each dispatched
task has an identifier, independent cancellation token and response storage. Every
worker callback carries its task identifier; each prompt additionally has a request
identifier. Only a live matching request accepts a response. Cancellation is checked
after semaphore acquisition. Both cancellation routes discard queued tasks. Trash
requests enter the FIFO unconfirmed and show confirmation only at its front, before
worker I/O. Finishing a task clears only its own prompt.

FileOperationService validates endpoints before mutation using entry inode/device
identity and canonical parent paths. Regular files and symlinks are copied into
exclusive sibling temporary entries, then renamed atomically over a destination only
when overwrite was selected. Otherwise Linux renameat2 RENAME_NOREPLACE is used.
Existing directories are never recursively removed to replace a destination. Directory
overwrite merges, with nested collisions recorded separately as skipped children.
Unsupported types are rejected before opening. Same-device rename supports special
entries; EXDEV goes through safe copy and therefore rejects them.

ItemResult distinguishes failures, cancellation, nested errors and skipped children.
A complete result has none of these. Move cleanup is private and runs only after a
complete copy and a final cancellation check. Incomplete directory transfers retain
the entire source tree, keep successful destination copies and explicitly report that
partial outcome. Top-level summary counts do not count nested errors as extra items.

TrashService returns structured errors (source lookup, creation, validation, metadata,
move), affected path and reason. It validates real directories with expected ownership
and private permissions without repairing existing directories. Shared .Trash requires
the sticky bit and no symlink; its validated user directory falls back to .Trash-$uid.
Home trash is selected only on the source filesystem. Metadata is exclusively created
and completed before a same-device RENAME_NOREPLACE; cancellation/failure removes only
this attempt's metadata. No permanent-delete or copy-to-home fallback exists.

Prompt input takes precedence over editors and shortcuts without changing the mode,
text, cursor or selection. Quick Look closes for prompt visibility. Ctrl+C works from
focused text fields; Escape declines trash but leaves conflicts unresolved. Rendered
keyboard tests cover focus and state preservation; native inspection remains a separate
acceptance check when a compositor is available.

Use installed HoloNight dependencies. All build and test artifacts remain under build/.

## Directory metadata review (2026-09-11)

New copy destinations are created with mode 0700 so read-only source directories
can receive their children. After traversal, source directory timestamps and
permission bits are restored. Existing merge destinations keep their permission
bits and are not assigned source timestamps. Metadata failures make the transfer
incomplete, preventing source cleanup during a move (REQ-F-011, REQ-F-038).
