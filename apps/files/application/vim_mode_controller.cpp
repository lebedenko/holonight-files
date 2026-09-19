#include "vim_mode_controller.h"

#include "fuzzy_matcher.h"

#include <QDir>

#include <limits>
#include <ranges>

VimModeController::VimModeController(QObject* parent) : QObject(parent) {}

int VimModeController::selectedCount() const {
  if (mode_ != Mode::Visual || visual_anchor_row_ < 0 || visual_current_row_ < 0) {
    return 0;
  }
  return qAbs(visual_current_row_ - visual_anchor_row_) + 1;
}

bool VimModeController::isRowSelected(int row) const {
  if (mode_ != Mode::Visual || visual_anchor_row_ < 0 || visual_current_row_ < 0) {
    return false;
  }
  const int lowRow = qMin(visual_anchor_row_, visual_current_row_);
  const int highRow = qMax(visual_anchor_row_, visual_current_row_);
  return row >= lowRow && row <= highRow;
}

void VimModeController::enterVisual(int currentRow) {
  mode_ = Mode::Visual;
  visual_anchor_row_ = currentRow;
  visual_current_row_ = currentRow;
  emit changed();
}

void VimModeController::extendVisual(int newRow) {
  if (mode_ != Mode::Visual) {
    return;
  }
  visual_current_row_ = newRow;
  emit changed();
}

void VimModeController::exitVisual() {
  mode_ = Mode::Normal;
  visual_anchor_row_ = -1;
  visual_current_row_ = -1;
  emit changed();
}

bool VimModeController::editingIsCreate() const {
  return insert_kind_ == InsertKind::CreateBelow || insert_kind_ == InsertKind::CreateAbove;
}

void VimModeController::enterInsert(InsertKind kind, int row, const QString& directoryPath,
                                    const QString& existingName) {
  mode_ = Mode::Insert;
  insert_kind_ = kind;
  editing_row_ = row;
  insert_directory_path_ = directoryPath;
  const bool create = editingIsCreate();
  insert_self_name_ = create ? QString() : existingName;
  insert_text_ = create ? QString() : existingName;
  insert_cursor_position_ = (kind == InsertKind::Prepend) ? 0 : static_cast<int>(insert_text_.size());
  insert_validation_ = validateName(insert_text_, insert_directory_path_, insert_self_name_);
  emit changed();
}

void VimModeController::setInsertText(const QString& text) {
  if (mode_ != Mode::Insert) {
    return;
  }
  insert_text_ = text;
  insert_validation_ = validateName(insert_text_, insert_directory_path_, insert_self_name_);
  emit changed();
}

VimModeController::InsertCommitResult VimModeController::commitInsert() {
  InsertCommitResult result;
  if (mode_ != Mode::Insert) {
    return result;
  }
  insert_validation_ = validateName(insert_text_, insert_directory_path_, insert_self_name_);
  if (!insert_validation_.valid) {
    emit changed();
    return result;  // REQ-F-037/038: stay in INSERT so the user can correct it
  }
  const QDir dir(insert_directory_path_);
  if (editingIsCreate()) {
    result.newPath = dir.filePath(insert_validation_.normalizedName);
    result.action =
        insert_validation_.createsDirectory ? InsertCommitAction::CreateDirectory : InsertCommitAction::CreateFile;
  } else if (insert_validation_.normalizedName == insert_self_name_) {
    result.oldPath = dir.filePath(insert_self_name_);
    result.action = InsertCommitAction::Touch;
  } else {
    result.oldPath = dir.filePath(insert_self_name_);
    result.newPath = dir.filePath(insert_validation_.normalizedName);
    result.action = InsertCommitAction::Rename;
  }
  return result;
}

void VimModeController::cancelInsert() {
  mode_ = Mode::Normal;
  resetInsertState();
  emit changed();
}

void VimModeController::reportCommitFailed(const QString& message) {
  insert_validation_.valid = false;
  insert_validation_.errorMessage = message;
  emit changed();
}

void VimModeController::reportCommitSucceeded() {
  mode_ = Mode::Normal;
  resetInsertState();
  emit changed();
}

void VimModeController::resetInsertState() {
  editing_row_ = -1;
  insert_text_.clear();
  insert_cursor_position_ = 0;
  insert_directory_path_.clear();
  insert_self_name_.clear();
  insert_validation_ = {};
}

void VimModeController::enterSearch(int currentRow) {
  mode_ = Mode::Search;
  search_pre_row_ = currentRow;
  search_query_.clear();
  search_best_row_ = -1;
  search_match_rows_.clear();
  emit changed();
}

void VimModeController::setSearchQuery(const QString& query, const QStringList& visibleNames) {
  if (mode_ != Mode::Search) {
    return;
  }
  search_query_ = query;
  refreshSearch(visibleNames);
}

void VimModeController::refreshSearch(const QStringList& visibleNames) {
  const auto query = mode_ == Mode::Search ? search_query_ : committed_query_;
  search_match_rows_.clear();
  search_best_row_ = -1;
  if (!query.isEmpty()) {
    int bestScore = std::numeric_limits<int>::min();
    for (int row = 0; row < visibleNames.size(); ++row) {
      const auto match = fuzzyMatch(query, visibleNames.at(row));
      if (!match.matched) {
        continue;
      }
      search_match_rows_.append(row);
      if (match.score > bestScore) {
        bestScore = match.score;
        search_best_row_ = row;
      }
    }
  }
  emit changed();
}

int VimModeController::advanceSearchMatch(int currentRow, bool forward) const {
  if (search_match_rows_.isEmpty()) {
    return -1;
  }
  if (forward) {
    for (const int row : search_match_rows_) {
      if (row > currentRow) {
        return row;
      }
    }
    return search_match_rows_.first();
  }
  for (const int row : std::ranges::reverse_view(search_match_rows_)) {
    if (row < currentRow) {
      return row;
    }
  }
  return search_match_rows_.last();
}

void VimModeController::commitSearch() {
  committed_query_ = search_query_;
  search_match_positions_.clear();
  mode_ = Mode::Normal;
  search_query_.clear();
  emit changed();
}

int VimModeController::cancelSearch() {
  search_match_positions_.clear();
  mode_ = Mode::Normal;
  const int restoreRow = search_pre_row_;
  search_query_.clear();
  search_match_rows_.clear();
  search_best_row_ = -1;
  search_pre_row_ = -1;
  emit changed();
  return restoreRow;
}

void VimModeController::updateSearchPositions(const QString& name) {
  const auto positions = mode_ == Mode::Search ? fuzzyMatch(search_query_, name).positions : QList<int>{};
  if (positions != search_match_positions_) {
    search_match_positions_ = positions;
    emit changed();
  }
}
void VimModeController::resetSearch() {
  search_query_.clear();
  committed_query_.clear();
  search_match_rows_.clear();
  search_match_positions_.clear();
  search_best_row_ = -1;
  search_pre_row_ = -1;
  emit changed();
}
