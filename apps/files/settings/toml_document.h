#pragma once

#include <QByteArray>
#include <QString>
#include <QtTypes>

#include <memory>
#include <vector>

// A diagnostic produced while reading a TOML file. Messages carry no file path; the caller that
// knows which file was read prefixes it when forwarding to a WarningSink.
struct TomlDiagnostic {
  enum class Kind { ParseError, WrongType, UnknownEntry };
  Kind kind = Kind::ParseError;
  QString message;
  int line = 0;  // 1-based; 0 when unknown
};

struct TomlValue {
  // Other = any TOML type the settings layer does not declare (array, table, float, date/time).
  enum class Type { Missing, Bool, String, Integer, Other };
  Type type = Type::Missing;
  bool bool_value = false;
  QString string_value;
  qint64 int_value = 0;
  int line = 0;
};

// The one adapter over the TOML library (SPEC.md REQ-F-028): none of its types cross this header. An empty
// section name addresses the document's root table.
class TomlDocument {
 public:
  struct ParseResult;
  // A missing file is not an error (REQ-F-002). An existing file that cannot be read or parsed
  // yields exactly one ParseError diagnostic and an empty document (REQ-F-003/006).
  static ParseResult parseFile(const QString& path);
  static ParseResult parse(const QByteArray& content);

  TomlValue value(const QString& section, const QString& key) const;
  // Top-level tables, in document order.
  std::vector<QString> sections() const;
  // Keys of a top-level table (any value type), in document order; empty if section is absent.
  std::vector<QString> keys(const QString& section) const;
  // Top-level keys whose values are not tables, in document order.
  std::vector<QString> rootKeys() const;
  int sectionLine(const QString& section) const;

  // Size of the top-level [[key]] array of tables; 0 if key is absent or not an array of tables.
  int arrayOfTablesSize(const QString& key) const;
  // field's value inside the array-of-tables key's index'th entry (0-based); Missing if absent.
  TomlValue arrayOfTablesValue(const QString& key, int index, const QString& field) const;
  // That entry's own keys, in document order (for unknown-key detection).
  std::vector<QString> arrayOfTablesKeys(const QString& key, int index) const;
  // 1-based line where that entry's table begins; 0 if absent.
  int arrayOfTablesLine(const QString& key, int index) const;

  // A TOML basic string literal, quoted and escaped, for writers that emit TOML text themselves.
  static QString quoteString(const QString& text);

 private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

struct TomlDocument::ParseResult {
  TomlDocument document;
  bool file_exists = false;
  std::vector<TomlDiagnostic> diagnostics;
};
