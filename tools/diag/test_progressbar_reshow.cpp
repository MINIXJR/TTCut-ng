// Harness for TTProgressBar's dialog re-show guard.
//
// The guard exists because closing the progress dialog (Cancel button or the
// window's X) always calls hideBar(), whether or not the cancel had any
// effect, while TTCutMainWindow leaves the main window DISABLED until the
// operation's Exit/Canceled bracket arrives. A phase that keeps running after
// the click therefore leaves the user with no dialog and a dead-looking main
// window. The guard brings the dialog back on the next progress message.
//
// It used to sit only in the `Step` branch of onSetProgress(). mplex reports
// exclusively through `AddProcessLine` (ShowProcessForm/AddProcessLine/
// HideProcessForm, never Step -- extern/ttmplexprovider.cpp), so a cancel
// during the mplex step of an MPG cut hid the dialog for the rest of the run.
//
// No video material and no cut are involved: the dialog is driven directly
// through its own status sink, which is exactly the interface
// TTCutMainWindow connects TTAVData::statusReport to.
//
// Usage: test_progressbar_reshow           (no arguments)
//
// Non-vacuity: case 2 FAILS against the pre-fix ttprogressbar.cpp (the
// AddProcessLine branch only called appendDetailLine()); case 1 passes there
// too and is the control that proves the harness's own mechanics -- create,
// close, feed, check -- are sound.
#include <QApplication>

#include <cstdio>

#include "common/istatusreporter.h"
#include "gui/ttprogressbar.h"

namespace {

int fail(const char* what)
{
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

// Bring a fresh dialog into the state a running operation leaves it in:
// visible, not finished.
void startOperation(TTProgressBar& bar)
{
  bar.onSetProgress(0, StatusReportArgs::Init, "Initializing MPEG-2 cut...", 0);
  bar.showBar();
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  // --- case 1: Step branch (pre-existing guard) -- control ------------------
  {
    TTProgressBar bar;
    startOperation(bar);
    if (!bar.isVisible()) return fail("case 1: dialog not visible after showBar()");

    bar.onBtnCancelClicked();                       // user closes it mid-run
    if (bar.isVisible())  return fail("case 1: Cancel did not hide the dialog");

    bar.onSetProgress(0, StatusReportArgs::Step, "Cut 1 of 3", 10);
    if (!bar.isVisible()) return fail("case 1: a Step did not bring the dialog back");
    printf("  case 1 (Step)            OK\n");
  }

  // --- case 2: AddProcessLine branch -- the mplex-shaped gap ----------------
  {
    TTProgressBar bar;
    startOperation(bar);
    bar.onBtnCancelClicked();
    if (bar.isVisible())  return fail("case 2: Cancel did not hide the dialog");

    // Exactly what TTMplexProvider emits while the external muxer runs.
    bar.onSetProgress(0, StatusReportArgs::AddProcessLine,
                      "   INFO: [mplex] Multiplexing video program stream!", 0);
    if (!bar.isVisible())
      return fail("case 2: an AddProcessLine did not bring the dialog back");
    printf("  case 2 (AddProcessLine)  OK\n");
  }

  // --- case 3: a FINISHED, user-closed dialog must stay closed --------------
  // The guard must not re-open a dialog the user closed after the operation
  // ended. enterFinishedState() sets mFinished before hideBar(), so both
  // branches are inert afterwards; asserted here because case 2 widened the
  // set of messages that can re-show it.
  {
    TTProgressBar bar;
    startOperation(bar);
    bar.onSetProgress(0, StatusReportArgs::Exit, "Cut complete", 100);
    bar.onBtnCancelClicked();                       // now the Close button
    if (bar.isVisible())  return fail("case 3: Close did not hide the finished dialog");

    bar.onSetProgress(0, StatusReportArgs::AddProcessLine, "trailing process line", 0);
    if (bar.isVisible())
      return fail("case 3: an AddProcessLine re-opened a finished dialog");
    bar.onSetProgress(0, StatusReportArgs::Step, "trailing step", 100);
    if (bar.isVisible())
      return fail("case 3: a Step re-opened a finished dialog");
    printf("  case 3 (finished dialog) OK\n");
  }

  printf("PASS\n");
  return 0;
}
