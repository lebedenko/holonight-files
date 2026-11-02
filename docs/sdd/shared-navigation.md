# Shared keyboard navigation

Files retains Normal/Insert/Visual operations and selection. The shared interpreter owns movement counts and gg; the navigation session and sidebar delegate stable identity to NavigationCursor. Window focus and modal changes reset pending input. Standard arrows now move the list; Vim navigation is an explicit preference. Existing mode regressions opt into Vim navigation in their fixture.

The interaction contract and installed-provider verification order are documented in the
umbrella `docs/shared-keyboard-navigation.md` and provider `holonight-qt/docs/navigation.md`.
Native focus and reachability acceptance remains manual. No deployment is part of this change.

Tab traverses header controls in visual order, then moves between collections and
surrounding controls. Places, file lists and file grids each provide one Tab stop;
arrows (and optional Vim bindings) move their authoritative cursor. File delegates
are not separate Tab stops. Clicking a file focuses its collection. Selection in
Visual mode remains independent of the keyboard cursor.
