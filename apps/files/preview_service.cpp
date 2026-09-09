#include "preview_service.h"

#include "exif_reader.h"
#include "text_preview_service.h"
#include "thumbnail_service.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>

#include <cstring>
#include <fcntl.h>
#include <list>
#include <sys/stat.h>
#include <unistd.h>

// The value type that crosses the worker->UI queued connection. Only PreviewService's own
// dispatch()/applyResult() (and the free runPreviewJob() worker function below) ever touch one.
struct PreviewResult {
  quint64 generation = 0;
  bool final = true;
  QString mime_type;
  QImage thumbnail_image;
  QImage full_image;
  QSize source_pixel_size;
  ExifReader::ExifSummary exif;
  bool has_text = false;
  TextPreviewService::TextPreviewResult text;
  PreviewService::PreviewError error;
};

struct PreviewWorkerCache {
  struct Entry {
    QString key;
    QSize size;
    QImage image;
  };
  std::list<Entry> entries;
  qint64 bytes = 0;
  QImage lookup(const QString& key, QSize size) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->key == key && it->size == size) {
        auto image = it->image;
        entries.splice(entries.begin(), entries, it);
        return image;
      }
    }
    return {};
  }
  void insert(const QString& key, QSize size, const QImage& image) {
    constexpr qint64 budget = 64LL * 1024 * 1024;
    if (image.isNull() || image.sizeInBytes() > budget) {
      return;
    }
    while (!entries.empty() && (entries.size() >= 2 || bytes + image.sizeInBytes() > budget)) {
      bytes -= entries.back().image.sizeInBytes();
      entries.pop_back();
    }
    entries.push_front({.key = key, .size = size, .image = image});
    bytes += image.sizeInBytes();
  }
};

namespace {
constexpr int kDecodeTimeoutMs = 3000;
constexpr int kResizeDebounceMs = 150;
constexpr qint64 kTextHeadBytes = 65536;
constexpr qint64 kSniffBytes = 8192;

QString formatPermissions(quint32 rawMode) {
  const auto mode = static_cast<mode_t>(rawMode);
  QString result = QStringLiteral("----------");
  if (S_ISDIR(mode)) {
    result[0] = u'd';
  } else if (S_ISLNK(mode)) {
    result[0] = u'l';
  } else if (S_ISCHR(mode)) {
    result[0] = u'c';
  } else if (S_ISBLK(mode)) {
    result[0] = u'b';
  } else if (S_ISFIFO(mode)) {
    result[0] = u'p';
  } else if (S_ISSOCK(mode)) {
    result[0] = u's';
  }
  const auto setBit = [&](int index, quint32 bit, QChar letter) {
    if ((rawMode & bit) != 0) {
      result[index] = letter;
    }
  };
  setBit(1, S_IRUSR, u'r');
  setBit(2, S_IWUSR, u'w');
  setBit(3, S_IXUSR, u'x');
  setBit(4, S_IRGRP, u'r');
  setBit(5, S_IWGRP, u'w');
  setBit(6, S_IXGRP, u'x');
  setBit(7, S_IROTH, u'r');
  setBit(8, S_IWOTH, u'w');
  setBit(9, S_IXOTH, u'x');
  if ((rawMode & S_ISUID) != 0) {
    result[3] = (rawMode & S_IXUSR) != 0 ? u's' : u'S';
  }
  if ((rawMode & S_ISGID) != 0) {
    result[6] = (rawMode & S_IXGRP) != 0 ? u's' : u'S';
  }
  if ((rawMode & S_ISVTX) != 0) {
    result[9] = (rawMode & S_IXOTH) != 0 ? u't' : u'T';
  }
  return result;
}

PreviewResult decodeImage(QFile& file, const QString& path, const QString& identity, PreviewResult result,
                          const std::shared_ptr<std::atomic_bool>& cancel, QSize requestedSize,
                          PreviewWorkerCache& cache, const std::function<void(const PreviewResult&)>& publish,
                          const std::function<void()>& beforeFullDecode) {
  QString thumbError;
  result.thumbnail_image = ThumbnailService::lookupOrDecode(file, path, identity, &thumbError);
  if (cancel->load()) {
    return result;
  }
  file.seek(0);
  {
    const QImageReader sizer(&file);
    result.source_pixel_size = sizer.size();
  }
  if (!result.thumbnail_image.isNull()) {
    result.final = false;
    publish(result);
    result.final = true;
  }
  result.full_image = cache.lookup(identity, requestedSize);
  QString fullError;
  if (result.full_image.isNull()) {
    if (beforeFullDecode) {
      beforeFullDecode();
    }
    if (cancel->load()) {
      return result;
    }
    result.full_image = ThumbnailService::decodeScaled(file, requestedSize, &fullError);
    if (!cancel->load()) {
      cache.insert(identity, requestedSize, result.full_image);
    }
  }
  if (result.thumbnail_image.isNull() && result.full_image.isNull()) {
    result.error = {.kind = PreviewService::PreviewErrorKind::DecodeFailed,
                    .message = !thumbError.isEmpty() ? thumbError : fullError};
    return result;
  }
  if (cancel->load()) {
    return result;
  }
  result.exif = ExifReader::read(file, result.mime_type.toUtf8(), cancel);
  return result;
}

// Runs entirely on PreviewService's worker thread. Mirrors DirectoryModel's walkDirectory: a free
// function with no Qt object identity, cooperatively cancellable at each stage boundary.
PreviewResult runPreviewJob(const QString& path, quint64 generation, const std::shared_ptr<std::atomic_bool>& cancel,
                            QSize requestedSize, PreviewWorkerCache& cache,
                            const std::function<void(const PreviewResult&)>& publish,
                            const std::function<void()>& beforeFullDecode) {
  PreviewResult result;
  result.generation = generation;
  if (cancel->load()) {
    return result;
  }
  // POSIX open is required for nonblocking, close-on-exec descriptor verification.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) {
    result.error = {.kind = PreviewService::PreviewErrorKind::PermissionDenied,
                    .message = QString::fromLocal8Bit(std::strerror(errno))};
    return result;
  }
  QFile file(path);
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    result.error = {.kind = PreviewService::PreviewErrorKind::PermissionDenied, .message = file.errorString()};
    return result;
  }
  struct stat info{};
  if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
    result.error = {.kind = PreviewService::PreviewErrorKind::Unsupported,
                    .message = QObject::tr("No preview available")};
    return result;
  }
  const QString identity = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
                               .arg(info.st_dev)
                               .arg(info.st_ino)
                               .arg(info.st_size)
                               .arg(info.st_mtim.tv_sec)
                               .arg(info.st_mtim.tv_nsec)
                               .arg(info.st_ctim.tv_sec)
                               .arg(info.st_ctim.tv_nsec);
  const auto sniff = file.read(kSniffBytes);
  if (file.error() != QFileDevice::NoError) {
    result.error = {.kind = PreviewService::PreviewErrorKind::DecodeFailed, .message = file.errorString()};
    return result;
  }
  if (cancel->load()) {
    return result;
  }
  QMimeDatabase mimeDatabase;
  const auto mime = mimeDatabase.mimeTypeForFileNameAndData(path, sniff);
  result.mime_type = mime.name();
  if (mime.name().startsWith(QStringLiteral("image/"))) {
    return decodeImage(file, path, identity, result, cancel, requestedSize, cache, publish, beforeFullDecode);
  }
  if (!TextPreviewService::looksBinary(sniff)) {
    result.text = TextPreviewService::readHead(file, kTextHeadBytes);
    result.has_text = result.text.error.isEmpty();
    if (!result.has_text) {
      result.error = {.kind = PreviewService::PreviewErrorKind::DecodeFailed, .message = result.text.error};
    }
  }
  return result;
}
}  // namespace

PreviewService::PreviewService(QObject* parent)
    : QObject(parent), worker_(new QObject), cache_(std::make_shared<PreviewWorkerCache>()) {
  // Process-wide: bounds how much memory any single QImageReader::read() call may allocate,
  // regardless of which thread calls it (see thumbnail_service.cpp's matching header-size guard).
  QImageReader::setAllocationLimit(256);
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, this, &PreviewService::shutdownFinished);
  timeout_timer_.setSingleShot(true);
  connect(&timeout_timer_, &QTimer::timeout, this, [this] {
    if (!busy_) {
      return;
    }
    busy_ = false;
    timed_out_ = true;
    if (cancellation_) {
      cancellation_->store(true);
    }
    pending_job_ = false;
    resetDisplayState();
    error_ = {.kind = PreviewErrorKind::DecodeTimeout, .message = tr("Cannot decode image")};
    emit changed();
  });
  resize_debounce_timer_.setSingleShot(true);
  connect(&resize_debounce_timer_, &QTimer::timeout, this, [this] {
    if (has_entry_ && !is_dir_ && !timed_out_) {
      const auto needed = source_pixel_size_.isValid() ? source_pixel_size_.scaled(requested_size_, Qt::KeepAspectRatio)
                                                       : requested_size_;
      const auto available = busy_ ? dispatched_size_ : full_image_.size();
      if (needed.width() > available.width() || needed.height() > available.height()) {
        dispatch();
      }
    }
  });
  thread_.start();
}

PreviewService::~PreviewService() {
  if (cancellation_) {
    cancellation_->store(true);
  }
  thread_.quit();
  thread_.wait();
}

void PreviewService::setTarget(const QString& path, bool isDir, qint64 size, const QDateTime& modified, quint32 mode,
                               bool statFailed, const QString& statError, quint64 revision) {
  if (path.isEmpty()) {
    clear();
    return;
  }
  if (has_entry_ && path_ == path && size_ == size && modified_ == modified && mode_ == mode && is_dir_ == isDir &&
      stat_failed_ == statFailed && stat_error_ == statError && revision_ == revision) {
    return;
  }
  mode_ = mode;
  stat_failed_ = statFailed;
  stat_error_ = statError;
  revision_ = revision;
  cancelInFlight();
  busy_ = false;
  path_ = path;
  name_ = QFileInfo(path).fileName();
  size_ = size;
  modified_ = modified;
  permissions_ = formatPermissions(mode);
  is_dir_ = isDir;
  has_entry_ = true;
  resetDisplayState();
  mime_type_.clear();
  if (statFailed) {
    // REQ-F-024: stat-level failures (broken symlinks, EACCES on the containing directory) are
    // already fully described by Stage 1's StatFailedRole/StatErrorRole; no new I/O needed.
    mime_type_.clear();
    error_ = {.kind = statError.contains(QStringLiteral("Broken symbolic link")) ? PreviewErrorKind::BrokenSymlink
                                                                                 : PreviewErrorKind::PermissionDenied,
              .message = statError};
    emit changed();
    return;
  }
  if (!S_ISREG(mode) && !S_ISLNK(mode) && !isDir) {
    mime_type_ = S_ISFIFO(mode) ? QStringLiteral("inode/fifo") : QStringLiteral("application/octet-stream");
    emit changed();
    return;
  }
  if (isDir) {
    mime_type_ = QStringLiteral("inode/directory");
    emit changed();
    return;
  }
  emit changed();  // Metadata is visible instantly; image/text/EXIF follow asynchronously.
  dispatch();
}

void PreviewService::clear() {
  cancelInFlight();
  has_entry_ = false;
  path_.clear();
  name_.clear();
  size_ = -1;
  modified_ = QDateTime();
  permissions_.clear();
  mime_type_.clear();
  is_dir_ = false;
  busy_ = false;
  resetDisplayState();
  emit changed();
}

void PreviewService::setRequestedSize(PreviewConsumer consumer, QSize pixels) {
  if (!pixels.isValid() || pixels.isEmpty()) {
    return;
  }
  auto& stored = consumer == PreviewConsumer::Pane ? pane_size_ : quick_look_size_;
  stored = pixels;
  updateRequestedSize();
}

void PreviewService::setQuickLookActive(bool active) {
  quick_look_active_ = active;
  updateRequestedSize();
}

void PreviewService::updateRequestedSize() {
  const auto size = quick_look_active_ ? quick_look_size_ : pane_size_;
  if (!size.isValid() || size.isEmpty() || size == requested_size_) {
    return;
  }
  requested_size_ = size;
  if (has_entry_ && mime_type_.startsWith(QStringLiteral("image/")) && !timed_out_) {
    resize_debounce_timer_.start(kResizeDebounceMs);
  }
}

void PreviewService::shutdown() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  cancelInFlight();
  if (cancellation_) {
    cancellation_->store(true);
  }
  thread_.quit();
}

void PreviewService::dispatch() {
  if (stopping_) {
    return;
  }
  cancelInFlight();
  cancellation_ = std::make_shared<std::atomic_bool>(false);
  busy_ = true;
  timeout_timer_.start(kDecodeTimeoutMs);
  pending_job_ = true;
  emit changed();
  if (!active_job_) {
    startJob();
  }
}

void PreviewService::startJob() {
  if (!pending_job_ || stopping_) {
    return;
  }
  pending_job_ = false;
  active_job_ = true;
  const auto generation = generation_;
  const auto cancel = cancellation_;
  const auto path = path_;
  const auto size = requested_size_.isValid() && !requested_size_.isEmpty() ? requested_size_ : QSize(1024, 1024);
  dispatched_size_ = source_pixel_size_.isValid() ? source_pixel_size_.scaled(size, Qt::KeepAspectRatio) : size;
  const auto beforeDispatch = before_dispatch_for_test_;
  const auto beforeFull = before_full_decode_for_test_;
  QMetaObject::invokeMethod(
      worker_,
      [this, path, generation, cancel, size, beforeDispatch, beforeFull] {
        const auto publish = [this](const PreviewResult& result) {
          QMetaObject::invokeMethod(this, [this, result] { applyResult(result); }, Qt::QueuedConnection);
        };
        if (!cancel->load() && beforeDispatch) {
          beforeDispatch();
        }
        auto result = runPreviewJob(path, generation, cancel, size, *cache_, publish, beforeFull);
        if (!cancel->load()) {
          publish(result);
        }
        QMetaObject::invokeMethod(
            this,
            [this] {
              active_job_ = false;
              startJob();
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void PreviewService::applyResult(const PreviewResult& result) {
  if (result.generation != generation_) {
    return;  // Stale: the target (or its requested size) has moved on.
  }
  if (timed_out_) {
    // A slow-but-eventually-successful decode must not silently replace an already-shown
    // "Cannot decode image" notice — only a new setTarget()/dispatch() call clears timed_out_.
    return;
  }
  if (result.final) {
    timeout_timer_.stop();
  }
  busy_ = !result.final;
  mime_type_ = result.mime_type;
  thumbnail_image_ = result.thumbnail_image;
  full_image_ = result.full_image;
  display_image_ = !full_image_.isNull() ? full_image_ : thumbnail_image_;
  source_pixel_size_ = result.source_pixel_size;
  exif_ = result.exif;
  has_text_ = result.has_text;
  text_ = result.text;
  error_ = result.error;
  emit changed();
}

void PreviewService::cancelInFlight() {
  ++generation_;
  pending_job_ = false;
  if (cancellation_) {
    cancellation_->store(true);
  }
  cancellation_.reset();
  timeout_timer_.stop();
  resize_debounce_timer_.stop();
  timed_out_ = false;
}

void PreviewService::resetDisplayState() {
  thumbnail_image_ = QImage();
  full_image_ = QImage();
  display_image_ = QImage();
  source_pixel_size_ = QSize();
  exif_ = ExifReader::ExifSummary();
  has_text_ = false;
  text_ = TextPreviewService::TextPreviewResult();
  error_ = PreviewError();
}
