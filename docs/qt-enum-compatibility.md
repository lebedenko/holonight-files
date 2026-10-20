# Qt enum and drag-event compatibility

The fourteen Q_ENUM declarations shared by Files and File Browser use Qt-recognized
quint8/qint8 base types. Names, values, signedness, width, scoped registration and
QML_FOREIGN wrappers are preserved. Unregistered enums remain unchanged.

Drag tests adapt integer coordinates to QPointF on Qt 6.12 and retain QPoint on
Qt 6.11. The minimum supported Qt version and application behavior remain unchanged.
The generated metadata check rejects std::uint8_t/std::int8_t spellings while
retaining export checks. Never edit generated metadata or suppress diagnostics.

Stage the updated provider first, then configure Files with its CMAKE_PREFIX_PATH,
QML_IMPORT_PATH and runtime library path. Regenerate registration for all five Files
executables; capture diagnostics and check metadata even if warnings do not reproduce.
Run scoped enum, drag, sidebar, places, devices, task, Vim and Quick Look regressions,
QML lint, both component acceptance gates and isolated installed-runtime acceptance.
Qt 6.11 and Qt 6.12 builds require separate directories; record missing toolchains.
See the [provider note](../../holonight-file-browser/docs/qt-enum-compatibility.md)
and [umbrella contract](../../docs/qt-enum-compatibility.md).
