# Files icon migration design

Places, listing, preview pane, and Quick Look continue to request the Files image provider for theme icons. Each `HnIcon` using that provider sets `rendering: HnIcon.Original`. Quick Look switches to `Semantic` when its source changes to a bundled fallback. The shared Qt API owns provider URL validation; Files keeps its existing fallback state and icon names.
