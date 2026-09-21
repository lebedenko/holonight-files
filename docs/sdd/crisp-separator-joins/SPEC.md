# Crisp separator joins — holonight-files

Status: Accepted for implementation, 2026-09-21. Work package CS-002.
Baseline: `46557c287f37c3d2d6cafad17d37d834bb68b1b5`. The approved user plan authorizes this scope.

- REQ-001: Adopt HnSeparator integer physical thickness, inherited opacity, borderPassive default,
  and Leading/Center/Trailing boundary alignment without caller DPR calculations.
- REQ-002: Bottom/right boundaries shall be trailing aligned; top/left boundaries leading aligned.
  Preserve unrelated worktree edits, application architecture and existing style overrides.
- REQ-003: Verify imports and relevant application regressions against explicitly staged modified Qt.
- REQ-004: Files shall restore the shared HnHeaderBar divider and remove diagnostic two-pixel overrides.
- REQ-005: Column rules shall be children of the column header, positioned from actual column cells,
  visible with their columns, and end at the bottom rule's leading boundary without overlap.
- REQ-006: Header/sidebar, header/column, column/bottom and sidebar/footer joins shall have no gaps,
  double compositing or clipping. Test complete junction regions, wide/narrow windows, both renderers,
  and DPR 1/1.25/1.5/1.75/2. Keep native visual acceptance pending until observed.
- REQ-007: Record actual executable imports, loaded provider identity, renderer and DPR. Explicitly
  rebuild/install the dirty provider into the local prefix; HEAD-only freshness is insufficient.
