// Gate: a preview window never leaves its cut (docs/code-map/cut-preview.md,
// found in the sight test of 2026-09-25). The windows were clamped to the
// stream only, so with a preview longer than a cut the clip played the
// material behind the cut and the combo showed "00:00:00 - 00:00:11" for
// every transition of a 12 s test picture.
//
// make_aspect_m2v fixture (12 s MPEG-2, 16:9 run [A, B], no audio), preview
// length 25 s (the user's setting: 312 frames per side, longer than every
// cut), three cuts; the preview of the middle cut with its neighbours.
//
//   - every preview entry lies inside its cut
//   - the combo shows the cuts a clip joins: cut-in of the first to
//     cut-out of the second, as in the cut list
//   - the combo tooltip shows the pre-/post-roll the clip really plays
//   - preview_002.mkv ("Cut 1-2") has cut 1 + cut 2 frames, nothing more
//   - after the aspect jump (cut 2 starts at A) both windows of cut 2 lie
//     inside the moved cut and the combo text follows
//
//   usage: test_preview_window_in_cut <aspect.m2v> <A> <B> <workdir>
//
// Build via `cmake --build build --target test_preview_window_in_cut`.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLocale>
#include <QEventLoop>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "data/ttcutpreviewtask.h"
#include "gui/ttcutpreview.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) gFailures++;
}

static int videoFrames(const QString& file)
{
  QProcess p;
  p.start("ffprobe", {"-v", "error", "-count_frames", "-select_streams", "v:0",
                      "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", file});
  if (!p.waitForFinished(60000)) return -1;
  bool ok = false;
  const int n = QString::fromLatin1(p.readAllStandardOutput()).trimmed().remove(',').toInt(&ok);
  return ok ? n : -1;
}

// Every preview entry pair (2k, 2k+1) inside cut k of the job list.
static bool entriesInsideCuts(TTCutList* preview, TTAVData& av, QString& detail)
{
  bool ok = true;
  for (int i = 0; i < preview->count(); i++) {
    const TTCutItem cut = av.cutItemAt(i / 2);
    const TTCutItem e   = preview->at(i);
    detail += QString(" %1-%2").arg(e.cutInIndex()).arg(e.cutOutIndex());
    ok &= e.cutInIndex() >= cut.cutInIndex() && e.cutOutIndex() <= cut.cutOutIndex()
          && e.cutInIndex() <= e.cutOutIndex();
  }
  return ok;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 5) { fprintf(stderr, "usage: %s <aspect.m2v> <A> <B> <workdir>\n", argv[0]); return 2; }
  const QString m2v  = QString::fromLocal8Bit(argv[1]);
  const int     A    = atoi(argv[2]);
  const int     B    = atoi(argv[3]);
  const QString tmp  = QString::fromLocal8Bit(argv[4]) + "/tmp";
  QDir().mkpath(tmp);
  TTSettings::instance()->setTempDirPath(tmp);
  TTSettings::instance()->setCutPreviewSeconds(25);

  QStringList boxes;
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, [&]() {
    if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      boxes << mb->text();
      mb->done(QMessageBox::Ok);
    }
  });
  driver.start(100);

  TTAVData av;
  { QEventLoop l; QObject::connect(&av, &TTAVData::threadPoolExit, &l, &QEventLoop::quit);
    QTimer::singleShot(30000, &l, &QEventLoop::quit); av.openAVStreams(m2v); l.exec(); }
  if (av.avCount() != 1) { check(false, "fixture opened"); return 1; }
  TTAVItem* item = av.avItemAt(0);
  TTVideoStream* vs = item->videoStream();

  const int cuts[3][2] = {{0, 60}, {A - 40, B}, {B + 30, B + 90}};
  TTCutList job;
  for (const auto& c : cuts) { av.appendCutEntry(item, c[0], c[1]); job.append(item, c[0], c[1]); }

  TTCutList* preview = nullptr;
  { QEventLoop l;
    QObject::connect(&av, &TTAVData::cutPreviewFinished, [&](TTCutList* p) { preview = p; l.quit(); });
    QTimer::singleShot(60000, &l, &QEventLoop::quit); av.doCutPreview(&job); l.exec(); }
  check(preview != nullptr && preview->count() == 6, "preview built (6 entries)");
  if (!preview || preview->count() != 6) return 1;

  QString detail;
  bool inside = entriesInsideCuts(preview, av, detail);
  check(inside, "every window inside its cut:" + detail);

  TTCutPreview dlg(nullptr);
  dlg.initPreview(preview, &job, &av, /*skipFirst=*/true, /*skipLast=*/true);
  auto* combo = dlg.findChild<QComboBox*>("cbCutPreview");
  auto* jump  = dlg.findChild<QPushButton*>("pbAspectJump");
  if (!combo || !jump || combo->count() != 2) { check(false, "dialog widgets and two entries"); return 1; }
  // first line of the current entry's tooltip, on the combo and on the entry
  auto roll = [&]() { return combo->toolTip().section('\n', 0, 0); };
  auto entryRoll = [&](int i) { return combo->itemData(i, Qt::ToolTipRole).toString().section('\n', 0, 0); };
  const double fps = vs->frameRate();
  auto rollText = [&](int preFrames, int postFrames) {
    return QString("Pre-roll %1 s \u00b7 Post-roll %2 s")
        .arg(QLocale().toString(preFrames / fps, 'f', 1), QLocale().toString(postFrames / fps, 'f', 1));
  };

  auto hms = [&](int frame) { return vs->frameTime(frame).toString("hh:mm:ss"); };
  const QString want12 = QString("Cut 1-2: %1 - %2").arg(hms(cuts[0][0])).arg(hms(cuts[1][1]));
  const QString want23 = QString("Cut 2-3: %1 - %2").arg(hms(cuts[1][0])).arg(hms(cuts[2][1]));
  check(combo->itemText(0) == want12, QString("combo 0 '%1' (want '%2')").arg(combo->itemText(0), want12));
  check(combo->itemText(1) == want23, QString("combo 1 '%1' (want '%2')").arg(combo->itemText(1), want23));

  const QString wantRoll = rollText(cuts[0][1] - cuts[0][0] + 1, cuts[1][1] - cuts[1][0] + 1);
  check(roll() == wantRoll && entryRoll(0) == wantRoll,
        QString("roll tooltip '%1' / entry '%2' (want '%3')").arg(roll(), entryRoll(0), wantRoll));
  const QString wantRoll23 = rollText(cuts[1][1] - cuts[1][0] + 1, cuts[2][1] - cuts[2][0] + 1);
  check(entryRoll(1) == wantRoll23, QString("entry 1 roll '%1' (want '%2')").arg(entryRoll(1), wantRoll23));

  const int cut12 = (cuts[0][1] - cuts[0][0] + 1) + (cuts[1][1] - cuts[1][0] + 1);
  const int got12 = videoFrames(TTCutPreviewTask::createPreviewFileName(2, "mkv"));
  check(got12 == cut12, QString("preview_002.mkv has %1 frames (cut 1 + cut 2 = %2)").arg(got12).arg(cut12));

  // The aspect jump moves cut 2's cut-in to A; both windows of cut 2 follow.
  // select "Cut 1-2" through a real index change: the aspect check runs on it
  combo->setCurrentIndex(1);
  combo->setCurrentIndex(0);
  check(!jump->isHidden(), "aspect jump offered at 'Cut 1-2'");
  if (!jump->isHidden()) jump->click();
  check(boxes.isEmpty(), QString("no warning box (%1)").arg(boxes.join(" | ")));
  check(av.cutItemAt(1).cutInIndex() == A, QString("cut 2 starts at %1").arg(av.cutItemAt(1).cutInIndex()));
  detail.clear();
  inside = entriesInsideCuts(preview, av, detail);
  check(inside, "after the jump, every window inside its cut:" + detail);
  const QString want23b = QString("Cut 2-3: %1 - %2").arg(hms(A)).arg(hms(cuts[2][1]));
  check(combo->itemText(1) == want23b, QString("combo 1 after the jump '%1' (want '%2')").arg(combo->itemText(1), want23b));
  const QString wantRollB = rollText(cuts[0][1] - cuts[0][0] + 1, B - A + 1);
  check(roll() == wantRollB, QString("roll tooltip after the jump '%1' (want '%2')").arg(roll(), wantRollB));
  const int cut12b = (cuts[0][1] - cuts[0][0] + 1) + (B - A + 1);
  const int got12b = videoFrames(TTCutPreviewTask::createPreviewFileName(2, "mkv"));
  check(got12b == cut12b, QString("rebuilt preview_002.mkv has %1 frames (want %2)").arg(got12b).arg(cut12b));

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
