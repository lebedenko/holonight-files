#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class SizeFormat : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
 public:
  explicit SizeFormat(QObject* parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE QString formatSize(qint64 bytes) const;
};
