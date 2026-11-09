#include <QImageIOPlugin>
#include <QVariant>

#include <cstdlib>

namespace {
bool matches(QIODevice* device) {
  return device != nullptr && device->peek(18) == QByteArray("\0HN-UNKNOWN-SIZE\xff\x01\x02", 18);
}

class Handler : public QImageIOHandler {
 public:
  [[nodiscard]] bool canRead() const override {
    if (!matches(device())) {
      return false;
    }
    setFormat("tiff");
    return true;
  }
  [[nodiscard]] bool supportsOption(ImageOption option) const override { return option == Size; }
  [[nodiscard]] QVariant option(ImageOption option) const override {
    return option == Size ? QVariant(device()->peek(19).endsWith('K') ? QSize(2, 3) : QSize()) : QVariant();
  }
  bool read(QImage* image) override {
    if (!device()->peek(19).endsWith('K')) {
      std::_Exit(86);  // The helper must reject unknown dimensions before pixel decoding.
    }
    *image = QImage(2, 3, QImage::Format_RGB32);
    image->fill(Qt::red);
    return true;
  }
};
}  // namespace

class UnknownDimensionsPlugin : public QImageIOPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "handler.json")
 public:
  Capabilities capabilities(QIODevice* device, const QByteArray& format) const override {
    return (format.isEmpty() || format == "tiff") && matches(device) ? CanRead : Capabilities{};
  }
  QImageIOHandler* create(QIODevice* device, const QByteArray& format) const override {
    auto* handler = new Handler;
    handler->setDevice(device);
    handler->setFormat(format);
    return handler;
  }
};

#include "handler.moc"
