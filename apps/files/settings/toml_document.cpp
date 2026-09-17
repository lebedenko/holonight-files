#include "settings/toml_document.h"

#include <QFile>

#include <algorithm>
#include <string_view>
#include <toml++/toml.h>
#include <utility>

struct TomlDocument::Impl {
  toml::table root;
};

namespace {
std::string_view utf8View(const QByteArray& bytes) {
  return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString fromUtf8(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

// toml::table is ordered by key, not by position; sort by source position so warnings follow the
// file top to bottom.
std::vector<QString> keysInSourceOrder(const toml::table& table, bool (*accept)(const toml::node&)) {
  std::vector<std::pair<toml::source_position, QString>> found;
  for (const auto& [key, node] : table) {
    if (accept(node)) {
      found.emplace_back(node.source().begin, fromUtf8(key.str()));
    }
  }
  std::ranges::sort(found, [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  std::vector<QString> keys;
  keys.reserve(found.size());
  for (auto& entry : found) {
    keys.push_back(std::move(entry.second));
  }
  return keys;
}

TomlValue toValue(const toml::node& node) {
  TomlValue value;
  value.line = static_cast<int>(node.source().begin.line);
  if (const auto* boolean = node.as_boolean()) {
    value.type = TomlValue::Type::Bool;
    value.bool_value = boolean->get();
  } else if (const auto* string = node.as_string()) {
    value.type = TomlValue::Type::String;
    value.string_value = fromUtf8(string->get());
  } else if (const auto* integer = node.as_integer()) {
    value.type = TomlValue::Type::Integer;
    value.int_value = integer->get();
  } else {
    value.type = TomlValue::Type::Other;
  }
  return value;
}
}  // namespace

TomlDocument::ParseResult TomlDocument::parse(const QByteArray& content) {
  ParseResult result;
  result.file_exists = true;
  try {
    auto impl = std::make_shared<Impl>();
    impl->root = toml::parse(utf8View(content));
    result.document.impl_ = std::move(impl);
  } catch (const toml::parse_error& error) {
    // The parser stops at the first error, so a file with many errors still yields one diagnostic.
    result.diagnostics.push_back({.kind = TomlDiagnostic::Kind::ParseError,
                                  .message = fromUtf8(error.description()),
                                  .line = static_cast<int>(error.source().begin.line)});
  }
  return result;
}

TomlDocument::ParseResult TomlDocument::parseFile(const QString& path) {
  QFile file(path);
  if (!file.exists()) {
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    ParseResult result;
    result.file_exists = true;
    result.diagnostics.push_back({.kind = TomlDiagnostic::Kind::ParseError, .message = file.errorString()});
    return result;
  }
  return parse(file.readAll());
}

namespace {
// nullptr if key is absent from root or is not an array of tables.
const toml::array* arrayOfTables(const toml::table& root, const QString& key) {
  const auto* array = root[utf8View(key.toUtf8())].as_array();
  return (array != nullptr && array->is_array_of_tables()) ? array : nullptr;
}
}  // namespace

TomlValue TomlDocument::value(const QString& section, const QString& key) const {
  if (!impl_) {
    return {};
  }
  const auto* table = &impl_->root;
  if (!section.isEmpty()) {
    table = impl_->root[utf8View(section.toUtf8())].as_table();
    if (table == nullptr) {
      return {};
    }
  }
  const auto* node = table->get(utf8View(key.toUtf8()));
  return node == nullptr ? TomlValue{} : toValue(*node);
}

std::vector<QString> TomlDocument::sections() const {
  if (!impl_) {
    return {};
  }
  return keysInSourceOrder(impl_->root, [](const toml::node& node) { return node.is_table(); });
}

std::vector<QString> TomlDocument::keys(const QString& section) const {
  if (!impl_) {
    return {};
  }
  const auto* table = impl_->root[utf8View(section.toUtf8())].as_table();
  if (table == nullptr) {
    return {};
  }
  return keysInSourceOrder(*table, [](const toml::node&) { return true; });
}

std::vector<QString> TomlDocument::rootKeys() const {
  if (!impl_) {
    return {};
  }
  return keysInSourceOrder(impl_->root, [](const toml::node& node) { return !node.is_table(); });
}

int TomlDocument::sectionLine(const QString& section) const {
  if (!impl_) {
    return 0;
  }
  const auto* node = impl_->root.get(utf8View(section.toUtf8()));
  return node == nullptr ? 0 : static_cast<int>(node->source().begin.line);
}

int TomlDocument::arrayOfTablesSize(const QString& key) const {
  if (!impl_) {
    return 0;
  }
  const auto* array = arrayOfTables(impl_->root, key);
  return array == nullptr ? 0 : static_cast<int>(array->size());
}

TomlValue TomlDocument::arrayOfTablesValue(const QString& key, int index, const QString& field) const {
  if (!impl_ || index < 0) {
    return {};
  }
  const auto* array = arrayOfTables(impl_->root, key);
  if (array == nullptr || static_cast<std::size_t>(index) >= array->size()) {
    return {};
  }
  const auto* entry = array->get(static_cast<std::size_t>(index))->as_table();
  if (entry == nullptr) {
    return {};
  }
  const auto* node = entry->get(utf8View(field.toUtf8()));
  return node == nullptr ? TomlValue{} : toValue(*node);
}

std::vector<QString> TomlDocument::arrayOfTablesKeys(const QString& key, int index) const {
  if (!impl_ || index < 0) {
    return {};
  }
  const auto* array = arrayOfTables(impl_->root, key);
  if (array == nullptr || static_cast<std::size_t>(index) >= array->size()) {
    return {};
  }
  const auto* entry = array->get(static_cast<std::size_t>(index))->as_table();
  if (entry == nullptr) {
    return {};
  }
  return keysInSourceOrder(*entry, [](const toml::node&) { return true; });
}

int TomlDocument::arrayOfTablesLine(const QString& key, int index) const {
  if (!impl_ || index < 0) {
    return 0;
  }
  const auto* array = arrayOfTables(impl_->root, key);
  if (array == nullptr || static_cast<std::size_t>(index) >= array->size()) {
    return 0;
  }
  return static_cast<int>(array->get(static_cast<std::size_t>(index))->source().begin.line);
}

QString TomlDocument::quoteString(const QString& text) {
  QString quoted;
  quoted.reserve(text.size() + 2);
  quoted += u'"';
  for (const QChar character : text) {
    const auto code = character.unicode();
    switch (code) {
      case u'"':
        quoted += QStringLiteral("\\\"");
        break;
      case u'\\':
        quoted += QStringLiteral("\\\\");
        break;
      case u'\n':
        quoted += QStringLiteral("\\n");
        break;
      case u'\t':
        quoted += QStringLiteral("\\t");
        break;
      case u'\r':
        quoted += QStringLiteral("\\r");
        break;
      default:
        if (code < 0x20 || code == 0x7f) {
          quoted += QStringLiteral("\\u%1").arg(static_cast<uint>(code), 4, 16, QLatin1Char('0'));
        } else {
          quoted += character;
        }
    }
  }
  quoted += u'"';
  return quoted;
}
