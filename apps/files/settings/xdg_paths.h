#pragma once

#include <QString>
#include <QStringList>

// XDG Base Directory resolution for this application's own files (SPEC.md REQ-C-001). An unset,
// empty or relative XDG_CONFIG_HOME/XDG_STATE_HOME is ignored in favour of the $HOME fallback, as
// the specification requires. Resolved on every call; callers resolve once at startup.
class WarningSink;

namespace XdgPaths {
// Desktop names are validated filename components, never paths.
QStringList sessionOverrideCandidates(const QString& configPath, const QString& desktop, WarningSink& warnings);
QString sessionOverridePath(const QString& configPath, WarningSink& warnings);
QString configFilePath();    // $XDG_CONFIG_HOME/holonight-files/config.toml
QString stateDirPath();      // $XDG_STATE_HOME/holonight-files
QString stateFilePath();     // stateDirPath()/state.toml
QString userDirsFilePath();  // $XDG_CONFIG_HOME/user-dirs.dirs (shared file, no holonight-files segment)
QString dataDirPath();       // $XDG_DATA_HOME/holonight/holonight-files
QString placesFilePath();    // dataDirPath()/places.toml
}  // namespace XdgPaths
