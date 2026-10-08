#pragma once

#include <QCache>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QString>

// Serves image://icon/<name1>/<name2>/... (SPEC.md REQ-F-002): the '/'-joined candidate chain
// produced by IconNameResolver and carried by DirectoryModel::IconNameRole. Each candidate is tried
// with QIcon::fromTheme() in order (REQ-F-001); the first theme hit is returned, and a total miss
// returns a null pixmap so the consuming HnIcon reports hasError and QML shows the bundled glyph
// (REQ-F-016). Pixmap-type (not Image, not async): Qt Quick only guarantees GUI-thread calls for
// Pixmap providers, which QIconLoader requires (REQ-NF-001) — so no locking is needed here.
class IconImageProvider final : public QQuickImageProvider {
 public:
  IconImageProvider();
  QPixmap requestPixmap(const QString& chain, QSize* size, const QSize& requestedSize) override;

 private:
  friend struct IconImageProviderTestAccess;
  // Key "<name>@<w>x<h>" (REQ-F-020). Misses are cached as null pixmaps too, so a name the theme
  // lacks costs one theme lookup per pixel size, not one per row.
  QCache<QString, QPixmap> cache_;
  int theme_lookup_count_for_test_ = 0;
};
