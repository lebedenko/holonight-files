# Design

PathFinderModel coordinates one traversal worker and a separate ranking worker. Per root/hidden-mode caches carry
completion and dirty metadata. The traversal generation is independent of popup/query generations. A selected
foreground root occupies the pending slot; Home warm-up remains pending until a successful/failed uncanceled
completion. Cancellation is cooperative and generation guards reject queued batches/completion from obsolete jobs.

Complete snapshots remain readable during refresh; incomplete cold snapshots accept progressive worker batches.
The existing HolonightSearch Index API continues to own append/search synchronization and ranking. Completion
replaces the snapshot, clears obsolete missing suppression, and starts a single-shot freshness timer only while
search is active. Path overlap considers ancestor and descendant relationships at component boundaries.
An injected clock and configurable test interval avoid five-minute test waits.

PathIndexStore is a Files-private streaming codec. QSaveFile disables direct-write fallback and provides unique
same-directory temporary files and atomic rename without cache locks. Header fields identify magic/version,
normalized root and SHA-256 of normalized effective exclusions. Each record contains a bounded UTF-8 relative path
and one-byte type; the footer records scan completion and SHA-256 integrity. A restored index is built privately
in bounded batches and accepted only after validation finishes. Startup always traverses after a compatible load.
Disk removal is queued behind canceled traversal on the same worker, before replacement traversal, preventing
an older local writer from republishing after invalidation. Interrupted removal recovers through startup refresh.

The first frameSwapped signal queues an idempotent warmUp call. Task affectedDirs, watcher paths, and successful
inline operations feed invalidation. Finder shutdown cancels jobs and timers; a short event-loop timer observes
worker completion and signals the existing SessionLifecycle barrier. Destruction joins workers as a final safeguard.
