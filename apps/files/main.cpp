#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTextStream>
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
  parser.process(app);
  if (!parser.positionalArguments().isEmpty()) {
    QTextStream(stderr) << QCoreApplication::translate("main", "This build does not yet support opening a folder.")
                        << '\n';
    return EXIT_FAILURE;
  }
  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
      Qt::QueuedConnection);
  engine.loadFromModule("HolonightFiles", "Main");
  if (engine.rootObjects().isEmpty()) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
