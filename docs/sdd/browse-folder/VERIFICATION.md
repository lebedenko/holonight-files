# Browse-folder verification

Review date: 2026-09-09. The supplied implementation plan authorized this review
scope under CONTRIBUTING.md. Existing workspace changes were preserved; provider
sources were not edited. Providers were consumed from `build/deps/prefix` after
`task deps`. Generated logs, fixtures, screenshots, staging trees and benchmark XML
are under `build/` and are not tracked.

## Environment

- Native Linux Wayland session, Qt 6.11.2, OpenGL scenegraph (`graphics_api=3`).
- Intel Core i9-9900K, 16 logical CPUs; approximately 64 GiB installed memory;
  NVIDIA GeForce GTX 1660 SUPER host GPU; local XFS fixture filesystem.
- Linux 7.2.4-arch1-2; CMake 4.4.3; clang-format 22.1.8. Detailed host metadata:
  `build/review-environment.json`.
- Functional smoke coverage: 32 tests passed; the one opt-in native benchmark was
  skipped in offscreen runs and passed separately below. All five CTest cases passed.
- Functional tests ran as UID 1000, under both `LC_ALL=C.UTF-8` and
  `LC_ALL=en_US.UTF-8`. Permission assertions executed; none were skipped for root.
- Functional/visual checks use Qt offscreen and the software rendering setting.
  Native acceptance uses the exposed production window and default OpenGL renderer,
  with no concurrent builds during the recorded passing run. The test preset is Debug.

## Commands and outcomes

| Command | Result | Local evidence |
| --- | --- | --- |
| `task deps` | Pass; installed providers refreshed without source changes | `build/deps/` |
| `task build` | Pass | `build/review-build.log` |
| `LC_ALL=C.UTF-8 task test` | Pass, all 5 CTest cases | `build/review-test-C.log` |
| `LC_ALL=en_US.UTF-8 task test` | Pass, all 5 CTest cases | `build/review-test-en_US.log` |
| `task build PRESET=release` | Pass | `build/review-release.log` |
| `task format`, `task format-check` | Pass, includes application/test headers | `build/review-format.log`, `build/review-format-check.log` |
| `cmake --build build/test --target format-check` | Pass, includes headers | `build/review-cmake-format-check.log` |
| `task tidy` | Pass | `build/review-tidy.log` |
| `task qml-lint` | Pass | `build/review-qml-lint.log` |
| `task license-check` | Pass; GPL-3.0-or-later coverage via REUSE.toml | `build/review-license.log` |
| `task install-check` | Pass; staged executable/version and packaging checks | `build/review-install.log` |
| `task desktop-check` | Pass; isolated development registration/build switching and packaged entry separation | `build/review-desktop.log` |
| `task visual-check` | Pass; dark/light, scales 1 / 1.25 / 1.5, populated and error captures | `build/review-visual.log`, `build/visual/` |

REUSE's first sandboxed run could not create its multiprocessing socket; the
same `task license-check` passed with authorized execution outside the sandbox.
Hosted CI itself was not run here. Its workflow now runs functional tests using
`runuser -u files-test` in both locales, and the CI image creates that user and
the English UTF-8 locale. Provider and container-image pinning remain deferred.

## Requirement and task evidence

| Tasks | Requirements | Passing evidence |
| --- | --- | --- |
| T-001, T-004, T-014 | F-001, F-002 | Async load, empty/10k fixtures, multiple incremental batches, stale-generation rejection; native rendered rows and interaction below |
| T-002, T-013 | F-004, F-005, C-002, C-003 | Real before-open removal and chmod races, unreadable directories, injected EIO after partial enumeration; dangling vs inaccessible symlink targets, valid target metadata, Unicode fixtures |
| T-003, T-015 | F-003, F-008 | Natural/case-insensitive order in both process locales, directory grouping and full descending reversal, unchanged global locale, fixed standard places |
| T-005, T-016 | F-009–F-013 | Controller grammar tests and real production-window key events, including huge counts, zero counts, rejected Unicode digits, `gg`/`G`, parent navigation and viewport-visible cursor |
| T-006 | F-006 | Window `l`, Return and Enter navigate into a real child; temporary QDesktopServices file handler captures the exact local URL; handler restored by scope guard |
| T-007 | F-014, F-015 | Actual `.` / `s` events change listing/filter/order and consume pending counts/chords; footer/README document both |
| T-008 | F-007 | Watcher create/rename/delete tests never call refresh directly; diff tests verify no-op silence, one changed metadata notification, batched insertions and contiguous removals; failed refreshes retain unseen rows |
| T-009, T-010 | C-001 | Shared startup helper covers valid, absent, missing, file-valued and unreadable arguments; CTest checks excess-argument rejection, help/version and QML-load failure; shutdown tests cover in-flight and blocked-delivery cancellation |
| T-011, T-012 | F-001–F-005, F-008 | Production-window populated delegates, selected-row visibility/highlight, dangling-link warning item, inline directory error and attempted-path label; screenshot inspection |
| T-017 | NF-001 | Native timing results below; all three limits passed with sufficient interaction samples |
| T-018 | C-001–C-003, F-001–F-015, NF-001 | Combined functional, native, formatting/header, quality, installation and visual checks recorded here |

IDs in this table omit the common `REQ-` prefix.

## Native rendering acceptance (T-017)

Command, run directly because CTest deliberately selects offscreen:

```sh
QT_QPA_PLATFORM=wayland QSG_INFO=1 FILES_BROWSE_BENCHMARK=1 \
  LC_ALL=en_US.UTF-8 \
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib" \
  build/test/tests/files-smoke '--gtest_filter=DirectoryPerformance.*' \
  --gtest_output=xml:build/review-native.xml > build/review-native.log 2>&1
```

| Measurement | Observed | Acceptance |
| --- | --- | --- |
| Real temporary fixture | 12,000 entries | ≥10,000 |
| Navigation to first swapped frame containing a synchronized, visible populated delegate | 55.620ms | ≤150ms |
| Largest frame interval during active scrolling while loading | 30.755ms | ≤100ms |
| Largest posted-key to cursor-update latency while loading | 18.887ms | ≤100ms |
| Scrolling frames / input samples | 15 / 20 | ≥10 each |
| Load duration | 422ms | ≥200ms sampling duration |
| Maximum scroll offset from ListView origin | 3,494px | Evidence of actual scrolling |
| Watcher create-to-settled refresh | 302ms | Separate measurement; no new threshold |

Result: **pass**, `build/review-native.xml` and `build/review-native.log`.
The benchmark checks `beforeSynchronizing`/`frameSwapped`, delegate geometry and
viewport intersection; it does not treat rowCount or timer ticks as rendered rows.
It posts real `5j` key events while the model is scanning. The fixture increases to
48k and 192k if the sample is insufficient; an insufficient sample cannot pass
acceptance, and measured limit violations fail even with too few samples.

Exploratory measurements exposed UI queue starvation at 192k entries (approximately
150ms frame / 142ms input gaps). Bounded, cancellable delivery of at most two batches
and immediate current-row positioning corrected this. Earlier incomplete samples
and the initial row-count/timer benchmark are not acceptance evidence. The final
12k run above is the passing acceptance fixture; performance on every directory,
filesystem, renderer or hardware configuration is not established by this run.
FrameSwapped is a Qt presentation boundary, not a physical display scanout measurement.
Offscreen regressions are not native rendering acceptance. No native gate is left
pending for the recorded host; repeat this opt-in gate on other target environments.

## Visual evidence

`task visual-check` captures `*-populated.png` and `*-error.png` for dark/light
appearance at 1, 1.25 and 1.5 scale, plus the existing small/large shell captures.
Inspected populated/error captures show the fixed Places sidebar, file metadata,
selected-row highlight, discoverable `. hidden` / `s reverse sort` footer, and an
inline error retaining the attempted path. Representative local artifacts:

- `build/visual/dark-1-populated.png`
- `build/visual/dark-1-error.png`
- `build/visual/light-1.25-populated.png`
- `build/visual/light-1.25-error.png`

## Collation reference

The C-locale fallback changes only the collator to English/US. Qt documents that
its POSIX fallback backend lacks numeric and case-insensitive collation, so flags
alone do not prove natural sorting. Behavioral tests pass on this Qt build in both
locales; a Qt build without a capable backend remains a platform limitation.
[Qt QCollator documentation](https://doc.qt.io/qt-6/qcollator.html#posix-fallback-implementation).
