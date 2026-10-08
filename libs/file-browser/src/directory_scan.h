#pragma once

#include <QList>

#include <HolonightFileBrowser/directory_entry.h>
#include <HolonightFileBrowser/places/place_list.h>
#include <atomic>
#include <functional>
#include <memory>

namespace HolonightFileBrowser::detail {
using PublishBatch = std::function<void(QList<DirectoryEntry>, bool, QString)>;
// Worker-only enumeration. Callbacks execute on the calling worker. The optional failure
// seams are used by Files' existing regressions; normal consumers use DirectoryReader.
void walkDirectory(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel,
                   const std::function<void()>& beforeOpen, int readErrorAfter,
                   const std::shared_ptr<const PlaceList::IconMap>& places, const PublishBatch& publish,
                   const QByteArray& nativePath = {});
DirectoryEntry syntheticParentEntry(const QString& path, const PlaceList::IconMap* places = nullptr,
                                    const QByteArray& nativeParent = {});
}  // namespace HolonightFileBrowser::detail
