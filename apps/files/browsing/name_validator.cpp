#include "name_validator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {
constexpr int kMaxNameBytes = 255;

bool isControlOrDelete(QChar character) { return character.unicode() < 0x20 || character.unicode() == 0x7F; }
}  // namespace

NameValidationResult validateName(const QString& rawInput, const QString& directoryPath, const QString& selfName) {
  NameValidationResult result;

  // 1. Autocorrect: a leading "./" is a legal no-op prefix (REQ-F-030).
  QString name = rawInput;
  if (name.startsWith(u"./")) {
    name.remove(0, 2);
  }

  // 2. No parent traversal, anywhere in what remains (REQ-F-031).
  if (name.contains(u"../")) {
    result.errorMessage =
        QCoreApplication::translate("NameValidator", "Can only operate within the current folder — no parent access");
    return result;
  }

  // 3. Exactly one trailing "/" marks a directory; any other "/" (embedded, doubled, more than
  //    one trailing) is rejected (REQ-F-032).
  bool createsDirectory = false;
  if (name.endsWith(u'/')) {
    const auto withoutTrailing = name.left(name.size() - 1);
    if (withoutTrailing.contains(u'/')) {
      result.errorMessage = QCoreApplication::translate(
          "NameValidator", "Cannot create nested entries — only items directly in this folder");
      return result;
    }
    createsDirectory = true;
    name = withoutTrailing;
  } else if (name.contains(u'/')) {
    result.errorMessage = QCoreApplication::translate(
        "NameValidator", "Cannot create nested entries — only items directly in this folder");
    return result;
  }

  // 4. Empty or whitespace-only (REQ-F-033).
  if (name.trimmed().isEmpty()) {
    result.errorMessage = QCoreApplication::translate("NameValidator", "Name cannot be empty");
    return result;
  }

  // 5. Literal "." or ".." (REQ-F-034).
  if (name == u"." || name == u"..") {
    result.errorMessage = QCoreApplication::translate("NameValidator", "That name is reserved");
    return result;
  }

  // 6. Filesystem max name length, in bytes (REQ-F-035; see DESIGN.md Known Risks re: Unicode).
  if (name.toUtf8().size() > kMaxNameBytes) {
    result.errorMessage = QCoreApplication::translate("NameValidator", "Name is too long");
    return result;
  }

  // 7. Control bytes / DEL (REQ-F-036).
  for (const auto character : name) {
    if (isControlOrDelete(character)) {
      result.errorMessage = QCoreApplication::translate("NameValidator", "Invalid file name");
      return result;
    }
  }

  // 8. Collision with an existing entry, excluding the unchanged-name case the caller identifies
  //    via selfName (REQ-F-037/REQ-F-038).
  const QFileInfo destination(QDir(directoryPath).filePath(name));
  if (name != selfName && (destination.exists() || destination.isSymLink())) {
    result.errorMessage = QCoreApplication::translate("NameValidator", "An entry with that name already exists");
    return result;
  }

  result.valid = true;
  result.normalizedName = name;
  result.createsDirectory = createsDirectory;
  return result;
}
