# Search index lifecycle requirements

Approved scope: the user-provided “Files search warm-up, persistence, and invalidation” plan (2026-10-02).

- L1: After the first window frame, Files shall warm the visible Home index once, loading exclusions before work.
- L2: Closing search shall stop ranking and refresh timers while indexing continues. Opening search shall attach
  to matching work. Foreground requests shall preempt unrelated traversal and unfinished Home work shall resume.
- L3: Cold scans shall publish progressive results. Refresh shall retain complete snapshots until atomic replacement.
  Canceled or invalidated generations shall never publish completed snapshots or obsolete caches.
- L4: Files shall persist only visible Home files and directories in its XDG cache, using a versioned, validated,
  integrity-checked binary format and atomic replacement. Relative/unset/empty XDG cache shall use ~/.cache.
  Cache I/O and reconstruction shall run on workers, without library API changes or new dependencies.
- L5: On launch, Files shall restore compatible cached results and always refresh them. Cache failures shall
  diagnose problems and preserve ordinary in-memory search. Concurrent writers shall use independent temporary files.
- L6: Open search shall refresh dirty indexes immediately and completed indexes after five minutes. Closed search
  shall not periodically refresh. Refresh failure shall retry at the next interval.
- L7: Affected task directories, successful inline edits, and watched-path changes shall invalidate overlapping
  roots using component boundaries. Active changes shall debounce for 250 ms and cancel affected generations.
- L8: Missing results shall be suppressed until replacement. Exclusion changes shall discard incompatible snapshots
  and cancel jobs; malformed configuration shall retain the last valid policy. Disk invalidation shall be asynchronous.
- L9: Finder shutdown shall participate in the application worker barrier without blocking the GUI thread.
- L10: Result limits, ranking, highlighting, navigation, and scanning presentation shall be preserved.

No settings, recursive monitoring, daemon, provider changes, publication, commits or umbrella pins are included.
