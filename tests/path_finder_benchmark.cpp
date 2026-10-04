#include "path_finder_model.h"
#include "path_index_store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <holonight/search/index.h>
#include <iostream>
#include <optional>

namespace {
PathScanFunction syntheticScanner(int count) {
  return [count](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool& cancelled,
                 const PathBatchReady& batchReady) {
    QVector<PathCandidate> batch;
    batch.reserve(4096);
    for (int i = 0; i < count && !cancelled.load(); ++i) {
      const QString folder = QStringLiteral("home/projects/repository-%1/module-%2/").arg(i / 400).arg((i / 40) % 10);
      const QString name = i % 251 == 0 ? QStringLiteral("lambda-audit-%1.cpp").arg(i)
                                        : QStringLiteral("document-%1-%2.cpp").arg(i % 997).arg(i);
      const QString relative = folder + name;
      batch.append({.path = root + u'/' + relative, .relative_path = relative, .directory = false});
      if (batch.size() == 4096) {
        batchReady(std::move(batch));
        batch.clear();
        batch.reserve(4096);
      }
    }
    if (!cancelled.load() && !batch.isEmpty()) {
      batchReady(std::move(batch));
    }
    return true;
  };
}

void printMemory() {
  QFile status(QStringLiteral("/proc/self/status"));
  if (!status.open(QIODevice::ReadOnly)) {
    return;
  }
  for (const QByteArray& line : status.readAll().split('\n')) {
    if (line.startsWith("VmRSS:") || line.startsWith("VmHWM:")) {
      std::cout << line.constData() << '\n';
    }
  }
}

std::optional<QVector<qint64>> measureQuery(PathFinderModel& model, const QString& base) {
  QVector<qint64> samples;
  for (int sample = 0; sample < 20; ++sample) {
    const QString query = sample % 2 == 0 ? base : base + u' ';
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool completed = false;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &loop, [&] {
      completed = true;
      loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QElapsedTimer timer;
    timer.start();
    model.setQuery(query);
    timeout.start(5000);
    loop.exec();
    if (!completed) {
      return std::nullopt;
    }
    samples.append(timer.nsecsElapsed() / 1'000'000);
  }
  std::ranges::sort(samples);
  return samples;
}
void reconstructBatch(HolonightSearch::Index& index, const QVector<PathCandidate>& batch) {
  QVector<HolonightSearch::Record> files;
  QVector<HolonightSearch::Record> directories;
  for (const auto& candidate : batch) {
    HolonightSearch::Record record{
        .id = candidate.path,
        .fields =
            {
                {.name = QStringLiteral("name"), .text = QFileInfo(candidate.relative_path).fileName(), .weight = 0},
                {.name = QStringLiteral("path"), .text = candidate.relative_path, .weight = 0},
            },
        .boost = 0,
    };
    (candidate.directory ? directories : files).append(std::move(record));
  }
  index.append(QStringLiteral("files"), std::move(files));
  index.append(QStringLiteral("directories"), std::move(directories));
}

bool measureColdSearch(const QString& root) {
  PathFinderModel cold;
  QElapsedTimer timer;
  timer.start();
  cold.start(root, false);
  if (!QTest::qWaitFor([&] { return !cold.scanning(); }, 120000)) {
    return false;
  }
  std::cout << "cold_index_readiness_ms=" << timer.elapsed() << " records=" << cold.indexedCount() << '\n';
  const auto samples = measureQuery(cold, QStringLiteral("path finder"));
  if (!samples) {
    return false;
  }
  std::cout << "cold_query_p50_ms=" << samples->at(10) << " cold_query_p95_ms=" << samples->at(18) << '\n';
  return true;
}

int persistenceBenchmark(const QString& root) {
  QTemporaryDir directory;
  const auto path = directory.filePath("snapshot");
  qputenv("XDG_CACHE_HOME", directory.filePath("cache").toUtf8());
  qputenv("XDG_CONFIG_HOME", directory.filePath("config").toUtf8());
  if (!measureColdSearch(root)) {
    return 11;
  }
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile(SearchSettings(), diagnostics);
  const std::atomic_bool cancelled = false;
  QElapsedTimer timer;
  timer.start();
  {
    PathIndexStore writer(path, root, policy.fingerprint());
    if (!scanPaths(root, false, policy, cancelled,
                   [&](const QVector<PathCandidate>& batch) { writer.append(batch); }) ||
        !writer.commit(QDateTime::currentMSecsSinceEpoch(), cancelled)) {
      return 6;
    }
  }
  std::cout << "cold_traversal_persist_ms=" << timer.elapsed() << " disk_bytes=" << QFileInfo(path).size() << '\n';
  qint64 reconstructionNs = 0;
  qint64 completed = 0;
  HolonightSearch::Index index;
  timer.restart();
  const bool valid = PathIndexStore::load(
      path, root, policy.fingerprint(), cancelled,
      [&](const QVector<PathCandidate>& batch) {
        QElapsedTimer reconstruction;
        reconstruction.start();
        reconstructBatch(index, batch);
        reconstructionNs += reconstruction.nsecsElapsed();
      },
      completed);
  if (!valid) {
    return 7;
  }
  std::cout << "restored_readiness_ms=" << timer.elapsed() << " reconstruction_ms=" << reconstructionNs / 1000000
            << " records=" << index.size() << '\n';
  timer.restart();
  const auto results = index.search(QStringLiteral("path finder"), {
                                                                       .limit = 96,
                                                                       .profile = HolonightSearch::Profile::Path,
                                                                       .cancelled = &cancelled,
                                                                       .source = QStringLiteral("files"),
                                                                   });
  std::cout << "restored_query_ms=" << timer.elapsed() << " hits=" << results.size() << '\n';
  printMemory();
  bool first = true;
  PathFinderModel model([&](const QString& selected, bool hidden, const SearchExclusionPolicy& exclusions,
                            const std::atomic_bool& cancel, const PathBatchReady& ready) {
    if (first) {
      first = false;
      qint64 scanTime = 0;
      return PathIndexStore::load(path, selected, exclusions.fingerprint(), cancel, ready, scanTime);
    }
    return scanPaths(selected, hidden, exclusions, cancel, ready);
  });
  model.start(root, false);
  if (!QTest::qWaitFor([&] { return !model.scanning(); }, 120000)) {
    return 8;
  }
  std::cout << "refresh_baseline_memory:" << '\n';
  printMemory();
  model.invalidatePaths({root});
  if (!QTest::qWaitFor([&] { return model.scanning(); }, 5000)) {
    return 9;
  }
  const auto samples = measureQuery(model, QStringLiteral("path finder"));
  if (!samples) {
    return 10;
  }
  std::cout << "refresh_query_p50_ms=" << samples->at(10) << " refresh_query_p95_ms=" << samples->at(18) << '\n';
  printMemory();
  if (!QTest::qWaitFor([&] { return !model.scanning(); }, 120000)) {
    return 12;
  }
  std::cout << "refresh_complete_memory:" << '\n';
  printMemory();
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--persist")) {
    return persistenceBenchmark(QDir(arguments.at(2)).absolutePath());
  }
  if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--traverse")) {
    const QString& root = arguments.at(2);
    QStringList diagnostics;
    const auto policy = SearchExclusionPolicy::compile(SearchSettings(), diagnostics);
    const std::atomic_bool cancelled = false;
    for (const bool prune : {false, true}) {
      QElapsedTimer traversal;
      int candidates = 0;
      traversal.start();
      const bool available =
          scanPaths(root, true, prune ? policy : SearchExclusionPolicy{}, cancelled,
                    [&](const QVector<PathCandidate>& batch) { candidates += static_cast<int>(batch.size()); });
      if (!available) {
        return 3;
      }
      std::cout << "pruned=" << prune << " candidates=" << candidates
                << " traversal_us=" << traversal.nsecsElapsed() / 1000 << '\n';
    }
    PathFinderModel model;
    model.setIncludeHidden(true);
    model.start(root, false);
    if (!QTest::qWaitFor([&] { return !model.scanning(); }, 120'000)) {
      return 4;
    }
    model.stop();
    model.start(root, false);
    std::cout << "reopen_reused=" << !model.scanning() << " records=" << model.indexedCount() << '\n';
    const auto samples = measureQuery(model, QStringLiteral("needle"));
    if (!samples) {
      return 5;
    }
    std::cout << "warm_query_p50_ms=" << samples->at(10) << " warm_query_p95_ms=" << samples->at(18) << '\n';
    return 0;
  }
  const int count = arguments.size() > 1 ? arguments.at(1).toInt() : 2'000'000;
  PathFinderModel model(syntheticScanner(count));
  QElapsedTimer timer;
  qint64 firstResultMs = -1;
  QObject::connect(&model, &QAbstractItemModel::modelReset, &app, [&] {
    if (firstResultMs < 0 && model.rowCount() > 0) {
      firstResultMs = timer.elapsed();
    }
  });
  timer.start();
  model.start(QStringLiteral("/synthetic"), false);
  model.setQuery(QStringLiteral("lambda audit"));
  if (!QTest::qWaitFor([&] { return !model.scanning() && firstResultMs >= 0; }, 120'000)) {
    return 1;
  }
  std::cout << "records=" << model.indexedCount() << " cold_scan_ms=" << timer.elapsed()
            << " first_result_ms=" << firstResultMs << '\n';
  printMemory();
  // Alternating a trailing space models a final keystroke with the same terms.
  for (const QString& query : {
           QStringLiteral("lambda"),
           QStringLiteral("lambda audit"),
           QStringLiteral("audit lambda"),
           QStringLiteral("document 42"),
       }) {
    const auto samples = measureQuery(model, query);
    if (!samples) {
      return 2;
    }
    std::cout << "query=" << query.toStdString() << " p50_ms=" << samples->at(10) << " p95_ms=" << samples->at(18)
              << '\n';
  }
}
