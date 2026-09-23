// Gate for re-encoded frames the H.264 SPS unification could not adjust:
// ttRewriteEncoderPacketForSourceSps must count such slices, and
// TTAVData::confirmUnrewrittenFrames must list the frames, copy them to the
// clipboard without closing, return keep/discard as clicked, and not show a
// dialog under --auto-cut (setNonInteractive).
//   QT_QPA_PLATFORM=offscreen test_unrewritten_frames   exit 0 = PASS
#include <cstdio>
#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include "avstream/ttbitstream.h"
#include "data/ttavdata.h"
#include "extern/tth264bitstream.h"

static int gFailures = 0;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

// P slice whose header ends inside the MMCO list: the rewrite rejects it.
static QByteArray truncatedSlice()
{
    TTBitWriter w;
    w.bits(0x41, 8); w.ue(0); w.ue(5); w.ue(0);
    w.bits(3, 4);             // frame_num
    w.bits(10, 6);            // pic_order_cnt_lsb
    w.bits(0, 1); w.bits(0, 1);
    w.bits(1, 1); w.ue(1);    // adaptive flag, MMCO 1 - then the data ends
    return ttNalFromRbsp(w.data());
}

static QPushButton* buttonWithText(QMessageBox* box, const QString& text)
{
    for (QAbstractButton* b : box->buttons())
        if (b->text() == text) return qobject_cast<QPushButton*>(b);
    return nullptr;
}

// Runs confirmUnrewrittenFrames with `act` operating on the dialog once it
// is up; returns the function's result.
static bool runDialog(TTAVData& av, const QList<int>& frames,
                      const std::function<void(QMessageBox*)>& act)
{
    QTimer poll;
    bool acted = false;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box || acted) return;
        acted = true;
        act(box);
    });
    poll.start(20);
    const bool keep = av.confirmUnrewrittenFrames(frames, 25.0);
    poll.stop();
    return keep;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // 1. The packet rewrite counts the slice it could not rewrite.
    {
        const TTH264PpsInfo pps = { false, false, true, false, false, 0, 0, 0, true };
        const QByteArray packet = QByteArray("\x00\x00\x00\x01", 4) + truncatedSlice();
        int failed = 0;
        ttRewriteEncoderPacketForSourceSps(packet, 4, 6, true, 6, 8, true, pps, 1, 3, 0, &failed);
        check(failed == 1, "ttRewriteEncoderPacketForSourceSps counts the unrewritable slice");
    }

    TTAVData av;
    const QList<int> frames = { 1234, -1 };

    // 2. Text, clipboard (dialog stays open), "Keep result".
    bool copiedAndOpen = false, textOk = false;
    const bool kept = runDialog(av, frames, [&](QMessageBox* box) {
        textOk = box->text().contains("source frame 1234 (00:00:49.360)")
                 && box->text().contains("frame at an unknown position");
        buttonWithText(box, "Copy to clipboard")->click();
        const QString clip = QApplication::clipboard()->text();
        copiedAndOpen = box->isVisible()
                        && clip.contains("source frame 1234 (00:00:49.360)")
                        && clip.contains("frame at an unknown position");
        buttonWithText(box, "Keep result")->click();
    });
    check(textOk, "dialog lists the frame with its source time and the unknown one");
    check(copiedAndOpen, "copy puts the list on the clipboard and keeps the dialog open");
    check(kept, "\"Keep result\" returns true");

    // 3. "Discard".
    const bool keptAfterDiscard = runDialog(av, frames, [](QMessageBox* box) {
        buttonWithText(box, "Discard")->click();
    });
    check(!keptAfterDiscard, "\"Discard\" returns false");

    // 4. --auto-cut: no dialog, result kept.
    av.setNonInteractive(true);
    bool dialogShown = false;
    const bool keptAuto = runDialog(av, frames, [&](QMessageBox* box) {
        dialogShown = true;
        box->reject();
    });
    check(keptAuto && !dialogShown, "non-interactive: no dialog, result kept");

    // 5. Nothing to report: no dialog.
    av.setNonInteractive(false);
    bool shownForEmpty = false;
    const bool keptEmpty = runDialog(av, {}, [&](QMessageBox* box) {
        shownForEmpty = true;
        box->reject();
    });
    check(keptEmpty && !shownForEmpty, "empty list: no dialog");

    printf("%s\n", gFailures ? "UNREWRITTEN-FRAMES FAIL" : "UNREWRITTEN-FRAMES PASS");
    return gFailures ? 1 : 0;
}
