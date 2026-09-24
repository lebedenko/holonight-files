#include "quick_look_presentation_model.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"
#include "quick_look_presentation_model_test_access.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
#include <limits>
#include <memory>

namespace {
using Kind = QuickLookPresentationModel::Kind;
using Access = QuickLookPresentationModelTestAccess;

void configure(QuickLookPresentationModel& model) {
  model.setWindowSize({1000, 800});
  model.setCardPadding(10);
  model.setCaptionReserve(42);
  model.setFrameCaptionGap(8);
  model.setMinCompactWidth(240);
  model.setIconExtent(96);
  model.setHintImplicitWidth(180);
}

void selectFile(PreviewService& service, const QString& path) {
  const QFileInfo info(path);
  service.setTarget(path, info.isDir(), info.size(), info.lastModified(), 0100644, false, {});
}

class QuickLookPresentationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    model_.classBegin();
    configure(model_);
    model_.setPreview(&service_);
    model_.componentComplete();
  }

  QString image(const QString& name, QSize size) {
    return files_test::writeFile(dir_, name, files_test::renderPngBytes(size));
  }

  bool waitForPreview() {
    return QTest::qWaitFor([this] { return !service_.busy(); });
  }

  QTemporaryDir& directory() { return dir_; }
  PreviewService& service() { return service_; }
  QuickLookPresentationModel& model() { return model_; }

 private:
  QTemporaryDir dir_{files_test::fixturePattern("quicklook-model")};
  PreviewService service_;
  QuickLookPresentationModel model_;
};
}  // namespace

TEST(QuickLookClassification, PreservesPrecedenceAndIntermediateLoadingStates) {
  EXPECT_EQ(Access::classify(false, true, "image/png", true, true), Kind::None);
  EXPECT_EQ(Access::classify(true, true, "image/png", true, true), Kind::Compact);
  EXPECT_EQ(Access::classify(true, false, {}, false, true), Kind::Compact);
  EXPECT_EQ(Access::classify(true, false, {}, false, false), Kind::Pending);
  EXPECT_EQ(Access::classify(true, true, {}, false, false), Kind::Pending);
  EXPECT_EQ(Access::classify(true, true, "image/png", true, false), Kind::Image);
  EXPECT_EQ(Access::classify(true, true, "text/plain", true, false), Kind::Text);
  EXPECT_EQ(Access::classify(true, true, "application/octet-stream", false, false), Kind::Pending);
  EXPECT_EQ(Access::classify(true, false, "application/octet-stream", false, false), Kind::Compact);
  EXPECT_EQ(Access::classify(true, false, "inode/directory", false, false), Kind::Compact);
}

TEST_F(QuickLookPresentationTest, StartsCompactAndClassifiesBeforeBusyIsSet) {
  EXPECT_EQ(model().kind(), Kind::None);
  EXPECT_EQ(model().cardSize(), QSizeF(240, 166));
  EXPECT_EQ(model().frameSize(), QSizeF(96, 96));
  const auto path = image("initial.png", {600, 400});
  ASSERT_FALSE(path.isEmpty());
  bool sawPreDispatch = false;
  QObject::connect(&service(), &PreviewService::changed, &model(), [&] {
    if (service().hasEntry() && service().mimeType().isEmpty() && !service().busy()) {
      sawPreDispatch = true;
      EXPECT_EQ(model().kind(), Kind::Pending);
      EXPECT_EQ(model().cardSize(), QSizeF(240, 166));
    }
  });
  selectFile(service(), path);
  EXPECT_TRUE(sawPreDispatch);
  EXPECT_EQ(model().kind(), Kind::Pending);
  ASSERT_TRUE(waitForPreview());
  EXPECT_EQ(model().kind(), Kind::Image);
  EXPECT_EQ(model().frameSize(), QSizeF(900, 600));
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
}

TEST_F(QuickLookPresentationTest, PortraitRetainsCaptionWidthAndZeroBoundsDoNotDivide) {
  const auto path = image("portrait.png", {100, 1000});
  ASSERT_FALSE(path.isEmpty());
  selectFile(service(), path);
  ASSERT_TRUE(waitForPreview());
  EXPECT_EQ(model().frameSize(), QSizeF(66, 666));
  EXPECT_EQ(model().cardSize(), QSizeF(240, 736));
  const auto calls = Access::requestedSizeCallCount(model());
  model().setWindowSize({0, 0});
  EXPECT_EQ(model().frameSize(), QSizeF(0, 0));
  EXPECT_EQ(model().cardSize(), QSizeF(0, 0));
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
  model().setWindowSize({1000, 800});
  EXPECT_EQ(model().cardSize(), QSizeF(240, 736));
  // Returning from invalid bounds to the already submitted size needs no request.
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
}

TEST_F(QuickLookPresentationTest, TextFillsAvailableBoundsAndErrorsUseCompactLayout) {
  const auto text = files_test::writeSmallText(directory());
  ASSERT_FALSE(text.isEmpty());
  selectFile(service(), text);
  ASSERT_TRUE(waitForPreview());
  EXPECT_EQ(model().kind(), Kind::Text);
  EXPECT_EQ(model().frameSize(), QSizeF(900, 666));
  EXPECT_EQ(model().cardSize(), QSizeF(920, 736));
  service().setTarget(directory().filePath("missing"), false, -1, {}, 0100644, true,
                      QStringLiteral("Broken symbolic link"));
  EXPECT_EQ(model().kind(), Kind::Compact);
  EXPECT_EQ(model().frameSize(), QSizeF(96, 96));
  EXPECT_EQ(model().cardSize(), QSizeF(240, 166));
}

TEST_F(QuickLookPresentationTest, PendingAndAbsentRetainInputsAcrossShrinkAndRestore) {
  const auto first = image("landscape.png", {600, 400});
  const auto next = image("portrait.png", {100, 1000});
  ASSERT_FALSE(first.isEmpty());
  ASSERT_FALSE(next.isEmpty());
  selectFile(service(), first);
  ASSERT_TRUE(waitForPreview());
  const auto calls = Access::requestedSizeCallCount(model());
  selectFile(service(), next);
  // Worker completion is queued: no event processing is needed to exercise the pending interval.
  ASSERT_EQ(model().kind(), Kind::Pending);
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
  model().setWindowSize({500, 400});
  EXPECT_EQ(model().frameSize(), QSizeF(440, 293));
  EXPECT_EQ(model().cardSize(), QSizeF(460, 363));
  model().setWindowSize({1000, 800});
  EXPECT_EQ(model().frameSize(), QSizeF(900, 600));
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
  service().clear();
  EXPECT_EQ(model().kind(), Kind::None);
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
  selectFile(service(), next);
  ASSERT_TRUE(waitForPreview());
  EXPECT_EQ(model().kind(), Kind::Image);
  EXPECT_EQ(model().cardSize(), QSizeF(240, 736));
}

TEST_F(QuickLookPresentationTest, MeasurementsUpdateLayoutWithoutRequestFeedback) {
  const auto calls = Access::requestedSizeCallCount(model());
  model().setHintImplicitWidth(400);
  EXPECT_EQ(model().cardSize(), QSizeF(420, 166));
  model().setMinCompactWidth(500);
  EXPECT_EQ(model().cardSize(), QSizeF(500, 166));
  model().setIconExtent(120);
  EXPECT_EQ(model().frameSize(), QSizeF(120, 120));
  EXPECT_EQ(model().cardSize(), QSizeF(500, 190));
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
  model().setCaptionReserve(60);
  EXPECT_EQ(model().cardSize(), QSizeF(500, 208));
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service()), QSize(900, 648));
  model().setFrameCaptionGap(10);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service()), QSize(900, 646));
  model().setCardPadding(12);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service()), QSize(896, 642));
}

TEST_F(QuickLookPresentationTest, FractionalBoundsAndDprRoundRequestsIndependentlyOfFrame) {
  const auto path = image("small.png", {60, 40});
  ASSERT_FALSE(path.isEmpty());
  selectFile(service(), path);
  ASSERT_TRUE(waitForPreview());
  model().setWindowSize({1001, 801});
  EXPECT_EQ(model().frameSize(), QSizeF(900, 600));
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
  model().setDevicePixelRatio(1.5);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service()), QSize(1351, 1000));
  EXPECT_EQ(model().frameSize(), QSizeF(900, 600));
  model().setDevicePixelRatio(0);
  EXPECT_EQ(model().devicePixelRatio(), 1);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service()), QSize(901, 667));
  const auto calls = Access::requestedSizeCallCount(model());
  model().setDevicePixelRatio(-1);
  model().setDevicePixelRatio(std::numeric_limits<qreal>::quiet_NaN());
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
}

TEST_F(QuickLookPresentationTest, DuplicateInputsAndSameRoundedSizeDoNotResubmit) {
  QSignalSpy inputs(&model(), &QuickLookPresentationModel::inputsChanged);
  QSignalSpy presentation(&model(), &QuickLookPresentationModel::presentationChanged);
  const auto calls = Access::requestedSizeCallCount(model());
  configure(model());
  model().setPreview(&service());
  model().setDevicePixelRatio(1);
  EXPECT_EQ(inputs.count(), 0);
  EXPECT_EQ(presentation.count(), 0);
  model().setWindowSize({1000.1, 800});
  EXPECT_EQ(inputs.count(), 1);
  EXPECT_EQ(presentation.count(), 0);
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls);
}

TEST(QuickLookPresentationLifecycle, QmlConstructionDefersFirstRequest) {
  PreviewService service;
  QuickLookPresentationModel model;
  model.classBegin();
  model.setPreview(&service);
  configure(model);
  EXPECT_EQ(Access::requestedSizeCallCount(model), 0);
  model.componentComplete();
  EXPECT_EQ(Access::requestedSizeCallCount(model), 1);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(service), QSize(900, 666));
  model.componentComplete();
  EXPECT_EQ(Access::requestedSizeCallCount(model), 1);
}

TEST_F(QuickLookPresentationTest, ReplacementDisconnectsAndDestructionResetsRetainedState) {
  const auto path = image("landscape.png", {600, 400});
  ASSERT_FALSE(path.isEmpty());
  selectFile(service(), path);
  ASSERT_TRUE(waitForPreview());
  auto replacement = std::make_unique<PreviewService>();
  const auto calls = Access::requestedSizeCallCount(model());
  model().setPreview(replacement.get());
  EXPECT_EQ(model().kind(), Kind::None);
  EXPECT_EQ(model().cardSize(), QSizeF(240, 166));
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls + 1);
  EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(*replacement), QSize(900, 666));
  QSignalSpy presentation(&model(), &QuickLookPresentationModel::presentationChanged);
  service().clear();
  EXPECT_EQ(presentation.count(), 0);
  selectFile(*replacement, path);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !replacement->busy(); }));
  EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
  QSignalSpy previewChanges(&model(), &QuickLookPresentationModel::previewChanged);
  replacement.reset();
  EXPECT_EQ(previewChanges.count(), 1);
  EXPECT_EQ(model().preview(), nullptr);
  EXPECT_EQ(model().kind(), Kind::None);
  EXPECT_EQ(model().cardSize(), QSizeF(240, 166));
  model().setWindowSize({500, 400});
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls + 1);
  model().setPreview(&service());
  EXPECT_EQ(Access::requestedSizeCallCount(model()), calls + 2);
}

TEST_F(QuickLookPresentationTest, ObserversSeeConsistentOutputs) {
  const auto path = image("landscape.png", {600, 400});
  ASSERT_FALSE(path.isEmpty());
  bool sawImage = false;
  QObject::connect(&model(), &QuickLookPresentationModel::presentationChanged, &model(), [&] {
    if (model().kind() == Kind::Image) {
      sawImage = true;
      EXPECT_EQ(model().frameSize(), QSizeF(900, 600));
      EXPECT_EQ(model().cardSize(), QSizeF(920, 670));
    }
  });
  selectFile(service(), path);
  ASSERT_TRUE(waitForPreview());
  EXPECT_TRUE(sawImage);
}

TEST_F(QuickLookPresentationTest, SvgFractionalGeometryAndDprDriveSharpPreview) {
  const auto path = files_test::writeFile(directory(), "fractional.svg",
                                          "<svg xmlns='http://www.w3.org/2000/svg' width='0.25' height='0.5'/>");
  model().setDevicePixelRatio(1.25);
  service().setQuickLookActive(true);
  selectFile(service(), path);
  ASSERT_TRUE(waitForPreview());
  EXPECT_EQ(model().kind(), Kind::Image);
  EXPECT_EQ(model().frameSize(), QSizeF(333, 666));
  EXPECT_EQ(service().documentSize(), QSizeF(0.25, 0.5));
  EXPECT_GE(service().image().height(), 833);
  const auto requests = Access::requestedSizeCallCount(model());
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !PreviewServiceTestAccess::resizePending(service()) && !service().busy(); }));
  EXPECT_EQ(Access::requestedSizeCallCount(model()), requests);
}
