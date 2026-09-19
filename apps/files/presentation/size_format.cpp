#include "size_format.h"

#include <QCoreApplication>

#include <array>

// QML invokes this method through the engine-owned singleton instance.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
QString SizeFormat::formatSize(qint64 bytes) const {
  if (bytes < 0) {
    return {};
  }
  if (bytes < 1024) {
    return QCoreApplication::translate("SizeFormat", "%1 B").arg(bytes);
  }
  constexpr std::array units{"KB", "MB", "GB", "TB"};
  auto value = static_cast<double>(bytes) / 1024;
  std::size_t unit = 0;
  while (value >= 1024 && unit < units.size() - 1) {
    value /= 1024;
    ++unit;
  }
  return QCoreApplication::translate("SizeFormat", "%1 %2")
      .arg(QString::number(value, 'f', 1), QString::fromLatin1(units.at(unit)));
}
