# Design

The TOML adapter recognizes homogeneous string arrays, including empty lists; mixed arrays retain their unsupported
value type and are rejected as a whole by the typed registry. SearchSettings declares both search lists alongside
GeneralSettings, so startup diagnostics recognize the new section without changing other settings' loading behavior.

SearchExclusionPolicy is a private value snapshot: cleaned, sorted, deduplicated path and pattern rules plus compiled,
anchored regular expressions with all characters escaped except `*` and `?`. Scans capture the snapshot by value.
Exact path rules use component boundaries; rules containing the selected root are bypassed. Directory name rules
are applied only to enumerated entries, naturally permitting explicit named roots and descendants.

PathScanner checks the snapshot before appending candidates or enqueueing directories. PathFinderModel reloads at
start(), retains policy on parser/read failure, and keeps configuration diagnostics separate from transient scan/query
errors. Equivalent sorted rules preserve cache reuse. Changed rules call stop(), clear both cache maps and completion
sets, and then start the selected root, preserving existing serial guards for stale scan and rank callbacks.

No watcher, persistent index, configuration writer, Settings UI or provider change is introduced.
