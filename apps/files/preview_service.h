#pragma once

#include "exif_reader.h"
#include "text_preview_service.h"

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>

// The value type that crosses the worker->UI queued connection. Defined in preview_service.cpp —
// only PreviewService's own dispatch()/applyResult() ever construct or inspect one.
struct PreviewResult;
struct PreviewWorkerCache;

// The async engine and the single QML-facing object for everything Stage 2 adds. Owned as a
// value member of DirectoryController, structurally mirroring DirectoryModel: one persistent
// worker QObject moved to one dedicated QThread, a generation counter compared only on the UI
// thread, a cancellation token replaced on every new target (SPEC.md REQ-C-004).
class PreviewService : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
 public:
  // Nested (rather than a free-standing enum merely Q_ENUM'd from inside the class) so QML's
  // type resolver associates PreviewErrorKind with PreviewService's meta-object correctly —
  // referenced from QML as `PreviewService.None`, `PreviewService.DecodeTimeout`, etc.
  enum class PreviewErrorKind { None, PermissionDenied, BrokenSymlink, DecodeFailed, DecodeTimeout, Unsupported };
  Q_ENUM(PreviewErrorKind)

  enum class PreviewConsumer { Pane, QuickLook };
  Q_ENUM(PreviewConsumer)

  struct PreviewError {
    PreviewErrorKind kind = PreviewErrorKind::None;
    QString message;
  };

  Q_PROPERTY(bool hasEntry READ hasEntry NOTIFY changed)
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(qint64 size READ size NOTIFY changed)
  Q_PROPERTY(QDateTime modified READ modified NOTIFY changed)
  Q_PROPERTY(QString permissions READ permissions NOTIFY changed)
  Q_PROPERTY(QString mimeType READ mimeType NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
  Q_PROPERTY(QImage image READ image NOTIFY changed)
  Q_PROPERTY(QSize sourcePixelSize READ sourcePixelSize NOTIFY changed)
  Q_PROPERTY(bool exifPresent READ exifPresent NOTIFY changed)
  Q_PROPERTY(QString exifMake READ exifMake NOTIFY changed)
  Q_PROPERTY(QString exifModel READ exifModel NOTIFY changed)
  Q_PROPERTY(QString exifExposureTime READ exifExposureTime NOTIFY changed)
  Q_PROPERTY(QString exifIso READ exifIso NOTIFY changed)
  Q_PROPERTY(QString exifFocalLength READ exifFocalLength NOTIFY changed)
  Q_PROPERTY(bool hasText READ hasText NOTIFY changed)
  Q_PROPERTY(QString textContent READ textContent NOTIFY changed)
  Q_PROPERTY(bool textTruncated READ textTruncated NOTIFY changed)
  Q_PROPERTY(qint64 textTotalSize READ textTotalSize NOTIFY changed)
  Q_PROPERTY(PreviewErrorKind previewErrorKind READ previewErrorKind NOTIFY changed)
  Q_PROPERTY(QString previewErrorMessage READ previewErrorMessage NOTIFY changed)

  explicit PreviewService(QObject* parent = nullptr);
  ~PreviewService() override;

  bool hasEntry() const { return has_entry_; }
  QString name() const { return name_; }
  qint64 size() const { return size_; }
  QDateTime modified() const { return modified_; }
  QString permissions() const { return permissions_; }
  QString mimeType() const { return mime_type_; }
  bool busy() const { return busy_; }
  bool hasImage() const { return !display_image_.isNull(); }
  QImage image() const { return display_image_; }
  QSize sourcePixelSize() const { return source_pixel_size_; }
  bool exifPresent() const { return exif_.present; }
  QString exifMake() const { return exif_.make; }
  QString exifModel() const { return exif_.model; }
  QString exifExposureTime() const { return exif_.exposureTime; }
  QString exifIso() const { return exif_.iso; }
  QString exifFocalLength() const { return exif_.focalLength; }
  bool hasText() const { return has_text_; }
  QString textContent() const { return text_.content; }
  bool textTruncated() const { return text_.wasTruncated; }
  qint64 textTotalSize() const { return text_.totalSize; }
  PreviewErrorKind previewErrorKind() const { return error_.kind; }
  QString previewErrorMessage() const { return error_.message; }

  // Called only by DirectoryController::syncPreviewTarget(). Formats generic metadata
  // synchronously on the UI thread with zero I/O and emits changed() before returning.
  void setTarget(const QString& path, bool isDir, qint64 size, const QDateTime& modified, quint32 mode, bool statFailed,
                 const QString& statError, quint64 revision = 0);
  void clear();  // REQ-F-009
  Q_INVOKABLE void setRequestedSize(PreviewConsumer consumer, QSize pixels);
  void setQuickLookActive(bool active);  // REQ-F-004/005, called by both QML consumers
  void shutdown();

 signals:
  void changed();
  void shutdownFinished();

 private:
  friend struct PreviewServiceTestAccess;
  void dispatch();
  void startJob();
  void updateRequestedSize();
  void applyResult(const PreviewResult& result);
  void cancelInFlight();
  void resetDisplayState();
  // Snapshotted on the UI thread before dispatch; lets tests deterministically exercise the
  // 3-second decode timeout without a pathological fixture (see DirectoryModel's analogous
  // before_open_for_test_ seam).
  std::function<void()> before_dispatch_for_test_;
  std::function<void()> before_full_decode_for_test_;
  std::shared_ptr<PreviewWorkerCache> cache_;
  bool active_job_ = false;
  bool pending_job_ = false;

  QThread thread_;
  QObject* worker_;

  bool has_entry_ = false;
  QString path_;
  QString name_;
  qint64 size_ = -1;
  QDateTime modified_;
  QString permissions_;
  QString mime_type_;
  bool is_dir_ = false;
  quint32 mode_ = 0;
  bool stat_failed_ = false;
  QString stat_error_;
  quint64 revision_ = 0;

  bool busy_ = false;
  bool timed_out_ = false;
  QImage thumbnail_image_;
  QImage full_image_;
  QImage display_image_;
  QSize source_pixel_size_;
  ExifReader::ExifSummary exif_;
  bool has_text_ = false;
  TextPreviewService::TextPreviewResult text_;
  PreviewError error_;

  quint64 generation_ = 0;
  std::shared_ptr<std::atomic_bool> cancellation_;
  QTimer timeout_timer_;

  QSize requested_size_;
  QSize pane_size_;
  QSize quick_look_size_;
  QSize dispatched_size_;
  bool quick_look_active_ = false;
  QTimer resize_debounce_timer_;

  bool stopping_ = false;
};
