#include "text_line_model.h"

TextLineModel::TextLineModel(QObject* parent) : QAbstractListModel(parent) {}

int TextLineModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(lines_.size());
}

QVariant TextLineModel::data(const QModelIndex& index, int role) const {
  if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
    return {};
  }
  switch (role) {
    case LineTextRole:
      return lines_.at(index.row());
    case LineNumberRole:
      return index.row() + 1;
    default:
      return {};
  }
}

QHash<int, QByteArray> TextLineModel::roleNames() const {
  return {{LineTextRole, "lineText"}, {LineNumberRole, "lineNumber"}};
}

void TextLineModel::setLines(QStringList lines) {
  beginResetModel();
  lines_ = std::move(lines);
  endResetModel();
}

void TextLineModel::clear() {
  if (lines_.isEmpty()) {
    return;
  }
  beginResetModel();
  lines_.clear();
  endResetModel();
}
