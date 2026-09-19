#pragma once
class QQmlEngine;
// Call before loading any application QML, for each engine independently.
void initializeFilesEngine(QQmlEngine& engine);
