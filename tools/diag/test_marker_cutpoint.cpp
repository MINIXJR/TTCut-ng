// Gate for a finding of the full map check (2026-10-11):
// "Set cut-in" / "Set cut-out" in a marker's context menu ends in
// TTCutMainWindow::onStreamPointSetCutIn/-Out, which jumps to the frame and
// calls TTCutFrameNavigation::onSetCutIn/-Out directly. The cut-point rule
// (isCutInPoint / isCutOutPoint) only enabled or disabled the two buttons;
// the slots did not test it, so a marker set a cut point the buttons
// refuse (measured), and the cut reached the cut list.
//
// Needs the encoder mode OFF (with it on every frame is a cut point).
// Drives the real window offscreen on an MPEG-2 stream, looks for a frame
// whose "Set cut-in" button is disabled and one whose "Set cut-out" button
// is disabled, then goes through the marker route.
//
//   usage: test_marker_cutpoint <mpeg2-es>
// Build via `cmake --build build --target test_marker_cutpoint`.
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

#include "common/ttsettings.h"
#include "gui/ttcutframenavigation.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttcuttreeview.h"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool waitForDirty(TTCutMainWindow& wnd, int timeoutMs)
{
  QElapsedTimer t; t.start();
  while (!wnd.windowTitle().endsWith("*") && t.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(20);
  }
  return wnd.windowTitle().endsWith("*");
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <mpeg2-es>\n", argv[0]);
    return 2;
  }
  const QString video = QString::fromUtf8(argv[1]);

  TTCutMainWindow mainWnd;
  mainWnd.show();
  QApplication::processEvents();

  QTimer::singleShot(0, &mainWnd, [&]() {
    mainWnd.onReadVideoStream(video);
    if (!waitForDirty(mainWnd, 60000)) {
      printf("FAIL: the opened video did not mark the project modified\n");
      failures++;
      qApp->exit(1);
      return;
    }
    TTSettings::instance()->setEncoderMode(false);
    pump(300);

    auto* nav      = mainWnd.findChild<TTCutFrameNavigation*>();
    auto* pbIn     = nav ? nav->findChild<QPushButton*>("pbSetCutIn") : nullptr;
    auto* pbOut    = nav ? nav->findChild<QPushButton*>("pbSetCutOut") : nullptr;
    auto* pbAdd    = nav ? nav->findChild<QPushButton*>("pbAddCut") : nullptr;
    auto* laIn     = nav ? nav->findChild<QLabel*>("laCutInPosition") : nullptr;
    auto* laOut    = nav ? nav->findChild<QLabel*>("laCutOutPosition") : nullptr;
    auto* treeView = mainWnd.findChild<TTCutTreeView*>();
    auto* tree     = treeView ? treeView->findChild<QTreeWidget*>() : nullptr;
    if (!nav || !pbIn || !pbOut || !pbAdd || !laIn || !laOut || !tree) {
      printf("FAIL: widgets not found\n");
      failures++;
      qApp->exit(1);
      return;
    }

    // A frame the cut-in button refuses, and a later one the cut-out button refuses.
    int inFrame = -1, outFrame = -1;
    for (int i = 1; i < 60 && inFrame < 0; ++i) {
      mainWnd.onStreamPointJump(i); pump(60);
      if (!pbIn->isEnabled()) inFrame = i;
    }
    for (int i = (inFrame < 0 ? 1 : inFrame + 20); i < 140 && outFrame < 0; ++i) {
      mainWnd.onStreamPointJump(i); pump(60);
      if (!pbOut->isEnabled()) outFrame = i;
    }
    printf("observed: encoderMode=%d refused cut-in frame=%d refused cut-out frame=%d\n",
           TTSettings::instance()->encoderMode() ? 1 : 0, inFrame, outFrame);
    if (inFrame < 0) {
      printf("FAIL: no frame with a disabled cut-in button found - the probe cannot say anything\n");
      failures++;
      qApp->exit(1);
      return;
    }
    // A stream without B-frames (the Tux MPEG-2 fixture is IPPP...) has no
    // frame the cut-out button refuses: the cut-out half then only closes the
    // range, and its check is skipped.
    const bool outRefused = (outFrame >= 0);
    if (!outRefused) outFrame = inFrame + 30;

    mainWnd.onStreamPointSetCutIn(inFrame); pump(100);
    printf("observed after marker cut-in at %d: button enabled=%d label=%s\n",
           inFrame, pbIn->isEnabled() ? 1 : 0, qPrintable(laIn->text()));
    const bool inTaken = laIn->text().contains(QString("(%1)").arg(inFrame));

    mainWnd.onStreamPointSetCutOut(outFrame); pump(100);
    printf("observed after marker cut-out at %d: button enabled=%d label=%s\n",
           outFrame, pbOut->isEnabled() ? 1 : 0, qPrintable(laOut->text()));
    const bool outTaken = laOut->text().contains(QString("(%1)").arg(outFrame));

    const int rowsBefore = tree->topLevelItemCount();
    nav->onAddCutRange(); pump(200);
    const int rowsAfter = tree->topLevelItemCount();
    QString row;
    if (rowsAfter > 0)
      for (int c = 0; c < tree->columnCount(); ++c)
        row += tree->topLevelItem(rowsAfter - 1)->text(c) + " | ";
    printf("observed: cut list rows %d -> %d, last row: %s\n", rowsBefore, rowsAfter, qPrintable(row));

    // Positive control, so that a slot refusing everything cannot pass: a
    // frame both buttons allow is taken for cut-in and cut-out, and the cut
    // reaches the list.
    int okIn = -1, okOut = -1;
    for (int i = 2; i < 140 && okIn < 0; ++i) {
      mainWnd.onStreamPointJump(i); pump(60);
      if (pbIn->isEnabled()) okIn = i;
    }
    for (int i = (okIn < 0 ? 2 : okIn + 20); i < 260 && okOut < 0; ++i) {
      mainWnd.onStreamPointJump(i); pump(60);
      if (pbOut->isEnabled()) okOut = i;
    }
    bool controlOk = false;
    int rowsControl = rowsAfter;
    if (okIn >= 0 && okOut >= 0) {
      mainWnd.onStreamPointSetCutIn(okIn); pump(100);
      const bool a = laIn->text().contains(QString("(%1)").arg(okIn));
      mainWnd.onStreamPointSetCutOut(okOut); pump(100);
      const bool b = laOut->text().contains(QString("(%1)").arg(okOut));
      nav->onAddCutRange(); pump(200);
      rowsControl = tree->topLevelItemCount();
      controlOk = a && b && rowsControl == rowsAfter + 1;
    }
    printf("observed control: allowed cut-in frame=%d cut-out frame=%d rows %d -> %d\n",
           okIn, okOut, rowsAfter, rowsControl);

    check(controlOk, "a marker sets cut-in and cut-out on frames the buttons allow, and the cut is added");
    check(!inTaken,  "a marker does not set a cut-in on a frame the cut-in button refuses");
    if (outRefused)
      check(!outTaken, "a marker does not set a cut-out on a frame the cut-out button refuses");
    else
      printf("SKIP: cut-out rule not testable, the stream has no frame the cut-out button refuses\n");
    check(rowsAfter == rowsBefore, "no cut with refused cut points reaches the cut list");
    qApp->exit(0);
  });

  app.exec();
  printf("%s\n", failures ? "MARKER-CUTPOINT FAIL" : "MARKER-CUTPOINT PASS");
  return failures ? 1 : 0;
}
