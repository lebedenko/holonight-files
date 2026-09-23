#include <QFile>
#include <QImageIOPlugin>
#include <QVariant>

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
    setFormat("hnunknownsize");
    return true;
  }
  [[nodiscard]] bool supportsOption(ImageOption option) const override { return option == Size; }
  [[nodiscard]] QVariant option(ImageOption option) const override {
    return option == Size ? QVariant(QSize()) : QVariant();
  }
  bool read(QImage* image) override {
    QFile marker(qEnvironmentVariable("FILES_UNKNOWN_SIZE_READ_MARKER"));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Append) || marker.write("read\n") != 5) {
      return false;
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
    return format == "hnunknownsize" || (format.isEmpty() && matches(device)) ? CanRead : Capabilities{};
  }
  QImageIOHandler* create(QIODevice* device, const QByteArray& format) const override {
    auto* handler = new Handler;
    handler->setDevice(device);
    handler->setFormat(format);
    return handler;
  }
};

#include "handler.moc"
