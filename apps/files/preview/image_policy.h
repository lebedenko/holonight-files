#pragma once
#include <QImageReader>

#include <holonight_images/image.h>
#include <limits>
// Preserve Files' source memory guard and stored-pixel orientation during migration.
inline constexpr HolonightImages::Limits kPreviewImageLimits{.inputBytes = std::numeric_limits<qint64>::max(),
                                                             .sourcePixels = (256LL * 1024 * 1024) / 4,
                                                             .sourceExtent = std::numeric_limits<int>::max(),
                                                             .decodedBytes = 256LL * 1024 * 1024,
                                                             .metadataBytes = 1024 * 1024,
                                                             .tiffMetadataBytes = 64 * 1024 * 1024,
                                                             .metadataRecords = 4096};

inline void configurePreviewImageLimits() {
  qputenv("QT_IMAGEIO_MAXALLOC", "256");
  QImageReader::setAllocationLimit(256);
}
