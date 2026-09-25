# UDisks2 storage — I-003

Approved by the user-provided implementation plan and continuation on 2026-09-24.
Baseline: 672b7193fd5523dd5d52dabe0c666044f81b9ab7. Provider: 5f2ecda7eea653995f4c860bfb7f3a3f53beb279, published and pinned.

- R1: The application shall instantiate its own Storage controller and own all presentation/filtering.
- R2: When storage changes, the view shall reflect current provider facts, excluding ignored, loop, container,
  swap and OS infrastructure volumes; HintSystem alone shall not hide data volumes.
- R3: Shell shall show removable devices with media; Files shall additionally show empty removable drives and
  mounted fixed data volumes. Locked volumes shall be unavailable; unlocked backing rows shall not duplicate them.
- R4: Manual actions shall use opaque IDs, display busy/error state, and expose capability-appropriate actions.
  Power-off shall display the full affected scope before confirmation and reject stale confirmation.
- R5: Files shall mount before navigating and discard a completion after superseding navigation or activation.
  Removal/unmount of the displayed filesystem shall cancel stale work and return Home with an explanation.
- R6: Files shall preserve keyboard access and modal guards. Shell shall use the existing status-popup framework
  and hide the storage indicator when no eligible device remains.

Automatic mounting, unlock, disk administration and network/FUSE discovery remain excluded.

## Amendments

Amended by the [device-actions](../device-actions/SPEC.md) cycle (2026-09-25); R1, R2, R5 and R6 are unaffected.

- R3 (Files): narrowed. Empty **optical** drives remain shown, since opening the tray is the one action on
  them; empty media-removable non-optical slots (card-reader LUNs) are hidden (device-actions REQ-F-016,
  REQ-F-017). Unmounted internal volumes stay hidden (REQ-F-015).
- R4: the power-off confirmation is removed (REQ-F-025). Each row offers at most one removal action, chosen
  by drive class and `MediaRemovable` rather than one button per capability. Stale-scope rejection is
  retained and now guards a scope derived at the moment of invocation: a change before the controller acts
  is still rejected with `ScopeChanged` and leaves every affected volume mounted (REQ-F-026, REQ-F-027).
