#pragma once

#include "directory_controller.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class InspectionKeys : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
 public:
  explicit InspectionKeys(QObject* parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE bool press(int key, const QString& text, int modifiers, bool autoRepeat, DirectoryController* controller,
                         bool popup) const;
  Q_INVOKABLE bool release(int key) const;
  Q_INVOKABLE bool overrideShortcut(int key, bool popup, bool blockEscape) const;
};
