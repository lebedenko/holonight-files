#include "places_model.h"

#include "places/bookmark_store.h"
#include "places/user_dirs_parser.h"
#include "settings/xdg_paths.h"

#include <QDir>
#include <QMetaObject>
#include <QSet>

#include <thread>
#include <utility>

PlacesModel::PlacesModel(QObject* parent)
    : PlacesModel(QDir::homePath(), XdgPaths::userDirsFilePath(), XdgPaths::placesFilePath(),
                  std::make_shared<StatPlaceAvailabilityChecker>(), std::make_shared<StderrWarningSink>(), parent) {}

PlacesModel::PlacesModel(const QString& homePath, const QString& userDirsFilePath, const QString& placesFilePath,
                         std::shared_ptr<PlaceAvailabilityChecker> checker,
                         const std::shared_ptr<WarningSink>& warnings, QObject* parent)
    : QAbstractListModel(parent), guard_(std::make_shared<DeliveryGuard>()), checker_(std::move(checker)) {
  guard_->model = this;
  buildPlaces(homePath, userDirsFilePath, placesFilePath, *warnings);
  dispatchStartupChecks();
}

PlacesModel::~PlacesModel() {
  const std::scoped_lock lock(guard_->mutex);
  guard_->model = nullptr;
}

void PlacesModel::buildPlaces(const QString& homePath, const QString& userDirsFilePath, const QString& placesFilePath,
                              WarningSink& warnings) {
  QSet<QString> seen;
  const auto append = [&](const QString& name, const QString& path, const QString& icon, Origin origin,
                          bool startsBookmarks) {
    places_.append({.id = next_id_++,
                    .name = name,
                    .path = path,
                    .iconName = icon,
                    .origin = origin,
                    .status = Status::Checking,
                    .starts_bookmarks = startsBookmarks});
    seen.insert(path);
  };

  const auto cleanedHome = QDir::cleanPath(homePath);
  append(tr("Home"), cleanedHome, QStringLiteral("user-home"), Origin::Home, /*startsBookmarks=*/false);

  for (const auto& entry : UserDirsParser::parseFile(userDirsFilePath, cleanedHome)) {
    if (seen.contains(entry.path)) {
      continue;  // REQ-F-016: silent (two XDG_*_DIR keys aliasing the same path)
    }
    append(UserDirsParser::label(entry.key), entry.path, UserDirsParser::iconName(entry.key), Origin::XdgUserDirectory,
           /*startsBookmarks=*/false);
  }

  bool bookmarksStarted = false;
  for (const auto& bookmark : BookmarkStore::read(placesFilePath, warnings)) {
    if (seen.contains(bookmark.path)) {
      auto describer = tr("an earlier bookmark");
      for (const auto& place : places_) {
        if (place.path == bookmark.path) {
          if (place.origin == Origin::Home) {
            describer = tr("Home");
          } else if (place.origin == Origin::XdgUserDirectory) {
            describer = place.name;
          }
          break;
        }
      }
      warnings.warn(QStringLiteral("%1: bookmark \"%2\" duplicates %3").arg(placesFilePath, bookmark.path, describer));
      continue;  // REQ-F-017
    }
    append(bookmark.name, bookmark.path, QStringLiteral("user-bookmarks"), Origin::Bookmark, !bookmarksStarted);
    bookmarksStarted = true;
  }
}

void PlacesModel::dispatchStartupChecks() {
  for (const auto& place : places_) {
    dispatchCheck(place.id, /*generation=*/0, place.path);
  }
}

void PlacesModel::dispatchCheck(quint64 placeId, quint64 generation, const QString& path) {
  std::thread([guard = guard_, checker = checker_, placeId, generation, path] {
    const bool available = checker->isAvailable(path);  // may block indefinitely; no lock held
    const std::scoped_lock lock(guard->mutex);
    if (guard->model == nullptr) {
      return;  // model destroyed while checking
    }
    QMetaObject::invokeMethod(
        guard->model,
        [model = guard->model, placeId, generation, available] {
          model->deliverResult(placeId, generation, available);
        },
        Qt::QueuedConnection);
  }).detach();
}

int PlacesModel::rowForId(quint64 placeId) const {
  for (int i = 0; i < places_.size(); ++i) {
    if (places_[i].id == placeId) {
      return i;
    }
  }
  return -1;
}

void PlacesModel::deliverResult(quint64 placeId, quint64 generation, bool available) {
  const auto row = rowForId(placeId);
  if (row < 0) {
    return;  // row already removed; delivery is a no-op
  }
  auto& place = places_[row];
  if (place.recheck_generation != generation) {
    return;  // Includes a startup check superseded by bookmark activation.
  }
  if (generation == 0) {
    // Startup check.
    if (place.origin == Origin::Home) {
      // REQ-C-005: Home is always Available once checked, never removed or Unavailable.
      place.status = Status::Available;
      const auto idx = index(row);
      emit dataChanged(idx, idx, {StatusRole});
    } else if (place.origin == Origin::XdgUserDirectory) {
      if (available) {
        place.status = Status::Available;
        const auto idx = index(row);
        emit dataChanged(idx, idx, {StatusRole});
      } else {
        beginRemoveRows({}, row, row);  // REQ-F-018/020: never Unavailable for XDG, only removed
        places_.removeAt(row);
        endRemoveRows();
      }
    } else {
      place.status = available ? Status::Available : Status::Unavailable;
      const auto idx = index(row);
      emit dataChanged(idx, idx, {StatusRole});
    }
    return;
  }
  // Recheck (REQ-F-022): only ever dispatched for Bookmark rows, by recheckBookmark().
  place.status = available ? Status::Available : Status::Unavailable;
  const auto idx = index(row);
  emit dataChanged(idx, idx, {StatusRole});
  emit bookmarkRecheckResolved(placeId, place.path, available);
}

quint64 PlacesModel::recheckBookmark(int row) {
  if (row < 0 || row >= places_.size()) {
    return 0;
  }
  auto& place = places_[row];
  if (place.origin != Origin::Bookmark) {
    return 0;
  }
  const auto generation = ++place.recheck_generation;
  dispatchCheck(place.id, generation, place.path);
  return place.id;
}

int PlacesModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(places_.size());
}

QVariant PlacesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.model() != this || index.row() >= places_.size()) {
    return {};
  }
  const auto& place = places_[index.row()];
  switch (role) {
    case NameRole:
      return place.name;
    case PathRole:
      return place.path;
    case IconNameRole:
      return place.iconName;
    case OriginRole:
      return QVariant::fromValue(place.origin);
    case StatusRole:
      return QVariant::fromValue(place.status);
    case StartsBookmarksRole:
      return place.starts_bookmarks;
    default:
      return {};
  }
}

QHash<int, QByteArray> PlacesModel::roleNames() const {
  return {{NameRole, "name"},     {PathRole, "path"},     {IconNameRole, "iconName"},
          {OriginRole, "origin"}, {StatusRole, "status"}, {StartsBookmarksRole, "startsBookmarks"}};
}
