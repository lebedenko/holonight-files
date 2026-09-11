// T-118/T-119/T-122 (docs/sdd/file-operations/TASKS.md): a rendered VISUAL-selection trash task
// spanning a real, distinct filesystem, and the same flow against a trash location that fails
// validation for every item — proving a single confirmation covers the whole selection, per-item
// failures never raise a second destructive prompt, and REQ-F-044's "failure leaves the source
// untouched" holds under real per-partition trash directory selection, not a mocked filesystem
// (REQ-C-002).
//
// This combines two techniques that otherwise live in separate binaries for a reason each keeps
// documented at its own definition: fs_isolation's unshare(CLONE_NEWUSER|CLONE_NEWNS) (see
// fs_isolation.h) gives real distinct st_dev filesystems without root, and smoke.cpp's rendered
// QQmlApplicationEngine window exercises actual keyboard dispatch instead of calling handleKey()
// directly. Deliberately excluded from files-smoke (this binary's unshare() would grant
// CAP_DAC_OVERRIDE over files-smoke's chmod-000 permission fixtures — see fs_isolation.h) and from
// files-fsops-smoke (no QML/rendering dependency there). Because that same CAP_DAC_OVERRIDE defeats
// chmod-based permission denial on anything this namespace itself mounts, the "invalid trash
// location" fixture below exhausts the mount's inode budget instead (nr_inodes=), matching
// CrossFilesystem.FailedTrashLeavesSourceUntouched in cross_filesystem_test.cpp.

#include "directory_controller.h"
#include "fs_isolation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

#include <gtest/gtest.h>
#include <sys/statvfs.h>
#include <unistd.h>

Q_IMPORT_QML_PLUGIN(HolonightFilesPlugin)

namespace {

// Function-local statics rather than namespace-scope globals, set once from main() before any
// test runs and read-only from then on (mirrors cross_filesystem_test.cpp).
fs_isolation::SetupResult& setupResult() {
  static fs_isolation::SetupResult result;
  return result;
}
QString& xdgDataHomeForTest() {
  static QString path;
  return path;
}

// GTEST_SKIP() must expand directly inside the TEST() body to return from it.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SKIP_IF_UNAVAILABLE()                                      \
  if (!setupResult().available) {                                  \
    GTEST_SKIP() << setupResult().unavailableReason.toStdString(); \
  }

QString writeFile(const QString& path, const QByteArray& content = "hello") {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return {};
  }
  return path;
}

bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}

struct RenderedWindow {
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;

  RenderedWindow() {
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
  }

  bool activate() {
    if (engine.rootObjects().size() != 1) {
      return false;
    }
    window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window == nullptr) {
      return false;
    }
    window->requestActivate();
    return QTest::qWaitForWindowActive(window);
  }
};

}  // namespace

TEST(WindowCrossFilesystem, VisualDTrashesEntireSelectionOnForeignFilesystemInOneTask) {
  SKIP_IF_UNAVAILABLE();
  const auto foreign = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(foreign.isEmpty());
  ASSERT_FALSE(writeFile(foreign + "/a.txt").isEmpty());
  ASSERT_FALSE(writeFile(foreign + "/b.txt").isEmpty());
  ASSERT_FALSE(writeFile(foreign + "/c.txt").isEmpty());

  RenderedWindow rendered;
  ASSERT_TRUE(rendered.activate());
  rendered.controller.open(foreign);
  ASSERT_TRUE(settled(rendered.controller));

  // v + j + j selects all three entries; D requests trash confirmation for the whole selection
  // as a single TaskManager task (T-090), not three separate ones.
  QTest::keyClick(rendered.window, Qt::Key_V);
  QTest::keyClick(rendered.window, Qt::Key_J);
  QTest::keyClick(rendered.window, Qt::Key_J);
  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);

  auto* trashLabel = rendered.window->findChild<QObject*>("trashConfirmLabel");
  ASSERT_NE(trashLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return trashLabel->property("visible").toBool(); }));
  EXPECT_TRUE(trashLabel->property("rawText").toString().contains("Trash 3 item"));
  EXPECT_EQ(rendered.controller.tasks()->trashConfirmCount(), 3);

  QTest::keyClick(rendered.window, Qt::Key_Y);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !rendered.controller.tasks()->busy(); }));
  EXPECT_FALSE(rendered.controller.tasks()->hasPrompt());
  EXPECT_EQ(rendered.controller.tasks()->itemsTotal(), 3);
  EXPECT_TRUE(rendered.controller.tasks()->lastSummaryText().contains("3/3 succeeded"));

  for (const auto* name : {"a.txt", "b.txt", "c.txt"}) {
    EXPECT_FALSE(QFile::exists(foreign + "/" + name));
    EXPECT_TRUE(QFile::exists(foreign + "/.Trash-" + QString::number(::getuid()) + "/files/" + name));
  }
}

TEST(WindowCrossFilesystem, VisualDValidThenInvalidTrashLocationsFailPerItemWithNoExtraPrompt) {
  SKIP_IF_UNAVAILABLE();

  RenderedWindow rendered;
  ASSERT_TRUE(rendered.activate());

  // Pass 1: a real, distinct, otherwise-ordinary filesystem. Per-partition trash validates and
  // both selected items succeed in one confirmation (T-119's "valid" location).
  const auto valid = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(valid.isEmpty());
  ASSERT_FALSE(writeFile(valid + "/keep-a.txt").isEmpty());
  ASSERT_FALSE(writeFile(valid + "/keep-b.txt").isEmpty());
  rendered.controller.open(valid);
  ASSERT_TRUE(settled(rendered.controller));
  QTest::keyClick(rendered.window, Qt::Key_V);
  QTest::keyClick(rendered.window, Qt::Key_J);
  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);
  auto* trashLabel = rendered.window->findChild<QObject*>("trashConfirmLabel");
  ASSERT_NE(trashLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return trashLabel->property("visible").toBool(); }));
  QTest::keyClick(rendered.window, Qt::Key_Y);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !rendered.controller.tasks()->busy(); }));
  EXPECT_TRUE(rendered.controller.tasks()->lastSummaryText().contains("2/2 succeeded"));
  EXPECT_FALSE(QFile::exists(valid + "/keep-a.txt"));
  EXPECT_FALSE(QFile::exists(valid + "/keep-b.txt"));

  // Pass 2: a second, distinct filesystem whose inode budget is exhausted by its two files plus
  // the mount's own root, leaving none for TrashService to mkdir() a fallback .Trash-$uid under —
  // a real, capability-independent validation failure (see file header; mirrors
  // CrossFilesystem.FailedTrashLeavesSourceUntouched). Both items fail independently in the same
  // task; REQ-F-044 leaves both sources untouched, and no further prompt of any kind appears.
  const auto invalid = fs_isolation::mountFreshTmpfs("nr_inodes=3");
  ASSERT_FALSE(invalid.isEmpty());
  ASSERT_FALSE(writeFile(invalid + "/stuck-a.txt").isEmpty());
  ASSERT_FALSE(writeFile(invalid + "/stuck-b.txt").isEmpty());
  rendered.controller.open(invalid);
  ASSERT_TRUE(settled(rendered.controller));
  QTest::keyClick(rendered.window, Qt::Key_V);
  QTest::keyClick(rendered.window, Qt::Key_J);
  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return trashLabel->property("visible").toBool(); }));
  EXPECT_EQ(rendered.controller.tasks()->trashConfirmCount(), 2);

  QTest::keyClick(rendered.window, Qt::Key_Y);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !rendered.controller.tasks()->busy(); }));
  EXPECT_FALSE(rendered.controller.tasks()->hasPrompt());
  EXPECT_EQ(rendered.controller.tasks()->promptKind(), TaskManager::PromptKind::None);
  EXPECT_TRUE(rendered.controller.tasks()->lastSummaryText().contains("0/2 succeeded"));
  EXPECT_TRUE(QFile::exists(invalid + "/stuck-a.txt"));
  EXPECT_TRUE(QFile::exists(invalid + "/stuck-b.txt"));

  // No extra prompt snuck in behind the confirmed one: a further short wait still shows no
  // prompt, and the window is still alive to receive input.
  QTest::qWait(50);
  EXPECT_FALSE(rendered.controller.tasks()->hasPrompt());
  EXPECT_TRUE(rendered.window->isVisible());
}

// Supplemental to T-119/T-122: metadata failure after successful validation is not a
// mixed validation-failure fixture. All selected sources share one trash location.
TEST(WindowCrossFilesystem, VisualDMixedMetadataFailureHasOneConfirmationAndPreservesFailedSource) {
  SKIP_IF_UNAVAILABLE();
  const auto foreign = fs_isolation::mountFreshTmpfs("nr_inodes=7");
  ASSERT_FALSE(foreign.isEmpty());
  struct statvfs inodeInfo{};
  const auto mountPath = QFile::encodeName(foreign);
  ASSERT_EQ(::statvfs(mountPath.constData(), &inodeInfo), 0);
  ASSERT_EQ(inodeInfo.f_files, 7U);
  ASSERT_EQ(inodeInfo.f_ffree, 6U);  // Only the root inode exists.
  ASSERT_FALSE(writeFile(foreign + "/a.txt", "successful payload").isEmpty());
  ASSERT_FALSE(writeFile(foreign + "/b.txt", "retained payload").isEmpty());
  ASSERT_EQ(::statvfs(mountPath.constData(), &inodeInfo), 0);
  ASSERT_EQ(inodeInfo.f_ffree, 4U);  // Three trash directories and one metadata file.

  RenderedWindow rendered;
  ASSERT_TRUE(rendered.activate());
  rendered.controller.open(foreign);
  ASSERT_TRUE(settled(rendered.controller));
  auto* tasks = rendered.controller.tasks();
  int confirmations = 0;
  bool afterConfirmation = false;
  bool extraPrompt = false;
  quint64 previousPrompt = 0;
  // Context dies before these captured counters, including on an assertion failure.
  QObject observer;
  QObject::connect(tasks, &TaskManager::changed, &observer, [&] {
    if (tasks->hasPrompt()) {
      if (afterConfirmation) {
        extraPrompt = true;
      }
      if (tasks->promptKind() == TaskManager::PromptKind::TrashConfirm && tasks->promptId() != previousPrompt) {
        ++confirmations;
        previousPrompt = tasks->promptId();
      }
    }
  });
  QTest::keyClick(rendered.window, Qt::Key_V);
  QTest::keyClick(rendered.window, Qt::Key_J);
  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);
  auto* trashLabel = rendered.window->findChild<QObject*>("trashConfirmLabel");
  ASSERT_NE(trashLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return trashLabel->property("visible").toBool(); }));
  EXPECT_EQ(tasks->trashConfirmCount(), 2);
  EXPECT_EQ(confirmations, 1);
  afterConfirmation = true;
  QTest::keyClick(rendered.window, Qt::Key_Y);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !tasks->busy(); }));
  QTest::qWait(50);
  EXPECT_EQ(confirmations, 1);
  EXPECT_FALSE(extraPrompt);
  EXPECT_FALSE(tasks->hasPrompt());
  EXPECT_EQ(tasks->itemsTotal(), 2);
  EXPECT_EQ(tasks->itemsDone(), 2);
  EXPECT_TRUE(tasks->lastSummaryText().contains("1/2 succeeded")) << tasks->lastSummaryText().toStdString();

  EXPECT_TRUE(tasks->lastSummaryText().contains("Trash metadata"));

  const auto trash = foreign + "/.Trash-" + QString::number(::getuid());
  EXPECT_FALSE(QFile::exists(foreign + "/a.txt"));
  QFile successful(trash + "/files/a.txt");
  ASSERT_TRUE(successful.open(QIODevice::ReadOnly));
  EXPECT_EQ(successful.readAll(), "successful payload");
  QFile metadata(trash + "/info/a.txt.trashinfo");
  ASSERT_TRUE(metadata.open(QIODevice::ReadOnly));
  const auto info = metadata.readAll();
  EXPECT_TRUE(info.startsWith("[Trash Info]\n"));
  EXPECT_TRUE(info.contains("Path=a.txt\n"));
  EXPECT_TRUE(info.contains("DeletionDate="));
  QFile failed(foreign + "/b.txt");
  ASSERT_TRUE(failed.open(QIODevice::ReadOnly));
  EXPECT_EQ(failed.readAll(), "retained payload");
  EXPECT_FALSE(QFile::exists(trash + "/files/b.txt"));
  EXPECT_FALSE(QFile::exists(trash + "/info/b.txt.trashinfo"));
  ASSERT_EQ(::statvfs(mountPath.constData(), &inodeInfo), 0);
  EXPECT_EQ(inodeInfo.f_ffree, 0U);
}

int main(int argc, char* argv[]) {
  setupResult() = fs_isolation::setUp();
  qunsetenv("QT_QUICK_CONTROLS_STYLE");
  qunsetenv("QT_QUICK_CONTROLS_FALLBACK_STYLE");
  qunsetenv("QT_QUICK_CONTROLS_CONF");
  const QGuiApplication app(argc, argv);
  const auto fixtureRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/fixtures/fsops-window");
  if (!QDir().mkpath(fixtureRoot)) {
    return 1;
  }
  qputenv("TMPDIR", QFile::encodeName(fixtureRoot));
  qputenv("XDG_CACHE_HOME", QFile::encodeName(fixtureRoot + "/cache"));
  static QTemporaryDir home;  // static duration: lives for the process, cleaned up at exit
  if (setupResult().available) {
    xdgDataHomeForTest() = home.path();
    qputenv("XDG_DATA_HOME", xdgDataHomeForTest().toLocal8Bit());
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
