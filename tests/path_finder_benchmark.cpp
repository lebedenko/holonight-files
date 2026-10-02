#include "path_finder_model.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTest>
#include <QTimer>

#include <algorithm>
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
      batch.append({.path = root + u'/' + relative, .relativePath = relative, .directory = false});
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
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList arguments = QCoreApplication::arguments();
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
