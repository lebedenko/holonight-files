# Scoped preview enums

Files uses the provider's scoped-only PreviewService QML contract:
`PreviewService.PreviewErrorKind.None` for error comparisons and
`PreviewService.PreviewConsumer.Pane` for preview sizing. Quick Look consumers use
`PreviewService.PreviewConsumer.QuickLook`; eligibility values use
`PreviewService.QuickLookEligibility`, including Unsupported.

Flattened aliases (`PreviewService.None`, `PreviewService.Pane`,
`PreviewService.QuickLook`, `PreviewService.Unsupported`) are intentionally unavailable.
PreviewErrorKind.Unsupported and QuickLookEligibility.Unsupported have different
values and must retain their enum scopes. C++ enums, properties and preview behavior
are unchanged. The existing QML_FOREIGN registration remains in place.

Build against the updated holonight-file-browser installation using CMAKE_PREFIX_PATH,
with matching QML_IMPORT_PATH and runtime library paths. Run
`ctest --test-dir build/dev -R '^files-preview-scoped-enums$' --output-on-failure`
for a fresh-process regression using the actual HolonightFiles module. It checks
scoped values, absent aliases and enum-clash diagnostics without opening a window.
See the [provider note](../../holonight-file-browser/docs/preview-enum-registration.md)
and [umbrella integration note](../../docs/preview-enum-registration.md) for dependency
order and wider acceptance. Historical SDDs describe their original interfaces.
