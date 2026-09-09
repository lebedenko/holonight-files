// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>

#pragma once

#include <QObject>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class WindowState : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit WindowState(QObject* parent = nullptr) : QObject(parent) {}

  Q_INVOKABLE void setFullscreen(QWindow* window, bool fullscreen) {
    if (!window) {
      return;
    }
    // A tiled Wayland window can also report WindowMaximized. Changing that
    // flag would send a separate maximize/unmaximize request to the compositor.
    auto states = window->windowStates();
    states.setFlag(Qt::WindowFullScreen, fullscreen);
    window->setWindowStates(states);
  }
};
