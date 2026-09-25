// Gate for the cut-preview dialog's clip bookkeeping (audit run 9,
// docs/code-map/cut-preview.md H1, H3, H4, H8, H9), on the make_aspect_m2v
// fixture: MPEG-2 4:3 with a 16:9 run [A, B], no audio.
//
// Three cuts; only the middle one starts in 4:3 before its 16:9 majority, so
// it alone carries an aspect hint at its cut-in. The preview is the one the
// cut list builds for "the middle cut with its neighbours": skipFirst and
// skipLast set, so the dialog's first combo entry is clip 1, not clip 0.
//
//   H1  the hint appears at the transition "Cut 1-2" (where cut 2 starts),
//       not at "Cut 2-3"
//   H3  the jump to the aspect target, which lies beyond the preview window,
//       leaves a proper window (start <= end) around the new edge
//   H8  the rebuild does not throw; a failing MPEG-2 rebuild returns false
//   H9  the rebuilt clip replaces preview_002.mkv (no audio: rename path)
//   H4  the rebuild writes preview_002.srt again
//   and the jump moves the right cut in the model (cut 2's cut-in to A).
//
//   usage: test_preview_clip_index <aspect.m2v> <A> <B> <workdir>
//
// Build via `cmake --build build --target test_preview_clip_index`.
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "common/ttexception.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "data/ttcutpreviewtask.h"
#include "data/ttpreviewclip.h"
#include "gui/ttcutpreview.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) gFailures++;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 5) { fprintf(stderr, "usage: %s <aspect.m2v> <A> <B> <workdir>\n", argv[0]); return 2; }
  const QString m2v  = QString::fromLocal8Bit(argv[1]);
  const int     A    = atoi(argv[2]);
  const int     B    = atoi(argv[3]);
  const QString work = QString::fromLocal8Bit(argv[4]);
  const QString tmp  = work + "/tmp";
  QDir().mkpath(tmp);
  TTSettings::instance()->setTempDirPath(tmp);
  TTSettings::instance()->setCutPreviewSeconds(2);   // 25 frames per side at 25 fps

  // A subtitle next to the video, so the preview cuts one per clip.
  QFile srt(QFileInfo(m2v).absolutePath() + "/" + QFileInfo(m2v).completeBaseName() + ".srt");
  if (srt.open(QIODevice::WriteOnly | QIODevice::Truncate))
    QTextStream(&srt) << "1\n00:00:00,000 --> 00:00:12,000\nSUB\n\n";
  srt.close();

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

  const int cuts[3][2] = {{0, 60}, {A - 40, B}, {B + 30, B + 90}};
  TTCutList job;
  for (const auto& c : cuts) { av.appendCutEntry(item, c[0], c[1]); job.append(item, c[0], c[1]); }

  TTCutList* preview = nullptr;
  { QEventLoop l;
    QObject::connect(&av, &TTAVData::cutPreviewFinished, [&](TTCutList* p) { preview = p; l.quit(); });
    QTimer::singleShot(60000, &l, &QEventLoop::quit); av.doCutPreview(&job); l.exec(); }
  check(preview != nullptr && preview->count() == 6, "preview built (6 entries)");
  if (!preview || preview->count() != 6) return 1;

  TTCutPreview dlg(nullptr);
  dlg.initPreview(preview, &job, &av, /*skipFirst=*/true, /*skipLast=*/true);
  auto* combo  = dlg.findChild<QComboBox*>("cbCutPreview");
  auto* aspect = dlg.findChild<QLabel*>("lblAspectWarning");
  auto* jump   = dlg.findChild<QPushButton*>("pbAspectJump");
  if (!combo || !aspect || !jump) { check(false, "dialog widgets found"); return 1; }
  check(combo->count() == 2, QString("two combo entries (got %1)").arg(combo->count()));

  // --- H1 --------------------------------------------------------------------
  combo->setCurrentIndex(1);
  const bool shownAt23 = !aspect->isHidden();
  printf("  'Cut 2-3': %s\n", shownAt23 ? qPrintable(aspect->text()) : "(no hint)");
  combo->setCurrentIndex(0);
  const bool shownAt12 = !aspect->isHidden();
  printf("  'Cut 1-2': %s\n", shownAt12 ? qPrintable(aspect->text()) : "(no hint)");
  check(shownAt12 && aspect->text().contains("Cut 2 starts"), "H1: hint at 'Cut 1-2', naming cut 2");
  check(!shownAt23, "H1: no hint at 'Cut 2-3'");

  // --- jump: H3, H4, H8, H9 ----------------------------------------------------
  const QString mkv2 = TTCutPreviewTask::createPreviewFileName(2, "mkv");
  const QString srt2 = TTCutPreviewTask::createPreviewFileName(2, "srt");
  const QString m2v2 = TTCutPreviewTask::createPreviewFileName(2, "m2v");
  const QDateTime mkvBefore = QFileInfo(mkv2).lastModified();
  const QDateTime srtBefore = QFileInfo(srt2).lastModified();
  QThread::msleep(1100);   // file times have one-second resolution on some file systems

  bool threw = false;
  try { if (!jump->isHidden()) jump->click(); } catch (...) { threw = true; }
  check(!threw, "H8: the jump does not throw");
  check(boxes.isEmpty(), QString("no warning box (%1)").arg(boxes.join(" | ")));
  check(av.cutItemAt(1).cutInIndex() == A,
        QString("model: cut 2 starts at %1 (got %2)").arg(A).arg(av.cutItemAt(1).cutInIndex()));
  check(av.cutItemAt(0).cutInIndex() == 0 && av.cutItemAt(2).cutInIndex() == B + 30,
        "model: the neighbours are untouched");
  const TTCutItem moved = preview->at(2);
  check(moved.cutInIndex() == A && moved.cutInIndex() <= moved.cutOutIndex(),
        QString("H3: preview window around the new edge is %1-%2").arg(moved.cutInIndex()).arg(moved.cutOutIndex()));
  check(QFileInfo(mkv2).lastModified() > mkvBefore, "H9: preview_002.mkv rewritten");
  check(!QFileInfo::exists(m2v2), "H9: no preview_002.m2v left behind");
  check(QFileInfo(srt2).lastModified() > srtBefore, "H4: preview_002.srt rewritten");

  // --- H8, directly: a clip whose range is turned around ------------------------
  TTCutList inverted;
  inverted.append(item, 150, 100);
  bool rebuilt = true;
  threw = false;
  try { rebuilt = ttRebuildMpeg2PreviewClip(&av, &inverted, 9, {}); } catch (...) { threw = true; }
  check(!threw && !rebuilt, "H8: a failing MPEG-2 rebuild returns false instead of throwing");

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
