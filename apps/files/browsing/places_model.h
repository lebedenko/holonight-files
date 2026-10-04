#pragma once

#include "places/place_availability_checker.h"
#include "places/place_list.h"
#include "warning_sink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <mutex>

// Home + XDG user directories + user bookmarks, deduplicated, with async availability status
// (SPEC.md places-sources). Constructed once by DirectoryController, exactly like today.
class PlacesModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum class Origin : std::uint8_t { Home, XdgUserDirectory, Bookmark };
  Q_ENUM(Origin)
  enum class Status : std::uint8_t { Checking, Available, Unavailable };
  Q_ENUM(Status)
  // Qt model roles/QML properties require implicit integer conversion.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    PathRole,
    IconNameRole,
    OriginRole,
    StatusRole,
    // True only on the first Bookmark row, when at least one non-bookmark row precedes it
    // (REQ-F-028); consumed by PlacesPanel.qml's delegate to add one extra spacing gap.
    StartsBookmarksRole,
  };

  explicit PlacesModel(QObject* parent = nullptr);
  // Test seam: overrides real XDG env resolution and the real stat()-based checker
  // (REQ-NF-001/002 tests inject a recording/blocking checker here).
  PlacesModel(const QString& homePath, const QString& userDirsFilePath, const QString& placesFilePath,
              std::shared_ptr<PlaceAvailabilityChecker> checker, const std::shared_ptr<WarningSink>& warnings,
              QObject* parent = nullptr);
  ~PlacesModel() override;
  PlacesModel(const PlacesModel&) = delete;
  PlacesModel& operator=(const PlacesModel&) = delete;
  PlacesModel(PlacesModel&&) = delete;
  PlacesModel& operator=(PlacesModel&&) = delete;

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  // Re-checks row's path off the GUI thread, regardless of its current status (REQ-F-022). Returns
  // the row's stable place id, or 0 if row is out of range or not a Bookmark row. Row indices are
  // NOT stable while startup checks are still removing XDG rows, so everything after dispatch is
  // keyed by id.
  quint64 recheckBookmark(int row);

  // Immutable path -> icon name map of Home + XDG places, built once from the same PlaceList the sidebar rows
  // come from (named-folder-icons REQ-F-001, REQ-F-014). Never null; not pruned when a missing XDG row is
  // later removed from the sidebar (a nonexistent directory is never listed, so the stale entry is inert).
  [[nodiscard]] std::shared_ptr<const PlaceList::IconMap> placeIcons() const { return place_icons_; }

 signals:
  // Fired once the freshest in-flight recheck for that place resolves; its StatusRole is already
  // updated (one dataChanged, REQ-NF-003) before this signal is emitted. A recheck superseded by a
  // newer recheckBookmark() call on the same place never reaches here.
  void bookmarkRecheckResolved(quint64 placeId, QString path, bool available);

 private:
  friend struct PlacesModelTestAccess;
  struct Place {
    quint64 id = 0;  // stable across the model's lifetime; rows are never reordered except removal
    QString name;
    QString path;
    QString icon_name;
    Origin origin = Origin::Home;
    Status status = Status::Checking;
    bool starts_bookmarks = false;
    quint64 recheck_generation = 0;  // guards stale recheckBookmark() deliveries (REQ-NF-003)
  };
  void buildPlaces(const QString& homePath, const QString& userDirsFilePath, const QString& placesFilePath,
                   WarningSink& warnings);
  void dispatchStartupChecks();
  void deliverResult(quint64 placeId, quint64 generation, bool available);
  [[nodiscard]] int rowForId(quint64 placeId) const;
  // Shared with every detached check thread; ~PlacesModel() locks it and nulls `model`.
  struct DeliveryGuard {
    std::mutex mutex;
    PlacesModel* model = nullptr;
  };
  void dispatchCheck(quint64 placeId, quint64 generation, const QString& path);
  QList<Place> places_;
  std::shared_ptr<const PlaceList::IconMap> place_icons_;
  std::shared_ptr<DeliveryGuard> guard_;
  std::shared_ptr<PlaceAvailabilityChecker> checker_;
  quint64 next_id_ = 1;
};
