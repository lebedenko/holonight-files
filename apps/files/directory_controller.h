#pragma once

#include "directory_model.h"
#include "directory_proxy_model.h"
#include "places_model.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The facade QML talks to: owns the DirectoryModel/DirectoryProxyModel/PlacesModel/watcher,
// NORMAL-mode key-chord parsing, and navigation/open dispatch. A full VimModeController for
// VISUAL/COMMAND/SEARCH/INSERT is explicitly out of scope for this stage (SPEC.md non-goals) —
// building that state machine for a single mode would be speculative generality.
class DirectoryController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
  Q_PROPERTY(QString currentPath READ currentPath NOTIFY changed)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
  Q_PROPERTY(QString directoryError READ directoryError NOTIFY changed)
  Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
  Q_PROPERTY(int cursorRow READ cursorRow NOTIFY changed)
  Q_PROPERTY(DirectoryProxyModel* listing READ listing CONSTANT)
  Q_PROPERTY(PlacesModel* places READ places CONSTANT)
 public:
  explicit DirectoryController(QObject* parent = nullptr);
  QString currentPath() const { return current_path_; }
  QString statusMessage() const { return status_message_; }
  QString directoryError() const { return model_.directoryError(); }
  bool scanning() const { return model_.scanning(); }
  int cursorRow() const { return cursor_row_; }
  DirectoryProxyModel* listing() { return &proxy_; }
  PlacesModel* places() { return &places_; }
  Q_INVOKABLE void open(const QString& path, const QString& fallbackReason = {});
  Q_INVOKABLE void navigateInto(int proxyRow);
  Q_INVOKABLE void navigateParent();
  Q_INVOKABLE void openEntry(int proxyRow);
  Q_INVOKABLE void toggleHidden();
  Q_INVOKABLE void toggleSortDirection();
  // Returns true when key was recognized and consumed. key is either the raw event.text (e.g.
  // "j", "5", "G") or a synthetic name QML supplies for non-printable keys ("Return").
  Q_INVOKABLE bool handleKey(const QString& key);
  Q_INVOKABLE void shutdown();

 signals:
  void changed();
  void shutdownFinished();

 private:
  int takeCount();
  void setCursorRow(qint64 row);
  void clampCursorRow();
  DirectoryModel model_;
  DirectoryProxyModel proxy_;
  PlacesModel places_;
  QFileSystemWatcher watcher_;
  QString current_path_;
  QString status_message_;
  int cursor_row_ = 0;
  int pending_count_ = 0;
  bool has_pending_count_ = false;
  bool pending_g_ = false;
  QElapsedTimer pending_g_timer_;
};
