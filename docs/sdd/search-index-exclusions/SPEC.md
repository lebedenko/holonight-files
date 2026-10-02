# Requirements

- E1: When scanning, Files shall exclude directories named `.git`, `.venv`, `.codex`, `.claude`, `.agents`,
  `node_modules`, `__pycache__`, and matching `build*` at any depth before indexing or descending.
- E2: Files shall exclude exact home `.cache`, resolved XDG cache, and resolved XDG data `Trash` subtrees,
  deduplicating rules and observing path-component boundaries.
- E3: When hidden mode is enabled, useful dot directories including `.config`, `.local/share`, and `.local/state`
  shall remain searchable unless excluded. Hidden paths remain opt-in. Files shall not read `.gitignore`.
- E4: When the selected root is excluded or inside an excluded subtree, Files shall permit that root and
  ignore ancestor exclusions while applying exclusions to entries beneath it.
- E5: Files shall union built-ins with `[search] exclude_paths` and `exclude_directory_patterns` string lists.
  Paths shall be absolute or start with `~/`, need not exist, and shall not expand other variables.
  Patterns shall match whole directory basenames case-sensitively, with only `*` and `?` special and `/` invalid.
- E6: When the finder opens, Files shall reload search settings without writing config.toml or reloading other settings.
  Missing config shall use built-ins. Invalid rules shall be diagnosed and ignored. Unreadable or malformed config
  shall retain the last successful policy, initially built-ins, and display errors in the finder and diagnostics.
- E7: When effective policy changes, Files shall cancel old work, reject stale callbacks, discard both hidden and
  visible caches, and rebuild. Equivalent policy shall retain completed scan reuse.
- E8: Traversal shall preserve cancellation, batching, unreadable-path handling and directory-symlink behavior.
