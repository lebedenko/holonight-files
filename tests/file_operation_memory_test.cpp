// SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "file_operation_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/resource.h>

namespace {
constexpr long kGrowthLimitKb = 200 * 1024;

// Missing fields, invalid units and parse errors must fail, never become zero.
long rssKb(const QByteArray& field) {
  QFile status(QStringLiteral("/proc/self/status"));
  if (!status.open(QIODevice::ReadOnly)) {
    return -1;
  }
  for (const auto& line : status.readAll().split('\n')) {
    const auto parts = line.simplified().split(' ');
    if (parts.size() == 3 && parts[0] == field && parts[2] == "kB") {
      bool parsed = false;
      const auto value = parts[1].toLong(&parsed);
      return parsed && value > 0 ? value : -1;
    }
  }
  return -1;
}

void enforceAddressSpaceLimit() {
  constexpr rlim_t kLimit = rlim_t{1} << 30;
  struct rlimit limit{};
  ASSERT_EQ(::getrlimit(RLIMIT_AS, &limit), 0) << std::strerror(errno);
  limit.rlim_cur = kLimit;
  ASSERT_EQ(::setrlimit(RLIMIT_AS, &limit), 0) << std::strerror(errno);
  ASSERT_EQ(::getrlimit(RLIMIT_AS, &limit), 0) << std::strerror(errno);
  ASSERT_EQ(limit.rlim_cur, kLimit);
}

TEST(FileOperationMemory, MultiGigabyteCopyMeetsThroughputFloorWithBoundedMemory) {
  ASSERT_NO_FATAL_FAILURE(enforceAddressSpaceLimit());
  const auto root = QCoreApplication::applicationDirPath() + "/fixtures/memory";
  ASSERT_TRUE(QDir().mkpath(root));
  QTemporaryDir dir(root + "/copy-XXXXXX");
  ASSERT_TRUE(dir.isValid());
  constexpr qint64 kFileSize = qint64{2} << 30;
  const auto source = dir.filePath("source.bin");
  const auto destination = dir.filePath("destination.bin");
  {
    QFile file(source);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray chunk(1 << 20, 'x');
    for (qint64 written = 0; written < kFileSize; written += chunk.size()) {
      ASSERT_EQ(file.write(chunk), chunk.size());
    }
    ASSERT_TRUE(file.flush());
  }
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  const auto baseline = rssKb("VmRSS:");
  ASSERT_GT(baseline, 0);
  QElapsedTimer timer;
  timer.start();
  const auto result = FileOperationService::copyEntry(source, destination, false, cancel);
  const auto elapsedNs = timer.nsecsElapsed();
  const auto peak = rssKb("VmHWM:");
  ASSERT_GT(peak, 0);
  ASSERT_GE(peak, baseline);
  ASSERT_GT(elapsedNs, 0);
  const double throughput = 2048.0 / (static_cast<double>(elapsedNs) / 1e9);
  const auto growth = peak - baseline;
  std::cout << "RLIMIT_AS=1024 MiB; current RSS baseline=" << baseline << " KiB; peak RSS=" << peak
            << " KiB; growth=" << growth << " KiB; throughput=" << throughput << " MiB/s\n";
  EXPECT_TRUE(result.complete()) << result.reason.toStdString();
  EXPECT_EQ(QFileInfo(destination).size(), kFileSize);
  EXPECT_GT(throughput, 50.0);
  EXPECT_LT(growth, kGrowthLimitKb);
}

// Run only in a separate invocation; these allocations must never precede the copy benchmark.
void validateRssMeasurement() {
  ASSERT_NO_FATAL_FAILURE(enforceAddressSpaceLimit());
  constexpr size_t kEarlierSize = size_t{320} << 20;
  constexpr size_t kLaterSize = size_t{256} << 20;
  void* earlier = ::mmap(nullptr, kEarlierSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(earlier, MAP_FAILED) << std::strerror(errno);
  std::memset(earlier, 1, kEarlierSize);
  ASSERT_EQ(::munmap(earlier, kEarlierSize), 0);
  const auto baseline = rssKb("VmRSS:");
  const auto oldPeak = rssKb("VmHWM:");
  ASSERT_GT(baseline, 0);
  ASSERT_GT(oldPeak, 0);
  void* later = ::mmap(nullptr, kLaterSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(later, MAP_FAILED) << std::strerror(errno);
  std::memset(later, 2, kLaterSize);
  const auto current = rssKb("VmRSS:");
  const auto peak = rssKb("VmHWM:");
  EXPECT_EQ(::munmap(later, kLaterSize), 0);
  ASSERT_GT(current, 0);
  ASSERT_GT(peak, 0);
  std::cout << "Controlled allocation: current baseline=" << baseline << " KiB; current after=" << current
            << " KiB; earlier peak=" << oldPeak << " KiB; final peak=" << peak << " KiB; old delta=" << peak - oldPeak
            << " KiB; corrected growth=" << peak - baseline << " KiB\n";
  EXPECT_GT(current - baseline, kGrowthLimitKb);
  EXPECT_LT(peak - oldPeak, kGrowthLimitKb);
  EXPECT_GT(peak - baseline, kGrowthLimitKb);
}
}  // namespace

int main(int argc, char* argv[]) {
  const QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  if (QCoreApplication::arguments().contains("--validate-rss-measurement")) {
    validateRssMeasurement();
    return ::testing::Test::HasFailure() ? 1 : 0;
  }
  return RUN_ALL_TESTS();
}
