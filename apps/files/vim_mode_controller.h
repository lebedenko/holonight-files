#pragma once

#include "name_validator.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// The NORMAL/VISUAL/SEARCH/INSERT mode state machine (docs/sdd/vim-modal-editing/SPEC.md). Owned
// as a value member of DirectoryController, structurally analogous to PreviewService, but kept
// free of any pointer into DirectoryModel/DirectoryProxyModel: every method that needs directory
// context (paths, an existing entry's name, the currently visible name list) takes it as a plain
// value parameter, so this class stays constructible and testable in isolation. It never touches
// the filesystem itself — commitInsert() returns a description of the action to perform;
// DirectoryController executes it (REQ-F-041: synchronous, direct calls, no TaskManager).
class VimModeController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")

  Q_PROPERTY(Mode currentMode READ currentMode NOTIFY changed)

  Q_PROPERTY(int visualAnchorRow READ visualAnchorRow NOTIFY changed)
  Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)

  Q_PROPERTY(int editingRow READ editingRow NOTIFY changed)
  Q_PROPERTY(bool editingIsCreate READ editingIsCreate NOTIFY changed)
  Q_PROPERTY(QString insertText READ insertText NOTIFY changed)
  Q_PROPERTY(int insertCursorPosition READ insertCursorPosition NOTIFY changed)
  Q_PROPERTY(bool insertValid READ insertValid NOTIFY changed)
  Q_PROPERTY(QString insertErrorMessage READ insertErrorMessage NOTIFY changed)
  Q_PROPERTY(bool insertCreatesDirectory READ insertCreatesDirectory NOTIFY changed)

  Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY changed)
  Q_PROPERTY(QList<int> searchMatchPositions READ searchMatchPositions NOTIFY changed)
  Q_PROPERTY(int searchBestRow READ searchBestRow NOTIFY changed)
  Q_PROPERTY(QList<int> searchMatchRows READ searchMatchRows NOTIFY changed)

 public:
  enum class Mode { Normal, Visual, Search, Insert };
  Q_ENUM(Mode)
  enum class InsertKind { Prepend, Append, CreateBelow, CreateAbove };
  Q_ENUM(InsertKind)
  enum class InsertCommitAction { None, Touch, Rename, CreateDirectory, CreateFile };
  Q_ENUM(InsertCommitAction)

  struct InsertCommitResult {
    InsertCommitAction action = InsertCommitAction::None;
    QString oldPath;
    QString newPath;
  };

  explicit VimModeController(QObject* parent = nullptr);

  Mode currentMode() const { return mode_; }

  // VISUAL — REQ-F-018 through REQ-F-023
  int visualAnchorRow() const { return visual_anchor_row_; }
  int selectedCount() const;
  Q_INVOKABLE bool isRowSelected(int row) const;
  Q_INVOKABLE void enterVisual(int currentRow);
  Q_INVOKABLE void extendVisual(int newRow);
  Q_INVOKABLE void exitVisual();

  // INSERT — REQ-F-006 through REQ-F-017, REQ-F-030 through REQ-F-042
  int editingRow() const { return editing_row_; }
  bool editingIsCreate() const;
  QString insertText() const { return insert_text_; }
  int insertCursorPosition() const { return insert_cursor_position_; }
  bool insertValid() const { return insert_validation_.valid; }
  QString insertErrorMessage() const { return insert_validation_.errorMessage; }
  bool insertCreatesDirectory() const { return insert_validation_.createsDirectory; }
  Q_INVOKABLE void enterInsert(InsertKind kind, int row, const QString& directoryPath, const QString& existingName);
  Q_INVOKABLE void setInsertText(const QString& text);
  // Returns the filesystem action DirectoryController must now perform, or
  // InsertCommitAction::None when validation still fails (mode stays Insert; REQ-F-037/038).
  Q_INVOKABLE VimModeController::InsertCommitResult commitInsert();
  Q_INVOKABLE void cancelInsert();
  // REQ-F-039/040: called by DirectoryController after attempting the filesystem action —
  // success returns to NORMAL; failure keeps INSERT active with a permission-specific message.
  Q_INVOKABLE void reportCommitFailed(const QString& message);
  Q_INVOKABLE void reportCommitSucceeded();

  // SEARCH — REQ-F-024 through REQ-F-029
  QList<int> searchMatchPositions() const { return search_match_positions_; }
  void updateSearchPositions(const QString& name);
  void refreshSearch(const QStringList& visibleNames);
  void resetSearch();
  QString searchQuery() const { return search_query_; }
  int searchBestRow() const { return search_best_row_; }
  QList<int> searchMatchRows() const { return search_match_rows_; }
  Q_INVOKABLE void enterSearch(int currentRow);
  Q_INVOKABLE void setSearchQuery(const QString& query, const QStringList& visibleNames);
  Q_INVOKABLE int advanceSearchMatch(int currentRow, bool forward) const;
  Q_INVOKABLE void commitSearch();
  // Returns the row the cursor should be restored to.
  Q_INVOKABLE int cancelSearch();

 signals:
  void changed();

 private:
  void resetInsertState();

  Mode mode_ = Mode::Normal;

  int visual_anchor_row_ = -1;
  int visual_current_row_ = -1;

  InsertKind insert_kind_ = InsertKind::Prepend;
  int editing_row_ = -1;
  QString insert_text_;
  int insert_cursor_position_ = 0;
  QString insert_directory_path_;
  QString insert_self_name_;
  NameValidationResult insert_validation_;

  QString search_query_;
  QString committed_query_;
  QList<int> search_match_positions_;
  int search_pre_row_ = -1;
  int search_best_row_ = -1;
  QList<int> search_match_rows_;
};

Q_DECLARE_METATYPE(VimModeController::InsertCommitResult)
