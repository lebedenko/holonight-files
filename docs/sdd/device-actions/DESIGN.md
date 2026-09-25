# Device Actions — Architecture Design

## Status

**Implemented and verified 2026-09-26** (build, test, format-check, tidy, qml-lint green; native acceptance passed, T-014). Implements [SPEC.md](SPEC.md) (64 requirements). Baseline `1cc5b2f`.

The scope-safety and row-ordering gaps found during review are closed by T-015 and T-016.

Resolves the two design decisions SPEC left open:

- **Open Question 5 (navigation structure)** — neither an aggregate model nor per-list boundary
  handoff. A pure C++ cursor (`SidebarNavigator`) owns the single focus position; both `ListView`s
  become presentational. See §4.1.
- **Open Question 4 (capacity refresh interval)** — 30 s while mounted rows exist, plus event-driven refreshes
  on mount change and task completion. See §4.4.

## Revisions after native acceptance (2026-09-25)

- Device operation errors are emitted as `DevicesModel::operationFailed` and shown in the window status
  bar through `DirectoryController::statusMessage`; the panel's error label is gone (REQ-F-029).
- Rows sort by `StoragePolicy::DeviceClass` (declaration order Internal, External, Optical) before
  `driveId + targetId` (REQ-F-055).
- Every device row keeps the capacity bar's slot (opacity 0 without figures) and shows its state text as
  the caption, so all rows are equally tall (REQ-F-043).
- Capacity queries use one detached thread per mount point with the `PlacesModel` `DeliveryGuard` idiom,
  not `QThreadPool`: a hung mount must neither starve other rows nor block application exit. A mount point
  with a query outstanding is not queried again.

- The removal key is plain `x` (`event.text === "x"`); `Shift+X` is unbound (REQ-F-021, REQ-F-024).
- A volume row offers a removal verb only while mounted (REQ-F-008a); empty optical drive rows keep Eject.

## 1. Overview & Component Diagram

The existing Devices path is `StorageController` → `DevicesModel::refresh()` (which flattens drives
and volumes into `QList<QVariantMap> rows_`) → `DevicesPanel.qml`. That shape is sound and is kept.
What changes:

- `refresh()` stops emitting four capability booleans and instead emits one classification, one
  removal verb, and grouping/capacity fields. The decision logic moves out of `refresh()` into pure
  functions so it can be tested without a model.
- Capacity arrives from a new model-owned async subsystem, mirroring `PlacesModel`'s availability
  checker and `DirectoryModel`'s classifier: an injectable interface, called off the GUI thread,
  results posted back by id.
- The sidebar stops being two anchored half-height panels and becomes one `Flickable` containing
  two content-sized, non-interactive `ListView`s. A new `SidebarNavigator` owns the keyboard cursor
  that spans them.

```
                       ┌──────────────────────────────┐
                       │      DirectoryController      │
                       │  places_ (value), devices_ (*) │
                       │  + navigator_ (value, new)     │
                       └───┬───────────┬────────────┬──┘
           places()        │           │ devices()  │ navigator()
                           ▼           ▼            ▼
         ┌──────────────────┐  ┌──────────────────────┐  ┌────────────────────────┐
         │   PlacesModel     │  │     DevicesModel      │  │   SidebarNavigator      │
         │   (unchanged)     │  │  rows_: QVariantMap   │  │  (new, pure QObject)    │
         └──────────────────┘  │  + capacity bookkeeping│  │  cursor: (Section,int)  │
                   ▲            └───┬──────────────┬───┘  └───────────┬────────────┘
                   │                │ calls        │ owns             │ reads counts
                   │                ▼              ▼                  │
                   │   ┌────────────────────┐  ┌──────────────────┐   │
                   │   │  storage_policy.h   │  │ CapacityProbe    │   │
                   │   │  (new, pure)        │  │ (new, injectable)│   │
                   │   │  classify()         │  │ Qt impl = QStorage│  │
                   │   │  removalVerb()      │  │ Info, off-thread  │  │
                   │   └────────────────────┘  └──────────────────┘   │
                   │                                                   │
                   └───────────────────────┬───────────────────────────┘
                                           │ (counts + activation)
                                           ▼
                              ┌──────────────────────────┐
                              │      SidebarPanel.qml     │  (new container)
                              │  Flickable > Column:      │
                              │    PlacesPanel  (list)    │
                              │    HnSeparator            │
                              │    DevicesPanel (list)    │
                              └──────────────────────────┘
```

Ownership: `DevicesModel` owns `rows_`, the injected `probe_` (`shared_ptr<const CapacityProbe>`),
the per-row capacity cache and the refresh timer. `storage_policy.h` is stateless. `SidebarNavigator`
owns only its cursor and holds non-owning pointers to the two models for row counts.

## 2. Components and Interfaces

### 2.1 `storage_policy.h` (new, pure) — `apps/files/storage/storage_policy.h`

Sits beside the existing `storage_filter.h`, which keeps its eligibility/infrastructure role
unchanged. This file holds only the new classification and verb decisions.

```cpp
#pragma once

#include <optional>

#include <StorageTypes.h>

namespace StoragePolicy {

// SPEC REQ-F-001..003. Presentation class; decides whether a removal control exists and its glyph.
enum class DeviceClass { Internal, External, Optical };

// SPEC REQ-C-001: reads only optical/connectionBus. No vendor, model, serial or path.
inline DeviceClass classify(const HoloNight::System::StorageDrive& drive) {
  if (drive.optical) return DeviceClass::Optical;                        // REQ-F-001, tested first
  if (!drive.connectionBus.isEmpty()) return DeviceClass::External;      // REQ-F-002
  return DeviceClass::Internal;                                          // REQ-F-003
}

// Policy for SPEC REQ-F-004..008, including the safe medium fallback.
// nullopt means "no removal control on this row" (REQ-F-008).
inline std::optional<HoloNight::System::StorageOperation> removalVerb(
    const HoloNight::System::StorageDrive& drive, bool volumeCanUnmount) {
  using Op = HoloNight::System::StorageOperation;
  if (classify(drive) == DeviceClass::Internal) {
    return volumeCanUnmount ? std::optional{Op::Unmount} : std::nullopt;  // REQ-F-009
  }
  // PowerOff never substitutes for Eject on a medium-removable drive (REQ-F-006).
  if (drive.mediaRemovable) {
    if (drive.canEject) return Op::Eject;
    return volumeCanUnmount ? std::optional{Op::Unmount} : std::nullopt;
  }
  if (drive.canPowerOff) return Op::PowerOff;
  if (drive.canEject) return Op::Eject;                                  // REQ-F-006
  return volumeCanUnmount ? std::optional{Op::Unmount} : std::nullopt;   // REQ-F-007, REQ-F-008
}

// SPEC REQ-F-036. Freedesktop names; rendered as authored (see SPEC "Deferred to follow-up work").
inline QString iconName(const HoloNight::System::StorageDrive& drive) {
  switch (classify(drive)) {
    case DeviceClass::Optical:  return QStringLiteral("media-optical-symbolic");
    case DeviceClass::External: return drive.mediaRemovable ? QStringLiteral("media-flash-symbolic")
                                                            : QStringLiteral("drive-removable-media-symbolic");
    case DeviceClass::Internal: return QStringLiteral("drive-harddisk-symbolic");
  }
  return {};
}

}  // namespace StoragePolicy
```

Both functions are free functions over `StorageDrive` with no model, controller or backend state
(REQ-C-004), and every field they read already exists on `StorageDrive`, so nothing in
`holonight-system-services` changes (REQ-C-002).

`removalVerb` returning `optional` rather than a bool-plus-enum represents REQ-F-008 directly.
A card-reader slot selects Eject when `canEject` is true and Unmount or no control otherwise.
PowerOff is never a medium-removable fallback. For a self-removable drive, the model checks the
controller-derived scope before exposing PowerOff (see §2.3).

### 2.2 `CapacityProbe` (new, injectable) — `apps/files/storage/capacity_probe.h`

Deliberately shaped like `LocationClassifier` (`apps/files/browsing/location_classifier.h`), which
`DirectoryModel` holds as `shared_ptr<const>` and invokes from its worker.

```cpp
#pragma once

#include <QString>

struct Capacity {
  bool valid = false;
  quint64 bytesAvailable = 0;  // REQ-F-039: space available to an unprivileged user.
  quint64 bytesTotal = 0;
};

class CapacityProbe {
 public:
  virtual ~CapacityProbe() = default;
  // Called on a worker thread only (REQ-F-042). May block.
  [[nodiscard]] virtual Capacity measure(const QString& mountPoint) const = 0;
};

// QStorageInfo::bytesAvailable(), not bytesFree(): the latter includes root-reserved blocks.
class StorageInfoCapacityProbe : public CapacityProbe {
 public:
  [[nodiscard]] Capacity measure(const QString& mountPoint) const override;
};
```

`Capacity::valid` carries "unmounted, failed, or not yet measured" as one state, which is exactly
the condition REQ-F-043 keys the hidden bar and text off. Both figures come from one `measure()`
call, so the bar and the text cannot disagree (REQ-F-041).

### 2.3 `DevicesModel` changes — `apps/files/storage/devices_model.{h,cpp}`

**Roles.** Removed: `CanMount`, `CanUnmount`, `CanEject`, `CanPowerOff`. Added:

| Role | Type | Requirement |
|---|---|---|
| `IconName` | QString | REQ-F-036 |
| `RemovalVerb` | int (`StorageOperation`, `-1` = none) | REQ-F-008, REQ-F-010 |
| `RemovalLabel` | QString | REQ-NF-002 |
| `GroupLabel` | QString (empty = no header) | REQ-F-018, REQ-F-019 |
| `CapacityValid` | bool | REQ-F-043 |
| `CapacityFraction` | qreal (used / total) | REQ-F-046 |
| `CapacityText` | QString | REQ-F-039 |

`TargetId`, `DriveId`, `Name`, `DriveName`, `State`, `Mounted`, `Busy`, `CanActivate` are kept.

`StorageOperation` is a plain `enum class` in `HoloNight::System` with no `Q_ENUM`, so QML cannot
name its values. `DevicesModel` therefore declares a mirror whose values match, which is what
`RemovalVerb` carries and what the delegate compares against:

```cpp
  enum RemovalVerb { NoVerb = -1, Mount = 0, Unmount, Eject, PowerOff };  // matches StorageOperation
  Q_ENUM(RemovalVerb)
```

**Public surface.** `mount()`, `unmount()`, `eject()`, `requestPowerOff()`, `confirmPowerOff()`,
`cancelPowerOff()` and the `confirmationText` property are all removed (REQ-F-025). Removing the
invokable `mount()` is what makes REQ-F-012 structural: QML has no way to request a mount except
through `activate()`. `errorMessage` remains for model consumers; failures also emit
`operationFailed` for the window status bar, and the panel error label is removed (REQ-F-029).
One entry point replaces the old actions:

```cpp
  Q_INVOKABLE void remove(const QString& targetId);   // REQ-F-021: x and the button both call this.
```

`remove()` looks the row up, reads its `RemovalVerb`, and dispatches:

```cpp
void DevicesModel::remove(const QString& targetId) {
  if (!interaction_enabled_) return;                                   // REQ-F-023
  const auto row = findRow(targetId);
  if (!row || row->value("busy").toBool()) return;                     // REQ-F-028
  const auto verb = row->value("removalVerb").toInt();
  switch (static_cast<StorageOperation>(verb)) {
    case StorageOperation::Unmount:  controller_->unmount(targetId); break;
    case StorageOperation::Eject:    controller_->eject(row->value("driveId").toString()); break;
    case StorageOperation::PowerOff: {
      const auto driveId = row->value("driveId").toString();
      const auto scope = controller_->removalScope(driveId, true);
      if (scope.isEmpty() || scopeContainsOtherDrive(driveId, scope)) {
        scheduleRefresh();
        break;
      }
      // REQ-F-026: scope derived at invocation; the controller re-derives in advance() and
      // rejects with ScopeChanged on a race (REQ-F-027).
      controller_->powerOff(driveId, scope);
      break;
    }
    default: break;                                                     // REQ-F-022: verb -1, inert.
  }
}
```

The existing `visibleTarget(id, capability)` helper is replaced by `findRow(id)`, since capability
booleans no longer exist as roles. The guard it provided — an operation must correspond to a visible,
capable row — survives: a row without a verb reaches `default:` and does nothing.

**T-015 scope guard.** `volumeRow()` and `appendEmptyDrives()` use `safeRemovalVerb()` to downgrade
PowerOff to row-local Unmount (or no control) when `StorageController::removalScope()` contains
another drive. `remove()` repeats the check immediately before dispatch because the scope can
change after the row was built. The controller's later `ScopeChanged` check remains the final race
guard.

**Visibility.** `refresh()`'s volume filter is unchanged for REQ-F-015 (it already skips unmounted
non-removable volumes). `appendEmptyDrives()` gains one condition:

```cpp
    if (!drive.removable || drive.mediaPresent) continue;
    if (!drive.optical) continue;   // REQ-F-016: empty card slots are noise; REQ-F-017 keeps optical.
```

**Grouping.** A second pass counts rows per `driveId` and writes `groupLabel` — `driveName` when
the count exceeds one, empty otherwise (REQ-F-018, REQ-F-019). The pass assumes each drive's rows
are contiguous. The comparator sorts by separate `(classRank, driveId, targetId)` fields, so
overlapping IDs cannot interleave drive groups (REQ-F-020, REQ-F-055; T-016).

### 2.4 Capacity subsystem inside `DevicesModel`

Dispatch mirrors §3.1 of the places-sources design. A delivery guard protects model lifetime;
a generation per mount point rejects results from an earlier mount or query.

```cpp
  QHash<QString /*mountPoint*/, Capacity> capacity_;
  QHash<QString /*mountPoint*/, quint64> capacity_in_flight_;
  quint64 capacity_generation_ = 0;
  QTimer capacity_timer_;                      // REQ-F-045, 30 s while mounted rows exist
  std::shared_ptr<const CapacityProbe> probe_;
  std::shared_ptr<DeliveryGuard> guard_;
```

Per refresh, a mounted row's mount point without a cached value is dispatched on its own detached
thread. The 30 s timer requests a fresh measurement unless one is already in flight. Results post
back through the delivery guard and are dropped if the mount point's generation changed. A hung
mount neither blocks another row nor application exit. `capacity_` is keyed on mount point, so two
volumes at the same path share a measurement and an unmount evicts by path. An existing value
remains visible while refresh is in flight; the interval bounds attempts, not displayed-data age.

The mount point used is `volume.mountPoints.first()` — unchanged from today, per SPEC's deferred
item 2.

### 2.5 `SidebarNavigator` (new) — `apps/files/presentation/sidebar_navigator.{h,cpp}`

```cpp
class SidebarNavigator : public QObject {
  Q_OBJECT
  Q_PROPERTY(int section READ section NOTIFY cursorChanged)   // Section enum
  Q_PROPERTY(int index READ index NOTIFY cursorChanged)
 public:
  enum Section { None = -1, Places = 0, Devices = 1 };
  Q_ENUM(Section)
  SidebarNavigator(PlacesModel* places, DevicesModel* devices, QObject* parent = nullptr);

  Q_INVOKABLE bool moveDown();          // REQ-F-050, REQ-F-052
  Q_INVOKABLE bool moveUp();            // REQ-F-051, REQ-F-052
  Q_INVOKABLE void setCursor(int section, int index);   // pointer focus, delegate clicks
  Q_INVOKABLE void clear();
 signals:
  void cursorChanged();
  void revealRequested(int section, int index);          // REQ-F-053
};
```

`moveDown()` walks the flattened sequence `[Places rows…][Devices rows…]`, so crossing a boundary is
an ordinary step and needs no special case beyond clamping at the ends. It returns `false` when the
cursor cannot move, which QML uses to leave the key unaccepted (REQ-F-052). Every successful move
emits `revealRequested`.

The single cursor is the whole of REQ-F-049: there is one position, so two sections cannot both
appear focused. Both `ListView`s bind `currentIndex` to it:

```qml
currentIndex: navigator.section === SidebarNavigator.Places ? navigator.index : -1
```

Because it is a plain `QObject` over two model pointers, every navigation requirement is testable
without instantiating QML.

### 2.6 `SidebarPanel.qml` (new) — `apps/files/qml/places/SidebarPanel.qml`

Replaces the two anchored children in `Main.qml:97-113`.

```qml
Flickable {
    id: root
    required property DirectoryController controller
    contentWidth: width
    contentHeight: column.implicitHeight          // REQ-F-033
    boundsBehavior: Flickable.StopAtBounds
    Controls.ScrollBar.vertical: Controls.ScrollBar {}   // REQ-F-030: the sidebar's only scroll bar

    Column {
        id: column
        width: root.width
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        PlacesPanel  { width: column.width; controller: root.controller }
        HnSeparator  { width: column.width; visible: devices.hasContent }   // REQ-F-031, REQ-F-032
        DevicesPanel { id: devices; width: column.width; controller: root.controller }
    }
}
```

Both panels change from `Item` with anchored fill to content-sized: their inner `ListView` gets
`interactive: false`, `height: contentHeight`, and drops its own `ScrollBar` (REQ-F-030, REQ-F-033).
`keyNavigationEnabled` becomes `false` — the navigator owns movement now.

`HnSeparator` is horizontal by default (`orientation: Qt.Horizontal`), so the existing vertical
`sidebarDivider` in `Main.qml` is untouched.

Key handling moves to the `Flickable`:

```qml
    Keys.onPressed: event => {
        if (!root.controller.sidebarActivationEnabled) return;      // REQ-F-023
        const nav = root.controller.sidebarNavigator;
        if (event.key === Qt.Key_Down || event.text === "j")      event.accepted = nav.moveDown();
        else if (event.key === Qt.Key_Up || event.text === "k")   event.accepted = nav.moveUp();
        else if (event.text === "x")                               { root.removeFocusedDevice(); event.accepted = true; }
    }
```

Plain `x` is matched on `event.text`; `Shift+X` falls through unhandled (REQ-F-024).
Activation keys (Return/Enter/Space) resolve through the same cursor (REQ-F-054).
With movement, activation and removal all bound here, every control the panel renders has a
keyboard route (REQ-NF-001).

`revealRequested` is handled by mapping the target delegate into the flickable's content item and
nudging `contentY`, which is robust to the variable row heights that REQ-F-043 and REQ-F-018
introduce:

```qml
    function reveal(section, index) {
        const list = section === SidebarNavigator.Places ? placesList : devicesList;
        const item = list.itemAtIndex(index);
        if (!item) return;
        const top = item.mapToItem(column, 0, 0).y;
        if (top < contentY) contentY = top;
        else if (top + item.height > contentY + height) contentY = top + item.height - height;
    }
```

### 2.7 `DeviceRow` composition — `apps/files/qml/places/DevicesPanel.qml`

The three `Controls.Button`s and the confirmation `Flow` are replaced by a `RowLayout` of three
columns with no separator elements (REQ-F-034):

```qml
RowLayout {
    spacing: HnMetrics.internalSpacing(HnControlSize.Compact)

    HnIcon {                                                  // column 1 — REQ-F-035, REQ-F-036
        Layout.alignment: Qt.AlignVCenter
        size: HnMetrics.iconSize(HnControlSize.Compact)
        source: "image://icon/" + row.iconName
    }
    ColumnLayout {                                            // column 2 — REQ-F-038
        Layout.fillWidth: true
        HnLabel { rawText: row.name }
        CapacityBar { opacity: row.capacityValid ? 1 : 0; fraction: row.capacityFraction } // REQ-F-043
        HnLabel { rawText: row.capacityValid ? row.capacityText : row.stateText
                  role: HnTypographyRole.Caption; color: HoloniightPalette.textMuted }
    }
    HnIconButton {                                            // column 3 — REQ-F-040
        Layout.alignment: Qt.AlignVCenter
        // One bundled eject glyph for every removal verb (REQ-F-014).
        icon.source: "qrc:/qt/qml/HolonightFiles/icons/eject-fallback.svg"
        visible: row.removalVerb >= 0                         // REQ-F-008
        enabled: !row.busy                                    // REQ-F-028
        Accessible.name: row.removalLabel                     // REQ-NF-002
        onClicked: root.controller.devices.remove(row.targetId)
    }
}
```

The fallback `HnIcon` pair from `PlacesPanel` (theme icon plus `hasError` qrc asset) is reused
verbatim for column 1 (REQ-F-037).

### 2.8 `CapacityBar` (new, inline) — `apps/files/qml/places/CapacityBar.qml`

REQ-C-005 rules out `Holonight.Controls`' `ProgressBar`, which fixes its indicator to the control
palette's `primary` role with no per-instance colour property. Two `Rectangle`s suffice:

```qml
Rectangle {
    id: root
    property real fraction: 0
    readonly property color fillColor: fraction >= 0.95 ? HoloniightPalette.warning        // REQ-F-047
                                     : fraction >= 0.90 ? HoloniightPalette.accentViolet   // REQ-F-048: inclusive
                                                        : HoloniightPalette.primary
    implicitHeight: 4
    radius: height / 2
    color: HoloniightPalette.surfaceRaised
    Rectangle {
        width: Math.round(parent.width * Math.max(0, Math.min(1, root.fraction)))
        height: parent.height
        radius: parent.radius
        color: root.fillColor
    }
}
```

Thresholds are `>=` against the used fraction, which is REQ-F-048 stated in code.

### 2.9 `DirectoryController` changes

Three additions, no restructuring:

```cpp
  Q_PROPERTY(SidebarNavigator* sidebarNavigator READ sidebarNavigator CONSTANT)
  Q_PROPERTY(bool sidebarActivationEnabled READ sidebarActivationEnabled NOTIFY changed)
  SidebarNavigator navigator_;   // constructed after places_ and devices_
```

`sidebarActivationEnabled` lifts the `activationEnabled` expression that both panels currently
duplicate in QML (Normal mode, no task prompt, no Quick Look) into one C++ property, so REQ-F-023
has a single definition rather than two copies.

## 3. Data Flow

**Removal, `x` on a focused External row:**

```
Flickable Keys.onPressed "x"
  └─ sidebarActivationEnabled? ──no──► ignored (REQ-F-023)
        │ yes
        ▼
  navigator.section == Devices? ──no──► ignored
        │ yes
        ▼
  DevicesModel::remove(targetId)
        ├─ row busy?  ──yes──► ignored (REQ-F-028)
        ├─ verb == -1 ──yes──► ignored, no error (REQ-F-022)
        └─ verb == PowerOff
              └─ controller_->powerOff(driveId, removalScope(driveId, true))   (REQ-F-026)
                    └─ StorageController::begin
                          ├─ scope mismatch ──► ScopeChanged ──► errorMessage (REQ-F-027)
                          └─ queues unmounts, then Drive.PowerOff
                                └─ operationFinished ──► refresh() ──► row disappears
```

**Capacity, first paint of a mounted row:**

```
refresh() ──► row mounted, capacity_ has no entry for its mount point
   └─ dispatch (worker): probe_->measure(mountPoint)          (REQ-F-042)
         └─ queued back to model thread, generation checked
               └─ capacity_[mountPoint] = value
                     └─ dataChanged(CapacityValid|Fraction|Text)
                           └─ bar and text become visible      (REQ-F-043)
```

Until that returns, `capacityValid` is false. The row retains a transparent bar slot and shows its
state text as a caption; the first paint never shows a bar at zero.

## 4. Key Decisions and Alternatives

### 4.1 Navigation: a cursor, not an aggregate model (SPEC Open Question 5)

**Chosen:** `SidebarNavigator` owns one cursor; both `ListView`s bind `currentIndex` to it and stop
handling keys.

*Aggregate model* (one `ListView` over a concatenating proxy) also satisfies REQ-F-049 structurally,
but the two delegates have almost nothing in common — a place row is an `HnListDelegate` with a
leading icon, a device row is a three-column layout with a capacity bar — so it would need a
`DelegateChooser` plus a union of both role sets plus index mapping for activation. That is
materially more machinery than the requirement needs.

*Per-list boundary handoff* keeps both lists autonomous but leaves REQ-F-049 as an invariant
maintained by hand in two `Keys` handlers, with `currentIndex` living in two places. The failure
mode — both sections showing a highlight — is exactly what the requirement exists to prevent.

The cursor approach makes the invariant structural (one position cannot be two) while leaving both
models and both delegates untouched, and it puts every navigation requirement in a plain `QObject`
that tests can drive directly.

### 4.2 Verb derivation returns `optional`, and lives in C++ not QML

`removalVerb()` returning `std::optional` means REQ-F-008 ("no verb, no control") cannot be
forgotten at a call site. Putting the Internal suppression (REQ-F-009) inside the same function
rather than in a QML `visible:` binding means an Internal drive advertising `canEject` cannot reach
`StorageController::eject` even if a future delegate change re-exposes a button.

### 4.3 Capacity is keyed on mount point, not volume id

A mount point is what `statvfs` takes, so keying the cache on it makes the query and the cache entry
agree by construction, lets two volumes at one path share a measurement, and makes eviction on
unmount a path removal. Volume-keyed caching would need invalidation logic for the same effect.

### 4.4 Refresh cadence: 30 s plus events (SPEC Open Question 4)

A 30 s timer runs while the model has mounted rows. Mount-point changes trigger a query
(REQ-F-044), and storage operation completion schedules a model refresh. The timer skips a path
whose query remains in flight, so it does not guarantee a maximum age for displayed figures.
The interval is a single named constant, per REQ-F-045's acceptance.

### 4.5 `activationEnabled` moves to C++

The expression is currently duplicated in `PlacesPanel.qml:13` and `DevicesPanel.qml:11`. With a
third consumer (the navigator's key handler) a single `sidebarActivationEnabled` property is cheaper
than a third copy and gives REQ-F-023 one place to be verified.

## 5. Test Strategy

All new tests join `files-smoke` via `tests/CMakeLists.txt`. Storage behavior uses `FakeStorage`,
and model/presentation capacity tests use a substituted probe. Only the `CapacityProbe.*` adapter
smoke tests query a temporary local directory; no test touches real UDisks2 hardware.

**New `tests/storage_policy_test.cpp`** — pure table-driven coverage of §2.1: REQ-F-001…009 and
REQ-F-036, including the Thunderbolt grey zone (External classification, both drive capabilities
false, falls through to Unmount) and an Internal drive advertising both capabilities (still Unmount).
REQ-C-001 is additionally checked by a source grep in the spirit of the existing literal-`toml++`
test.

**New `tests/sidebar_navigator_test.cpp`** — REQ-F-049…054 driven directly against the navigator with
both models populated: crossing down, crossing up, both clamped ends, crossing with an empty Devices
section, and the single-cursor invariant after every move.

**New `tests/capacity_probe_test.cpp`** — `StorageInfoCapacityProbe` against a known mount point for
`valid`/`bytesTotal` sanity, plus a nonexistent path returning `valid == false`.

**Extended `tests/devices_model_test.cpp`** — new `FakeStorage` fixtures for a four-LUN reader with
shared `SiblingId` (empty → zero rows per REQ-F-016; one card → one row), an optical drive empty and
loaded (REQ-F-017), and grouping (REQ-F-018, REQ-F-019). A substituted `CapacityProbe` covers
REQ-F-043…047. Three existing tests are adapted rather than deleted:
`PowerOffRejectsChangedVolumeScopeWithoutPartialUnmount` removes its confirmation step and
keeps its `ScopeChanged` assertion on a single-drive scope (REQ-F-027); `ModalGuardSuppressesActionsAndInvalidatesPendingActivation`
gains the `x` path (REQ-F-023); `MountOpensOnlyForCurrentActivationAndPreservesLocationOnFailure`
must pass unmodified (REQ-F-013).

**Extended `tests/places_window_test.cpp`** — sidebar composition as rendered: one scroll bar
(REQ-F-030), separator ordering and absence (REQ-F-031, REQ-F-032), content-sized sections
(REQ-F-033), row geometry for REQ-F-035, REQ-F-040 and REQ-F-043, and reveal-on-crossing (REQ-F-053).
Threshold colours are read back against the live `HoloniightPalette` singleton, never literals
(REQ-F-047).

## 6. Known Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Two content-sized `ListView`s inside a `Flickable` lose virtualisation | Both lists are small by nature (≈10 places, ≈5 devices). If a bookmark list ever grows large this becomes an aggregate-model question again. |
| Binding loop: `contentHeight` ← `ListView.contentHeight` ← delegate height ← row width ← `Flickable.width` | `Column.width` is bound to `root.width`, never the reverse; delegates size from `column.width` only. The technique and the loop it avoids are the ones recorded for the info sidebar's `SplitView`. |
| `itemAtIndex()` returns null for a delegate outside the reuse window | `reveal()` no-ops on null. With `interactive: false` and `height: contentHeight` every delegate is realised, so this is a guard, not a path. |
| Dropping `CanUnmount`/`CanEject`/`CanPowerOff` roles breaks an out-of-tree QML consumer | These roles exist only for `DevicesPanel.qml`; the Shell consumer uses `StorageController` directly and is untouched (SPEC "Not in Scope"). |
| `x` swallowed before reaching the sidebar | `WindowEventFilter` intercepts only while a task prompt is open, and REQ-F-023 suppresses the binding in exactly that state, so the two agree. |
| PowerOff reaches sibling drives | Medium-removable drives never fall back to PowerOff. Self-removable drives use a controller-derived scope guard when rows are built and again on invocation; focused regressions cover both paths (T-015). |
| Empty optical row offers PowerOff without Eject | The medium-removable fallback returns no verb when the empty drive cannot Eject; a focused regression covers `canPowerOff = true` (T-015). |
| Concatenated sort key splits one drive's rows | Separate `(classRank, driveId, targetId)` fields keep overlapping IDs in contiguous groups; a focused regression covers the old failure (T-016). |
| Device glyphs render dark on dark | Accepted, not worked around. SPEC "Deferred to follow-up work" item 1; `holonight-qt/docs/icons-recoloring-issue.md`. |

## 7. File Change List

**New**

| Path | Purpose |
|---|---|
| `apps/files/storage/storage_policy.h` | §2.1 classification, verb, icon name |
| `apps/files/storage/capacity_probe.h` / `.cpp` | §2.2 probe interface + `QStorageInfo` impl |
| `apps/files/presentation/sidebar_navigator.h` / `.cpp` | §2.5 cursor |
| `apps/files/qml/places/SidebarPanel.qml` | §2.6 container |
| `apps/files/qml/places/CapacityBar.qml` | §2.8 |
| `apps/files/icons/eject-fallback.svg`, `drive-fallback.svg` | REQ-F-037 |
| `tests/storage_policy_test.cpp`, `sidebar_navigator_test.cpp`, `capacity_probe_test.cpp` | §5 |

**Modified**

| Path | Change |
|---|---|
| `apps/files/storage/devices_model.h` / `.cpp` | §2.3, §2.4 — roles, `remove()`, visibility, grouping, capacity |
| `apps/files/qml/places/DevicesPanel.qml` | §2.7 — three-column row; confirmation UI deleted |
| `apps/files/qml/places/PlacesPanel.qml` | content-sized, non-interactive, no own scroll bar, no key nav |
| `apps/files/qml/Main.qml` | `97-113` replaced by one `SidebarPanel` |
| `apps/files/application/directory_controller.h` / `.cpp` | §2.9 |
| `apps/files/CMakeLists.txt` | new sources, icon list (`113`), and the explicit `module_qml` list (`100-106`) — this list feeds both `QML_FILES` and the `qml-lint` target, so a new `.qml` file is invisible to both until added |
| `tests/CMakeLists.txt` | three new test sources in `files-smoke` |
| `tests/devices_model_test.cpp`, `places_window_test.cpp` | §5 |
| `docs/sdd/udisks2-storage/SPEC.md` | note the R3/R4 amendments |

## Related Documents

- [SPEC.md](SPEC.md) — requirements
- [udisks2-storage/SPEC.md](../udisks2-storage/SPEC.md) — amended R3, R4
- [places-sources/DESIGN.md](../places-sources/DESIGN.md) — async dispatch precedent
- `holonight-qt/docs/icons-recoloring-issue.md` — deferred upstream defect
