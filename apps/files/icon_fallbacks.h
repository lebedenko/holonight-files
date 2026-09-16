#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QtQml/qqmlregistration.h>

class IconFallbacks : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
 public:
  explicit IconFallbacks(QObject* parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE bool isUnresolved(const QString& chain) const;
  Q_INVOKABLE void markUnresolved(const QString& chain);

 private:
  QSet<QString> unresolved_;
};
