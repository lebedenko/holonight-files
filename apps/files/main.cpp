#include "directory_controller.h"
#include "initial_directory.h"
#include "settings/app_settings.h"
#include "settings/xdg_paths.h"
#include "state/state_store.h"
#include "warning_sink.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(HolonightFilesPlugin)

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("holonight-files"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("HoloNight Files"));
  QGuiApplication::setApplicationVersion(QStringLiteral(FILES_VERSION));
  QGuiApplication::setOrganizationDomain(QStringLiteral("holonight.org"));
  QGuiApplication::setDesktopFileName(QStringLiteral("org.holonight.Files"));
  QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.holonight.Files")));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("main", "HoloNight Files — a keyboard-driven file manager."));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("folder"), QCoreApplication::translate("main", "Folder to open"),
                               QStringLiteral("[folder]"));
  parser.process(app);
  const auto arguments = parser.positionalArguments();
  if (arguments.size() > 1) {
    QTextStream(stderr) << QCoreApplication::translate("main", "Open at most one folder.") << '\n';
    return EXIT_FAILURE;
  }
  // Read once, before any folder opens (SPEC.md REQ-C-005); both files are local and small.
  StderrWarningSink warnings;
  const auto settings = AppSettings::load(XdgPaths::configFilePath(), warnings);
  const bool restoreEnabled = settings.general().restoreLastLocation();
  // State is only consulted when it can matter, so a disabled restore never warns about it.
  const auto stored = restoreEnabled && arguments.isEmpty()
                          ? StateStore(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()).load(warnings)
                          : StateLoadResult{};
  const auto plan = planStartup(arguments, restoreEnabled, stored.last_location);
  QGuiApplication::setQuitOnLastWindowClosed(false);
  DirectoryController controller;
  controller.configureRestore(restoreEnabled);
  QObject::connect(&app, &QGuiApplication::lastWindowClosed, &controller, &DirectoryController::shutdown);
  QObject::connect(&controller, &DirectoryController::shutdownFinished, &app, &QCoreApplication::quit);
  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
      Qt::QueuedConnection);
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  if (engine.rootObjects().isEmpty()) {
    return EXIT_FAILURE;
  }
  // Deferred so QML bindings to controller's properties are connected before the first load
  // fires changed(), matching how holonight-viewer sequences ImageDocument::open().
  QTimer::singleShot(0, &controller, [&controller, plan] {
    if (plan.resolved) {
      controller.open(plan.resolved->path, plan.resolved->fallback_reason);
    } else {
      controller.openRestoreCandidate(plan.pending_candidate_path);
    }
  });
  return QGuiApplication::exec();
}
