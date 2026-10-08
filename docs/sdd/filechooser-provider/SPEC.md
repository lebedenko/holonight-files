# Requirements

- FB-001: When a consumer requests a directory, Core shall enumerate immediate entries on a worker and reject stale batches.
- FB-002: Core shall preserve Files' directories-first natural sorting, hidden filtering, parent entries and icon metadata.
- FB-003: Core shall provide Home/XDG standard places without bookmarks, devices or application persistence.
- FB-004: Quick shall expose a passive listing with model, cursor, selected paths and delegate extension inputs and interaction signals.
- FB-005: Files shall adopt the shared worker/sorting/view while preserving its browsing, navigation and editing behavior and HolonightFiles module.
- FB-006: A consumer shall find_package(HolonightFileBrowser COMPONENTS Core Quick) after installation without source-tree includes.
- FB-007: Provider-only builds shall not require Images, Thumbnails, Search, Storage, TOML or the Files executable.
- FB-008: Core-only builds shall not require HolonightQt or Qt Quick and shall support Core-only installed consumers.
- FB-009: Consumers shall be able to cancel delivery and retire a worker asynchronously without nested event loops.
- FB-010: Core shall expose native path bytes and support native directory input without UTF-8 round trips, so the backend can preserve protocol filenames.
