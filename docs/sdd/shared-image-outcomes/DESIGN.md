# Design

Depend on unchanged HolonightImages `3633865d2f39e4f163f0159a0f252f88245379f0`. Keep typed outcomes in internal worker values and format raster errors at UI acceptance. Metadata status belongs to the existing EXIF value, travels with facts, and never creates a notice.

Files thumbnail results require an explicit outcome. Optional cache lookup distinguishes misses from cancellation; invalid entries fall back to source decode. Append ResourceLimit and IoFailure without renumbering existing enums. Metadata continues to be reread.

## Changed files

- `apps/files/preview/exif_reader.cpp`
- `apps/files/preview/exif_reader.h`
- `apps/files/preview/preview_service.cpp`
- `apps/files/preview/preview_service.h`
- `apps/files/preview/thumbnail_service.cpp`
- `apps/files/preview/thumbnail_service.h`
- `tests/exif_reader_test.cpp`
- `tests/preview_decode_limits_test.cpp`
- `tests/preview_service_test.cpp`
- `tests/preview_service_test_access.h`
- `tests/thumbnail_service_test.cpp`
