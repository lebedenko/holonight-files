# Design

Approved implementation plan: 2026-09-16; REQ-001–006.

Use default-constructible QObject types with QML_ELEMENT and QML_SINGLETON in the
existing static QML module. The engine constructs and owns each instance.
SizeFormat exposes formatSize(qint64), explicit SizeFormat translation context and
fixed decimal-point formatting. IconFallbacks owns a QSet<QString> without signals.
InspectionKeys exposes scalar key/text/modifiers/autoRepeat inputs, controller pointer
and popup flag; press/release/overrideShortcut return event acceptance. Null pointers
skip dispatch. Popup history shortcuts retain bitwise Control precedence.
QML retains Keys.BeforeItem and sets accepted only on true. Retain deferred failedChain
updates. Register sources, delete JS resources/files, preserve historical SDD records.
Direct tests exercise boundaries, engine ownership and routing; rendered tests exercise
size text, fallback behavior, keyboard focus and fullscreen integration. New material
uses repository-wide GPL-3.0-or-later metadata from REUSE.toml.
