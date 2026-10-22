# Icon view and selection-first mouse interaction

Files owns one session-only presentation choice: List (default) or Icons. The
controller exposes the choice, switching guard, parent availability and grid
geometry; the application proxy filters parent entries only in icon mode. Both
presentations therefore use the same row coordinates for preview, editing,
search and operations. Switching maps the selected source index into the new
proxy; a filtered parent falls back to the first entry. Switching does not
cancel an asynchronous navigation restore or reset search, sorting or history.

The header reserves space for two exclusive view buttons before Close and lets
the listing-aligned breadcrumbs shrink. Up uses the existing parent navigation
and restores the folder exited. Header actions require NORMAL without a task
prompt, finder or Quick Look; Up discards pending counts and is disabled at root.

A Loader instantiates only the active presentation. List retains its detailed
columns and gutter. Icons reuse Browser.DirectoryGrid with responsive columns
near 128px, 64px themed icons and two-line centered filenames. Each presentation
owns scrolling, inline editing, drag/drop hit testing and edge scrolling.

Single clicks select and retarget preview; VISUAL clicks extend the existing
range. Double clicks activate only in NORMAL. Other modes and modal UI retain
interaction ownership. Icons map h/l and Left/Right to adjacent entries and j/k
and Up/Down to displayed rows. Counts repeat spatial movement using the shared
grid boundary rules, including incomplete final rows. Half-page movement uses
visible grid rows times columns. List mappings and pinned Quick Look stay as
before; gg/G, search, VISUAL and editing retain shared controller behavior.

Acceptance uses proxy/controller regressions and offscreen window interaction
checks, followed by QML checks, task test and task check. Native appearance,
double-click behavior, drag/drop and focus must be checked manually; native
pointer, focus and UI automation are prohibited. No dependencies, provider
contracts, installation paths, publication or umbrella pins change.
