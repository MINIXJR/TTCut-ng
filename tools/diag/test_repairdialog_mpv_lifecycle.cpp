// Live-mpv proof for TTAudioRepairDialog's Play-button error handling
// (audio-anomaly-repair Task 7, review fix 2).
//
// Round 1 wired the "should we show a QMessageBox" gate to TTMpvWrapper::
// playerPlaying, which - verified by reading gui/ttmpvlibbackend.cpp - only
// fires ONCE per TTMpvWrapper instance (TTMpvLibBackend::start() is
// idempotent, the connected() handshake behind it fires via a one-shot
// QTimer::singleShot). A second Play click on the same dialog (e.g. "Play
// repaired" after "Play original") would never get that signal again.
//
// Round 2 switched to TTMpvWrapper::playbackRestarted (backed by mpv's
// MPV_EVENT_PLAYBACK_RESTART, which - per drainEvents() - fires once per
// load()/loadfile call). Getting this harness to actually prove that live
// took two more findings, both real, both worth keeping here:
//
// 1. A bare `while (elapsed<ms) qApp->processEvents();` pump (this
//    harness's first version, and the pattern most of this project's other
//    offscreen-safe harnesses use) hangs indefinitely driving mpv's real
//    render pipeline, on BOTH Xvfb and a real display - while
//    `mpv --vo=gpu --ao=null` on the exact same file, same DISPLAY, exits
//    cleanly in under a second (verified separately). tools/diag/
//    test_preview_then_cut.cpp's PREVIEW_FULL=1 path (the only other
//    harness here driving mpv playback for real) avoids this by using a
//    real nested QEventLoop instead of a pump for that part.
// 2. Adding mpv's QOpenGLWidget render widget to a dialog whose exec() loop
//    is ALREADY running terminates that loop: Qt recreates the top-level's
//    native window to get a GL-compatible surface, and that recreation
//    calls hide() as an internal step (immediately followed by a re-show) -
//    but QDialogPrivate::hide_helper() unconditionally exits exec()'s
//    private QEventLoop on ANY hide(), so exec() returns control to the
//    caller mid-click while the window is still on screen. Reproduced
//    identically under Xvfb (bare and QT_QPA_PLATFORM=xcb forced, ruling
//    out a Wayland-leak-through-xvfb-run theory), on the real live Wayland
//    session (ruling out a missing-window-manager theory), and with both a
//    null and a real shown parent widget (ruling out a parenting theory) -
//    so this is QOpenGLWidget + already-running QDialog::exec(), not an
//    environment artifact. A standalone Qt probe (plain QDialog + a
//    QOpenGLWidget added from a click handler, no TTCut/mpv at all)
//    reproduces it too.
//
//    Round 2 wrongly filed this as a harness-only problem and worked around
//    it with dlg.show() + QApplication::exec(). It is a PRODUCTION bug: the
//    real call site (TTStreamPointWidget::onContextMenu) runs the dialog
//    with a stack-allocated `dlg.exec()`, so the first Play click ended the
//    exec() loop with Rejected and destroyed the dialog mid-load. The fix
//    (final-review Critical 1) is in the dialog: TTAudioRepairDialog now
//    builds its TTMpvWrapper/render widget in its own CONSTRUCTOR, before
//    any show()/exec(), exactly like TTCutPreview - except under
//    QT_QPA_PLATFORM=offscreen, where mpv gets no GL context and the model
//    harness would hang. This harness therefore drives dlg.exec() again -
//    that IS the regression check: if exec() returns before step 4 below,
//    the harness fails.
//
// Sequence, all against ONE TTAudioRepairDialog/mPlayer instance, driven by
// QTimer::singleShot callbacks fired while dlg.exec() runs:
//   1. Play 1 - a real, playable AC3 file.
//   2. Play 2 - the SAME dialog/mPlayer, another real load (this is
//      exactly what playerPlaying could not confirm twice).
//   3. Play 3 - a nonexistent path (playFileForTest() bypasses
//      writePreviewWindow()'s own pre-check, so this reaches mpv itself).
//
// NOT offscreen, for the same GL-context reason as test_mainwindow_then_cut
// /test_preview_then_cut. Run under a real or virtual (Xvfb) X11/Wayland
// display:
//
//   LC_NUMERIC=C xvfb-run -a ./tools/diag/test_repairdialog_mpv_lifecycle
//
// (LC_NUMERIC is also forced in main(), see the std::setlocale call - the
// env prefix just documents the requirement at the call site.)
//
// Read the app log (TTMessageLogger's logfile, see common/ttmessagelogger.h
// - typically ~/.cache/ttcut-ng/logfile.log) afterwards for the dialog's own
// "Repair preview: ..." lines - that transcript, not this program's exit
// code, is the actual evidence (see task-7-report.md's Fix 2 round 2
// verification section).
//
//   usage: test_repairdialog_mpv_lifecycle
//
// Build via `cmake --build build --target test_repairdialog_mpv_lifecycle`.
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QWidget>

#include <cstdio>
#include <clocale>

#include "data/ttstreampoint.h"
#include "gui/ttaudiorepairdialog.h"
#include "common/ttsettings.h"

static const QString kAudioFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3");
static const QString kNonexistentFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test_DOES_NOT_EXIST_xyz123.ac3");

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
        fprintf(stderr, "REFUSING: mpv needs a real GL-capable platform - "
                        "offscreen gives it none (see file header). Use a "
                        "real display or xvfb-run.\n");
        return 2;
    }

    QApplication app(argc, argv);
    // libmpv requires LC_NUMERIC=C (see gui/ttcutmain.cpp's identical call
    // and its comment) - QApplication's construction applies the system
    // locale (de_DE here), which makes mpv_create() return NULL. Without
    // this the harness would "prove" every Play attempt fails, which is an
    // environment artifact, not the dialog logic under test.
    std::setlocale(LC_NUMERIC, "C");

    // The dialog's per-step "Repair preview: ..." trace lines (the actual
    // evidence this harness exists to produce) are gated behind logUI() -
    // in-memory only, TTSettings::save() is never called here, so this does
    // not touch the user's persisted TTCut-ng.conf.
    TTSettings::instance()->setLogUI(true);

    if (!QFileInfo::exists(kAudioFile)) {
        fprintf(stderr, "missing fixture: %s\n", qPrintable(kAudioFile));
        return 2;
    }
    if (QFileInfo::exists(kNonexistentFile)) {
        fprintf(stderr, "fixture collision: %s unexpectedly exists\n", qPrintable(kNonexistentFile));
        return 2;
    }

    // Real production call site (TTStreamPointWidget::showContextMenu) parents
    // the dialog to a real, already-shown widget inside the main window -
    // never nullptr. Mirrored here even though it turned out not to be the
    // deciding factor for the exec()-returns-early finding above (see
    // header) - it is still the more faithful setup.
    QWidget parentWin;
    parentWin.resize(400, 300);
    parentWin.show();

    const TTStreamPoint point; // irrelevant here - only playFileForTest() is exercised
    TTAudioRepairDialog dlg(nullptr, point, 0, QList<int>(), &parentWin);

    // Driver: dismiss any modal QMessageBox that appears while the app's
    // event loop runs - same pattern as test_mainwindow_then_cut.cpp/
    // test_preview_then_cut.cpp. HOW it is dismissed is not under test here.
    QTimer driver;
    driver.setInterval(100);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(qApp->activeModalWidget())) {
            printf("  (dismissing popup: %s)\n", qPrintable(box->text().left(80)));
            QAbstractButton* btn = box->defaultButton();
            if (!btn && !box->buttons().isEmpty()) btn = box->buttons().first();
            if (btn) btn->click(); else box->accept();
        }
    });
    driver.start();

    const int stepMs = 3500;
    QPointer<TTAudioRepairDialog> guard(&dlg);
    // Counts the steps that actually ran INSIDE the exec() loop. The whole
    // point of the Critical-1 regression check: exec() must still be running
    // when step 4 arrives. A premature return (the old bug) leaves this at 0
    // or 1 and fails the harness below.
    int stepsReached = 0;
    QTimer::singleShot(300, &dlg, [&, guard] {
        if (!guard) return;
        stepsReached = 1;
        printf("=== PLAY 1 (real file) ===\n");
        dlg.playFileForTest(kAudioFile);
    });
    QTimer::singleShot(300 + stepMs, &dlg, [&, guard] {
        if (!guard) return;
        stepsReached = 2;
        printf("=== PLAY 2 (same dialog/mPlayer, real file again) ===\n");
        dlg.playFileForTest(kAudioFile);
    });
    QTimer::singleShot(300 + 2 * stepMs, &dlg, [&, guard] {
        if (!guard) return;
        stepsReached = 3;
        printf("=== PLAY 3 (provoked load failure - nonexistent file) ===\n");
        dlg.playFileForTest(kNonexistentFile);
    });
    QTimer::singleShot(300 + 3 * stepMs, &dlg, [&, guard] {
        stepsReached = 4;
        printf("=== all three Play steps done, closing the dialog ===\n");
        if (guard) guard->reject();   // ends dlg.exec() the regular way
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        printf("  WATCHDOG: dialog did not close in time, forcing it\n");
        if (guard) guard->reject();
        qApp->quit();
    });
    watchdog.start(300 + 3 * stepMs + 15000);

    // dlg.exec() - the production call shape (TTStreamPointWidget::
    // onContextMenu). Before the Critical-1 fix this returned during PLAY 1.
    const int rc = dlg.exec();
    watchdog.stop();
    printf("=== dlg.exec() returned (rc=%d) after step %d ===\n", rc, stepsReached);

    if (stepsReached < 4) {
        printf("\nFAILED: dlg.exec() returned after step %d instead of surviving "
               "all three Play clicks - the QOpenGLWidget-inside-a-running-exec() "
               "regression is back (see this file's header, finding 2)\n",
               stepsReached);
        return 1;
    }
    printf("\nALL PASS (exec() survived 3 Play clicks) - see the app log for the "
           "literal 'Repair preview: ...' lines\n");
    return 0;
}
