# Device Actions — Requirements Specification (EARS Format)

## Status

**Implemented and verified 2026-09-26** (build, test, format-check, tidy, qml-lint green; native acceptance passed, T-014). Authored 2026-09-25 from an investigation of the actions currently offered by the Devices
panel. Supersedes parts of [udisks2-storage](../udisks2-storage/SPEC.md) R3 and R4 — see
[Amendments to udisks2-storage](#amendments-to-udisks2-storage).
The removal-scope, empty-optical and row-ordering cases are implemented; acceptance results are
recorded in [VERIFICATION.md](VERIFICATION.md).

Baseline: `1cc5b2f`.

## Context: the existing Devices code is a proof of concept

The Devices panel shipped in `7b15eee` exists primarily as a consumer-side harness that was needed to
build and exercise the shared `StorageController` in `holonight-system-services`. It is not a
finished Files feature. In particular:

- The action set was derived mechanically from UDisks2 capability flags rather than designed.
- `DevicesPanel` was injected into the sidebar as a second independently scrolling list occupying a
  fixed 50% of its height (`Main.qml:97-113`), rather than composed into the existing sidebar.
- Rows carry only a name and a state string; there is no device iconography and no capacity
  information.

This specification therefore treats the existing panel as replaceable. Requirements that restate
current behaviour do so only to keep it verified through the restructuring; no part of the present
layout or action set is preserved for compatibility.

## Overview

The Devices panel currently derives its buttons directly from the UDisks2 capability flags
`Ejectable` and `CanPowerOff`, one button per advertised capability, plus an Unmount button. Because
UDisks2 advertises `Ejectable` for anything on a removable bus rather than for drives with a physical
eject mechanism, a USB stick presents an "Eject" button that performs no meaningful action, and an
internal SATA disk would present drive-removal buttons if its firmware ever advertised them.

This specification replaces capability-per-button with a two-step model:

1. A drive is **classified** — Optical, External, or Internal — which decides *whether* a removal
   control exists and *which glyph* it carries.
2. A removal **verb** is derived from whether the drive holds a removable medium or is itself the
   removable object, which decides *what the control does*.

The result is at most one removal-family control per row, matching the convention established by
Nautilus/GVfs and Dolphin/Solid, while degrading honestly on hardware that advertises neither
drive-level capability.

## Scope

### In Scope

- Drive classification (Optical / External / Internal) in `apps/files/storage/storage_policy.h`
- Removal-verb derivation with capability fallback
- Removal-control presentation in `apps/files/qml/places/DevicesPanel.qml` — one control per row
- Removal of the power-off confirmation prompt and its model state
- Row visibility rules for empty media slots and empty optical drives
- Drive-level grouping of rows via a list section label
- An `x` keybinding for the removal action on the focused device row
- Bundled eject and device-class glyph assets
- Restructuring the sidebar into a single scroll area spanning Places and Devices
- A three-column device row: device icon, name/capacity-bar/capacity-text stack, removal control
- Filesystem capacity acquisition and its threshold colouring
- Unit coverage through the existing `FakeStorage` backend

### Not in Scope

- Any change to `holonight-system-services` — `StorageDrive` already carries every field required
- Any change to `holonight-qt` — a cross-repository change would require the umbrella initiative
  workflow described in the umbrella `AGENTS.md`
- Automatic mounting on device arrival, unlock of encrypted volumes, disk administration
- Whole-device power-off of a multi-slot reader (see [REQ-F-011](#req-f-011--whole-device-power-off-is-not-offered))
- A tree/expander presentation of drives and their volumes
- Capacity display for Places or bookmark rows
- Changes to `StorageFilter::infrastructure()` partition-type and mount-path filtering
- Changes to the Shell consumer of `StorageController`
- Replacing the "Not mounted" state text (tracked separately)

## Terminology

| Term | Meaning |
|---|---|
| **Drive** | A `StorageDrive`, one per SCSI LUN. A multi-slot card reader yields one Drive per slot. |
| **Volume** | A `StorageVolume` — a block device that may carry a filesystem. |
| **Row** | One visible entry in the Devices panel. Normally a volume; an empty optical drive is a drive-backed row. |
| **Removal control** | The single Unmount / Eject / Remove button on a row. |
| **Removal verb** | The `StorageOperation` the removal control invokes. |

---

## Functional Requirements

### Classification

#### REQ-F-001 — Optical classification

**State-driven**: While a drive reports `Optical` as true, the system shall classify that drive as
Optical, irrespective of its reported connection bus.

**Acceptance**:
- A synthetic drive with `optical = true` and `connectionBus = ""` classifies as Optical, not Internal.
- A synthetic drive with `optical = true` and `connectionBus = "usb"` classifies as Optical, not External.

---

#### REQ-F-002 — External classification

**State-driven**: While a drive is not classified as Optical and reports a non-empty `ConnectionBus`,
the system shall classify that drive as External.

**Acceptance**:
- Classification is true for `connectionBus` values `"usb"`, `"ieee1394"` and `"sdio"`.
- Classification is true for any other non-empty value, without that value appearing in the source.
- Classification does not consult `Removable`, `Ejectable` or `CanPowerOff`.

---

#### REQ-F-003 — Internal classification

**State-driven**: While a drive is classified as neither Optical nor External, the system shall
classify that drive as Internal.

**Acceptance**: A synthetic drive with `connectionBus = ""`, `optical = false` classifies as Internal
even when `removable`, `canEject` and `canPowerOff` are all true.

---

### Removal verb

#### REQ-F-004 — Medium-removable drives prefer Eject

**State-driven**: While a drive reports `MediaRemovable` as true and advertises `Ejectable`, the
system shall select Eject as the removal verb for rows backed by that drive.

**Rationale**: The removable object is the medium, not the drive. `Drive.Eject` releases one slot or
tray and leaves the enclosing device enumerated.

**Acceptance**: A card-reader slot (`mediaRemovable = true`, `canEject = true`, `canPowerOff = true`)
selects Eject, not PowerOff.

---

#### REQ-F-005 — Self-removable drives prefer PowerOff

**State-driven**: While a drive reports `MediaRemovable` as false and advertises `CanPowerOff`, the
system shall select PowerOff as the removal verb for rows backed by that drive if its
controller-derived scope contains no other drive (REQ-F-011).

**Acceptance**: A USB stick (`mediaRemovable = false`, `canEject = true`, `canPowerOff = true`)
selects PowerOff, not Eject.

---

#### REQ-F-006 — Safe capability fallback

**Unwanted behaviour**: If PowerOff is preferred for a self-removable drive but unavailable, then
the system shall select Eject when advertised. If Eject is preferred for a medium-removable drive
but unavailable, the system shall offer Unmount of the row's mounted volume when available, or no
control. PowerOff shall not substitute for Eject on a medium-removable drive.

**Acceptance**: `mediaRemovable = false`, `canPowerOff = false`, `canEject = true` selects Eject.
A mounted reader slot with `mediaRemovable = true`, `canEject = false`, `canPowerOff = true` offers
Unmount, never PowerOff, whether or not a sibling drive is currently enumerated.

---

#### REQ-F-007 — Capability fallback to Unmount

**Unwanted behaviour**: If no permitted drive-level verb can be selected, then the system shall
select Unmount of that row's own volume as the removal verb, provided the volume advertises
`canUnmount`.

**Rationale**: Externally attached drives that UDisks2 cannot power off — eSATA docks,
Thunderbolt-attached NVMe — must still offer the one operation that is genuinely available.

**Acceptance**: An External-classified drive with both drive capabilities false and a mounted volume
presents a working control whose invocation reaches `StorageController::unmount`.

---

#### REQ-F-008 — No verb means no control

**Unwanted behaviour**: If no removal verb can be selected for a row, then the system shall present
no removal control on that row.

**Acceptance**: A row whose drive advertises no drive-level capability and whose volume reports
`canUnmount = false` renders with no removal-family button; no disabled placeholder is shown.

---

#### REQ-F-008a — Unmounted volumes offer no removal

**State-driven**: While a volume row's volume has no mount point, the system shall present no removal
control on that row; activating it mounts and opens it instead. An empty optical drive's drive-backed
row keeps its Eject control when Eject is available (REQ-F-017).

**Acceptance**: Unmounted External, Optical and card-slot volume rows report no removal verb, render no
removal button, and `x` on them records no backend call.

**Added** after native acceptance (2026-09-25).

---

### Action presentation

#### REQ-F-009 — Internal drives offer Unmount only

**State-driven**: While a row's drive is classified as Internal, the system shall offer Unmount as
that row's only removal-family control when the mounted volume can be unmounted.

**Acceptance**: An Internal-classified drive that advertises `canEject` and `canPowerOff` presents
an Unmount control and no Eject or Power off control; neither `StorageController::eject` nor
`powerOff` is reachable from that row.

---

#### REQ-F-010 — External and Optical rows offer at most one control

**State-driven**: While a mounted volume row's drive is classified as External or Optical and a safe
removal verb is available, the system shall present exactly one removal-family control on that row.
An empty optical drive-backed row presents one only when Eject is available.

**Acceptance**:
- A mounted External or Optical fixture with an available removal verb yields one visible button.
- An unmounted volume or an empty optical drive without Eject yields none (REQ-F-008, REQ-F-008a).
- No separate Unmount control is rendered alongside it, including while the volume is mounted.

---

#### REQ-F-011 — Whole-device power-off is not offered

**Ubiquitous**: The system shall not offer PowerOff when its controller-derived removal scope
contains a drive other than the one backing the activated row.

**Rationale**: Powering off a multi-slot reader removes every sibling slot and requires physical
reconnection to recover. Eject is preferred for each slot, but capability fallback alone does not
prevent sibling-wide PowerOff. The presentation decision must also check removal scope.

**Acceptance**: With a four-slot reader fixture whose slots share a `SiblingId`, no control invokes
`StorageController::powerOff` for any slot, including when a populated slot has `canEject = false`
and `canPowerOff = true`. A multi-drive enclosure whose rows report `mediaRemovable = false` gets
the same protection.

---

#### REQ-F-012 — No explicit Mount control

**Ubiquitous**: The system shall not present a Mount control on any row.

**Acceptance**: No row fixture in any classification renders a button that invokes
`DevicesModel::mount` directly.

---

#### REQ-F-013 — Activation mounts before opening

**Event-driven**: When a row is activated and its volume is not mounted, the system shall mount that
volume and then navigate to the resulting mount path.

**Note**: This preserves existing behaviour; it is stated so the removal of the Mount control does
not make it unverified.

**Acceptance**: Existing coverage in
`tests/devices_model_test.cpp::MountOpensOnlyForCurrentActivationAndPreservesLocationOnFailure`
continues to pass unmodified.

---

#### REQ-F-014 — Shared eject glyph

**State-driven**: While a row's drive is classified as External or Optical, the system shall render
the removal control with the eject glyph.

**Rationale**: Nautilus and Dolphin both use one eject glyph for both cases; a distinct
"safely remove" glyph has no established recognition.

**Acceptance**: Both classifications resolve the same icon source; the asset is bundled through the
`apps/files/CMakeLists.txt` icon list and resolves without a system icon theme installed.

---

### Row visibility

#### REQ-F-015 — Unmounted internal volumes are hidden

**State-driven**: While a volume's drive is classified as Internal and the volume has no mount point,
the system shall omit that volume from the panel.

**Acceptance**: An Internal fixture with an unmounted data partition produces no row, and therefore
no removal control of any kind.

---

#### REQ-F-016 — Empty non-optical media slots are hidden

**State-driven**: While a drive reports `MediaRemovable` as true, `MediaAvailable` as false, and
`Optical` as false, the system shall omit that drive from the panel.

**Acceptance**: A four-slot reader fixture with no cards inserted produces zero rows. Inserting a
card into one slot produces exactly one row.

---

#### REQ-F-017 — Empty optical drives remain visible

**State-driven**: While a drive is classified as Optical and reports `MediaAvailable` as false, the
system shall present a drive-backed row for it. If it advertises Eject, the row shall offer Eject;
otherwise it shall have no removal control.

**Rationale**: Where supported, Ejecting an empty optical drive opens the tray. PowerOff is not a
substitute for opening it.

**Acceptance**: An Optical fixture with `mediaPresent = false`, `canEject = true` produces one row
whose control invokes `StorageController::eject`; the row is not activatable. With `canEject = false`,
the row remains visible without a control.

---

### Grouping

#### REQ-F-018 — Group label for multi-row drives

**State-driven**: While a drive contributes more than one visible row, the system shall display that
drive's name as a group label above its rows.

**Acceptance**: A reader fixture with two cards inserted, and an Internal fixture with two mounted
partitions, each render one group label carrying the drive name.

---

#### REQ-F-019 — No group label for single-row drives

**State-driven**: While a drive contributes exactly one visible row, the system shall display no
group label for that drive and shall consume no vertical space for one.

**Acceptance**: A single-stick fixture renders at the same height as the current implementation's
equivalent row; the section delegate reports zero height.

---

#### REQ-F-020 — Rows of one drive are contiguous

**Ubiquitous**: The system shall order rows such that all rows backed by the same drive are adjacent.

**Acceptance**: Rows sort by `(classification, driveId, targetId)`, keeping all rows of one drive
adjacent even when drive IDs share prefixes or concatenate ambiguously with target IDs. A fixture
with backend-interleaved drives `a` and `ax` and targets that would interleave under string
concatenation verifies the property.

---

#### REQ-F-055 — Internal drives are listed first

**Ubiquitous**: The system shall order device rows by classification — Internal, then External, then
Optical — and by drive within each classification.

**Acceptance**: A fixture listing an optical, an external and an internal drive in that backend order
yields rows in the order internal, external, optical.

**Added** after native acceptance (2026-09-25).

---

### Keyboard

#### REQ-F-021 — Removal keybinding

**Event-driven**: When the `x` key is pressed while a device row holds focus and that row has a
removal control, the system shall invoke that row's removal verb.

**Acceptance**: Sending `x` to a focused, mounted External row reaches `StorageController::powerOff`; sending
it to a focused Internal row reaches `unmount`; sending it to a focused Optical row reaches `eject`.

---

#### REQ-F-022 — Removal keybinding is inert without a control

**Unwanted behaviour**: If `x` is pressed while the focused row has no removal control, then the
system shall start no storage operation and shall report no error.

**Acceptance**: `errorMessage` remains empty and no backend call is recorded.

---

#### REQ-F-023 — Modal guard applies to the keybinding

**State-driven**: While the vim mode is not Normal, a task prompt is open, or Quick Look is open, the
system shall start no storage operation in response to `x`.

**Acceptance**: The existing `activationEnabled` gate covers the new binding; asserted for each of
the three suppressing conditions, mirroring
`tests/devices_model_test.cpp::ModalGuardSuppressesActionsAndInvalidatesPendingActivation`.

---

#### REQ-F-024 — Shift+X is unbound in the panel

**Ubiquitous**: The system shall leave the `Shift+X` key unhandled by the Devices panel.

**Rationale**: As in Vim, a plain letter binding means the unshifted key; a shifted binding is written
explicitly. Leaving `Shift+X` free avoids shadowing a future binding.

**Revised** after native acceptance (2026-09-25): the removal key was `X` (Shift+x); it is now plain `x`.

---

### Confirmation

#### REQ-F-025 — No power-off confirmation

**Ubiquitous**: The system shall invoke the selected removal verb without an intermediate
confirmation step.

**Acceptance**: `DevicesModel` exposes no `confirmationText` property; `DevicesPanel.qml` renders no
confirmation label or Power off / Cancel pair; a single activation of the control produces exactly one
backend request.

---

#### REQ-F-026 — Scope is derived at invocation

**Event-driven**: When the system invokes PowerOff, it shall pass the removal scope derived at the
moment of invocation.

**Rationale**: `StorageController::powerOff` rejects a scope that does not match its own derivation,
and re-derives again in `advance()`. Passing a freshly computed scope preserves that race protection
without a user-facing prompt.

**Acceptance**: Invocation succeeds against an unchanged fixture and is rejected with `ScopeChanged`
when a sibling drive is added between invocation and execution.

---

#### REQ-F-027 — Scope change is reported, not partially applied

**Unwanted behaviour**: If the removal scope changes between invocation and execution, then the
system shall surface the existing `ScopeChanged` message and leave every affected volume mounted.

**Acceptance**: Coverage adapted from
`tests/devices_model_test.cpp::PowerOffRejectsChangedVolumeScopeWithoutPartialUnmount`,
with no confirmation step or sibling-wide PowerOff.

---

### Busy and error state

#### REQ-F-028 — Controls are inert while busy

**State-driven**: While any operation holds a row's drive or volume in its scope, the system shall
present that row's removal control as disabled.

**Acceptance**: Existing busy-flag behaviour is retained; a second invocation during an in-flight
operation records no additional backend request.

---

#### REQ-F-029 — Operation errors are surfaced

**Event-driven**: When a removal operation fails, the system shall display the mapped message from
`operationError` in the window status bar, not in the sidebar.

**Acceptance**: Each branch of `operationError` is reachable through the new control for at least one
classification; the failure message becomes the controller's `statusMessage`, and the sidebar renders no
error label.

**Revised** after native acceptance (2026-09-25): an error label inside the sidebar was rejected.

---

### Sidebar composition

#### REQ-F-030 — Single sidebar scroll area

**Ubiquitous**: The sidebar shall present Places, bookmarks and Devices within one scrollable area
with one scroll position.

**Rationale**: The current structure anchors two independently scrolling lists to the top and bottom
halves of the sidebar, so a long Places list scrolls while Devices cannot be reached by the same
gesture.

**Acceptance**:
- Exactly one scroll bar is instantiated for the sidebar.
- Scrolling past the last Places row continues into the Devices section without a second gesture.
- No sidebar child sets its height as a proportion of the sidebar height.

---

#### REQ-F-031 — Sidebar section order

**Ubiquitous**: The sidebar shall order its content as Places and bookmarks, then a horizontal
separator, then Devices.

**Acceptance**: The separator's vertical position lies between the last Places row and the Devices
heading at every sidebar width and content length.

---

#### REQ-F-032 — Separator presence follows the Devices section

**State-driven**: While the Devices section renders no rows, the system shall display neither the
separator nor the Devices heading.

**Acceptance**: With an empty devices model the sidebar renders no trailing separator and no residual
vertical gap below the last Places row.

---

#### REQ-F-033 — Sections size to their content

**Ubiquitous**: Each sidebar section shall occupy exactly the height its rows require.

**Acceptance**: Sidebar content height equals the sum of both sections' content heights, the
separator and the inter-section spacing, within one logical pixel, for fixtures of 3 and 30 total rows.

---

### Device row composition

#### REQ-F-034 — Three-column row

**Ubiquitous**: A device row shall comprise three columns — a device icon, a vertical stack of name
and capacity, and the removal control — separated by spacing alone.

**Acceptance**: The row contains no separator, divider or rule element between columns.

---

#### REQ-F-035 — Icon column

**Ubiquitous**: The first column shall contain the device icon, vertically centred against the full
row height.

**Acceptance**: The icon's vertical centre matches the row's vertical centre within one logical pixel
for both a row that shows a capacity bar and one that does not.

---

#### REQ-F-036 — Icon selection by classification

**State-driven**: While a row's drive is classified as Optical, the system shall request
`media-optical-symbolic`; while classified as External and reporting `MediaRemovable` as true,
`media-flash-symbolic`; while classified as External and reporting `MediaRemovable` as false,
`drive-removable-media-symbolic`; while classified as Internal, `drive-harddisk-symbolic`.

**Note**: Names are from the freedesktop icon naming specification. `MediaRemovable` separates a card
in a reader from a stick or external disk, reusing the same property that selects the removal verb
in REQ-F-004 and REQ-F-005.

**Acceptance**: Each of the four cases resolves the stated name through the `image://icon/` provider.
All four names resolve against the installed icon themes on a reference system.

---

#### REQ-F-037 — Icon fallback

**Unwanted behaviour**: If a requested device theme icon fails to resolve, then the system shall
render a bundled fallback asset in its place.

**Rationale**: Mirrors the existing Places behaviour, where `HnIcon.hasError` selects a
`qrc:/qt/qml/HolonightFiles/icons/` asset.

**Acceptance**: With no system icon theme installed, every device row renders a visible icon.

**Note**: Symbolic assets are authored near-black and are not recoloured anywhere on the current
`image://icon/` path, so device glyphs will render dark against the dark sidebar surface until that
is addressed upstream. This specification deliberately adds no workaround; see
`holonight-qt/docs/icons-recoloring-issue.md`.

---

#### REQ-F-038 — Content column stack

**Ubiquitous**: The second column shall stack three elements vertically — the device name, the
capacity bar, and the capacity text — and shall consume the row's remaining width.

**Acceptance**: The column's width absorbs sidebar width changes while columns one and three keep
their implicit widths.

---

#### REQ-F-039 — Capacity text format

**State-driven**: While capacity figures are available for a row, the system shall display the
available and total amounts in the form `"345 GB free of 1 TB"`.

**Acceptance**:
- The string is produced through a translatable format with both amounts as arguments, not by
  concatenation.
- The free figure is the space available to an unprivileged user, excluding filesystem-reserved
  blocks.

---

#### REQ-F-040 — Removal control column

**Ubiquitous**: The third column shall contain the row's removal control, vertically centred against
the full row height.

**Acceptance**: The control's vertical centre matches the row's vertical centre within one logical
pixel, independent of whether the capacity bar is present.

---

### Capacity

#### REQ-F-041 — Capacity source

**Ubiquitous**: The system shall obtain capacity figures by querying the filesystem mounted at a
volume's mount point.

**Rationale**: `org.freedesktop.UDisks2.Filesystem` exposes only `MountPoints` and `Size`, and
reports `Size` as `0` for some mounted filesystems (Appendix B). No free-space figure exists anywhere
in the UDisks2 object model, so it cannot be sourced from the storage provider.

**Acceptance**: Both the free and total figures for one row originate from the same query, so the bar
and the text cannot disagree.

---

#### REQ-F-042 — Capacity acquisition is off the main thread

**Ubiquitous**: The system shall perform capacity queries on a worker thread.

**Rationale**: The query stats a mount point and can block indefinitely on an unresponsive device.

**Acceptance**: No capacity query is issued from the thread that owns the model; asserted by the same
means as the existing `DirectoryModel` classifier work.

---

#### REQ-F-043 — Rows without capacity

**State-driven**: While capacity figures are unavailable for a row, the system shall hide the capacity
bar while keeping its space, and shall show the row's state text in place of the capacity text, so that
every device row has the same height.

**Rationale**: Covers unmounted volumes, empty optical drives, and queries that have failed or not
yet completed. A bar at zero would misreport usage; rows of differing height looked inconsistent.

**Acceptance**: An unmounted-volume row and a measured row render at the same height within half a
logical pixel; the unmounted row's bar is fully transparent and its caption is its state text.

**Revised** after native acceptance (2026-09-25): collapsing rows without figures was rejected.

---

#### REQ-F-044 — Capacity refresh

**Event-driven**: When a volume's mount points change, the system shall re-query that volume's
capacity.

**Acceptance**: A fixture that mounts a volume produces a capacity query for it; unmounting clears
the previously displayed figures rather than leaving them stale.

---

#### REQ-F-045 — Capacity refresh cadence

**Ubiquitous**: While mounted device rows exist, the system shall attempt to refresh their capacity
at a bounded interval. A query still in flight shall not be duplicated. Its previous figure may
remain visible until that query completes.

**Acceptance**: The interval is a single named constant; a fixture advancing time past it produces a
new query without user interaction when no query for that mount point is in flight. The timer stops
when no mounted device rows remain.

---

#### REQ-F-046 — Bar represents used space

**State-driven**: While a capacity bar is displayed, its filled proportion shall equal used space
divided by total space.

**Acceptance**: A fixture reporting 1 TB total and 345 GB free fills the bar to `1 - 345/1000` of its
width, within one logical pixel.

---

#### REQ-F-047 — Capacity threshold colouring

**State-driven**: While used space is below 90% of total, the bar fill shall be
`HoloniightPalette.primary`; while at or above 90% and below 95%, `HoloniightPalette.accentViolet`;
while at or above 95%, `HoloniightPalette.warning`.

**Acceptance**: Fills are read back at used fractions of 0.899, 0.90, 0.949, 0.95 and 0.999 and
compared against the live palette singleton, not against hardcoded colour values.

---

#### REQ-F-048 — Threshold boundaries are inclusive at the lower edge

**Ubiquitous**: Each threshold shall apply from its stated percentage inclusive.

**Rationale**: States the boundary so REQ-F-047 cannot be satisfied by an off-by-one comparison.

---

### Sidebar navigation

#### REQ-F-049 — One focused row across the sidebar

**Ubiquitous**: The sidebar shall hold keyboard focus on at most one row at any time, across Places,
bookmarks and Devices together.

**Rationale**: The current structure keeps an independent current-index per list, so both sections
can appear focused at once.

**Acceptance**: After every navigation key in a fixture containing both sections, exactly one row
reports focus.

---

#### REQ-F-050 — Downward crossing into Devices

**Event-driven**: When `j` or Down is pressed while the last Places-section row holds focus and the
Devices section has at least one row, the system shall move focus to the first device row.

**Acceptance**: Focus lands on the first device row; the Places selection is cleared in the same
step, satisfying REQ-F-049.

---

#### REQ-F-051 — Upward crossing into Places

**Event-driven**: When `k` or Up is pressed while the first device row holds focus, the system shall
move focus to the last Places-section row.

**Acceptance**: Focus lands on the last Places row, including when a group label (REQ-F-018)
precedes the first device row.

---

#### REQ-F-052 — No crossing without a destination

**Unwanted behaviour**: If a crossing key is pressed at a sidebar boundary with no row beyond it,
then the system shall leave focus unchanged.

**Acceptance**: Covers Down on the last device row, Up on the first Places row, and Down on the last
Places row while the Devices section renders no rows.

---

#### REQ-F-053 — Crossing reveals its destination

**Event-driven**: When focus crosses a section boundary, the system shall scroll the newly focused
row into view.

**Rationale**: REQ-F-030 makes the sidebar one scroll area, so a crossing can land outside the
viewport.

**Acceptance**: With a Places list long enough to overflow the sidebar, crossing into Devices leaves
the focused device row fully within the visible area.

---

#### REQ-F-054 — Activation follows focus

**Ubiquitous**: The sidebar's activation keys shall act on the row that holds focus, irrespective of
its section.

**Acceptance**: Return, Enter and Space each navigate to a place when a Places row holds focus, and
each activate a device when a device row holds focus, with no change to the modal guard.

---

## Constraints

#### REQ-C-001 — No hardware-specific identifiers

**Ubiquitous**: Classification and verb derivation shall not read a drive's `vendor`, `model`,
`serial`, `siblingId`, `device` node, or any mount path.

**Acceptance**: The classification and verb functions reference only `optical`, `connectionBus`,
`mediaRemovable`, `mediaPresent`, `canEject` and `canPowerOff` from `StorageDrive`, and `canUnmount`
from `StorageVolume`. Reviewed by inspection; no string literal naming a bus value, vendor or model
appears in the classification source.

**Note**: The existing GPT partition-type GUIDs and FHS paths in `StorageFilter::infrastructure()`
are specification constants, not hardware identifiers, and are out of scope.

---

#### REQ-C-002 — Provider is unchanged

**Ubiquitous**: The implementation shall introduce no change to `holonight-system-services`.

**Acceptance**: `StorageDrive` already carries `optical`, `connectionBus`, `mediaRemovable`,
`mediaPresent`, `canEject` and `canPowerOff`, all populated in `UDisks2Backend.cpp`. The pinned
provider revision is unchanged.

---

#### REQ-C-003 — Coverage without hardware

**Ubiquitous**: Storage behavior in this specification shall be verifiable through the existing
`FakeStorage` backend; the capacity adapter itself may use a temporary local directory for a
focused smoke test, and native visual checks remain manual.

**Acceptance**: The test suite gains fixtures for a multi-slot reader (shared `SiblingId`, empty and
populated), an optical drive (empty and loaded), an External drive with no drive-level capability, and
an Internal drive advertising both. No test reads the host's real UDisks2 service.

---

#### REQ-C-004 — Pure classification functions

**Ubiquitous**: Classification and initial verb derivation shall be free functions over
`StorageDrive` and the volume's `canUnmount` fact, with no dependency on model, controller or
backend state. The model applies the REQ-F-011 scope guard to any proposed PowerOff verb.

---

#### REQ-C-005 — Design system is unchanged

**Ubiquitous**: The implementation shall introduce no change to `holonight-qt`.

**Note**: `Holonight.Controls`' `ProgressBar` fixes its indicator colour to the control palette's
`primary` role and exposes no per-instance colour property, so REQ-F-047 cannot be met by
configuring it. The capacity bar is therefore composed within the device row from primitives.
Adding a colour property upstream would be a cross-repository change requiring the umbrella
initiative workflow, and is deliberately excluded.

---

#### REQ-C-006 — Capacity probe is injectable

**Ubiquitous**: Capacity acquisition shall sit behind an interface that tests can substitute.

**Rationale**: Mirrors `LocationClassifier` / `RealLocationClassifier`, which `DirectoryModel`
already holds as a `shared_ptr<const>` and invokes from its worker.

**Acceptance**: Model and presentation capacity requirements are verified with a substituted probe
returning fixed figures. Only the `StorageInfoCapacityProbe` adapter smoke tests read a temporary
local directory's filesystem capacity.

---

## Non-functional Requirements

#### REQ-NF-001 — Keyboard parity

**Ubiquitous**: Every action reachable by pointer in the Devices panel shall be reachable by keyboard.

**Acceptance**: Activation (Return / Enter / Space) and removal (`x`) together cover every control
rendered by the panel.

---

#### REQ-NF-002 — Accessible description

**Ubiquitous**: The removal control shall carry an accessible name naming its verb.

**Rationale**: A shared glyph across External and Optical means the icon alone does not disambiguate.

**Acceptance**: `Accessible.name` differs between a row whose verb is Eject and one whose verb is
PowerOff.

---

## Amendments to udisks2-storage

| Original | Amendment |
|---|---|
| R3 — "Files shall additionally show empty removable drives" | Narrowed: empty **optical** drives remain shown; empty media-removable non-optical slots are hidden (REQ-F-016, REQ-F-017). |
| R4 — "Power-off shall display the full affected scope before confirmation and reject stale confirmation" | Confirmation removed (REQ-F-025). Stale-scope rejection is retained and now guards an invocation-time scope (REQ-F-026, REQ-F-027). |

R1, R2, R5 and R6 are unaffected.

---

## Open Questions

1. **Multi-LUN reader structure is inferred, not observed.** The one-Drive-per-slot model and the
   shared `SiblingId` follow from the UDisks2 object model and from the observation that `SiblingId`
   is a USB *interface* path (Appendix A), but no multi-slot reader was available during
   investigation. REQ-F-016 and REQ-F-018 should be re-verified against real hardware before the
   verification record is closed.
2. **Eject on a card-reader slot is firmware-dependent.** `Drive.Eject` issues SCSI `START STOP UNIT`;
   some readers ignore it and leave the slot reporting media present. The operation is harmless and
   the volume is unmounted first either way, but the row may not visibly change state.
3. **"Not mounted" state text** remains developer-facing. Out of scope here; worth a follow-up.

The capacity refresh interval and navigation structure were resolved in [DESIGN.md](DESIGN.md)
§4.4 and §4.1 respectively.

---

## Deferred to follow-up work

These are known, deliberately unaddressed here. Neither is to be worked around in this
specification's implementation.

1. **Symbolic icon recolouring.** Device glyphs requested under REQ-F-036 will render in their
   authored near-black against the dark sidebar, because nothing on the `image://icon/` path
   recolours them. Analysed in `holonight-qt/docs/icons-recoloring-issue.md`; the fix belongs
   upstream and is a prerequisite for this feature looking correct, not for it working.

2. **Volumes with multiple mount points.** A volume may report more than one mount point, and the
   present implementation opens `mountPoints.first()`. This specification neither relies on nor
   corrects that behaviour. It warrants its own investigation and SDD cycle.

---

## Appendix A — Observed UDisks2 data

`org.freedesktop.UDisks2.Drive`, udisks2 2.11.2-1, captured 2026-09-25.

| Drive | ConnectionBus | Optical | MediaRemovable | MediaAvailable | Ejectable | CanPowerOff |
|---|---|---|---|---|---|---|
| Samsung SSD 990 PRO (NVMe) | `''` | false | false | true | false | false |
| Samsung SSD 980 PRO (NVMe) | `''` | false | false | true | false | false |
| Hitachi HTS547575A9E384 (SATA) | `''` | false | false | true | false | false |
| TS1TSSD220Q (SATA SSD) | `''` | false | false | true | false | false |
| JetFlash Transcend 64GB (USB) | `'usb'` | false | false | true | true | true |

Supporting values for the USB stick:

```
Media            = 'thumb'
MediaCompatibility = ['thumb']
SiblingId        = '/sys/devices/pci0000:00/0000:00:14.0/usb2/2-7/2-7:1.0'
```

Two facts drive this specification:

- `ConnectionBus` is empty for every internally attached drive and non-empty only for externally
  attached ones, which makes it the internal/external discriminator without any hardware-specific
  literal.
- `MediaRemovable` is **false** for the USB stick. The stick is itself the removable object; a card
  reader slot or optical drive instead reports true because it *holds* a removable medium. This is
  the distinction that selects Eject versus PowerOff.

`SiblingId` resolving to a USB interface path rather than a per-LUN path is the corroboration that
sibling LUNs of one physical reader share it.

---

## Appendix B — UDisks2 exposes no free space

`org.freedesktop.DBus.Properties.GetAll` on `org.freedesktop.UDisks2.Filesystem`, same capture:

| Block device | MountPoints | Size |
|---|---|---|
| `nvme0n1p1` | `/boot` | `0` |
| `nvme0n1p3` | `/` | `981949480960` |
| `nvme1n1p1` | — | `0` |
| `sda1` | `/home/andrii/Media`, `/mnt/storage` | `1000203091968` |
| `sdb1` | `/mnt/backup` | `750154416128` |

The interface carries only these two properties. There is no free, available or used figure on
`Filesystem`, on `Block`, or on `Drive`, and `Size` is reported as `0` for a mounted filesystem in at
least one case above. This is why REQ-F-041 sources capacity from the filesystem directly rather than
from the storage provider, and why extending `StorageVolume` would not help.

Note also that two distinct sizes exist and must not be conflated: `Block.Size` (the partition size,
already carried as `StorageVolume::capacity`) and `Filesystem.Size` (the filesystem size). Neither is
the denominator a user expects, which is total space as the mounted filesystem reports it.
