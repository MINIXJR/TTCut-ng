// Gate for audit run 6, batch A: TTAVItem::checkCut validates a cut range.
//
// Before this batch checkCut() was an empty shell (its only test commented
// out with a TODO), so an inverted or negative range reached the cut lists
// and the engines: a probe appending (500,100) got an entry reporting 401
// frames of length. The one route that never passes a GUI navigator is the
// project file - TTCutProjectData::parseCutSection calls
// TTAVItem::appendCutEntry directly - so the check has to sit in checkCut,
// and the loader has to survive a rejected entry instead of aborting.
//
// The upper bound (cutOut >= frameCount) only applies with an open stream;
// a project file is parsed before the open task runs. That case is covered
// by the negative/inverted tests, not by a frame count.
//
// It also covers the sibling guard batch F added: TTCutList::remove used
// data.takeAt(data.indexOf(item)) without checking for -1, while update()
// and onUpdateOrder() always checked. No route into it is known; the gate
// pins the hardening.
//
// Material-free (no video is opened), offscreen, no dialogs.
//   usage: test_cut_range_check <workdir>
// Build via `cmake --build build --target test_cut_range_check`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <cstdio>

#include "common/ttexception.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"

namespace {

int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// True when appending this range raised TTInvalidOperationException.
bool rejects(TTAVItem& item, int cutIn, int cutOut)
{
  try { item.appendCutEntry(cutIn, cutOut); }
  catch (const TTInvalidOperationException&) { return true; }
  return false;
}

bool writeFile(const QString& path, const QByteArray& content)
{
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(content);
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
    return 2;
  }
  const QDir work(QString::fromUtf8(argv[1]));
  QDir().mkpath(work.absolutePath());

  {
    TTAVItem item(0);
    check(rejects(item, 500, 100),   "an inverted range is rejected");
    check(rejects(item, -7, 100),    "a negative cut-in is rejected");
    check(rejects(item, 10, -1),     "a negative cut-out is rejected");
    check(!rejects(item, 100, 500),  "a normal range is accepted");
    check(!rejects(item, 42, 42),    "a single-frame range is accepted");
    check(item.cutCount() == 2,      "only the two accepted ranges are in the list");
  }

  // TTAVData::appendCutEntry walks the whole AV list, so with a single video
  // loaded an item is checked against itself. canCutWith must return at once
  // then: it compares TWO videos, and running it on one file would pit two
  // positions of that file against each other - which fails as soon as the
  // aspect ratio changes somewhere in the recording, as it does in most DVB
  // captures. Also covers the null stream: everything below the early return
  // dereferences videoStream().
  {
    TTAVItem item(0);
    item.appendCutEntry(100, 200);
    bool threw = false;
    try { item.canCutWith(&item, 300, 400); }
    catch (const TTInvalidOperationException&) { threw = true; }
    check(!threw, "an item checked against itself raises nothing");
  }

  // Changing an existing range is validated too, but refused rather than
  // thrown: the six callers are Qt slots.
  {
    TTAVItem item(0);
    item.appendCutEntry(100, 500);
    const TTCutItem entry = item.cutListItemAt(0);

    item.updateCutEntry(entry, 100, 50);     // would invert the range
    check(item.cutListItemAt(0).cutOutIndex() == 500,
          "an update that would invert the range is refused");

    item.updateCutEntry(entry, -3, 500);     // negative cut-in
    check(item.cutListItemAt(0).cutInIndex() == 100,
          "an update with a negative position is refused");

    item.updateCutEntry(entry, 100, 400);    // sound
    check(item.cutListItemAt(0).cutOutIndex() == 400,
          "a sound update still goes through");
  }

  // Removing an entry that belongs to another item must not take at -1.
  {
    TTAVItem a(0), b(0);
    a.appendCutEntry(10, 20);
    b.appendCutEntry(30, 40);
    a.removeCutEntry(b.cutListItemAt(0));
    check(a.cutCount() == 1, "removing a foreign entry leaves the list alone");
    a.removeCutEntry(a.cutListItemAt(0));
    check(a.cutCount() == 0, "removing an entry of its own list still works");
  }

  // The project loader must skip a rejected entry and still finish the load.
  {
    const QString prj = work.absoluteFilePath("bad-cut.ttcut");
    if (!writeFile(prj,
            "<TTCut-Projectfile><Version>1.0</Version>"
            "<Video><Order>0</Order><Name>/nonexistent-ttcut-gate/video.m2v</Name>"
            "<Cut><Order>0</Order><CutIn>500</CutIn><CutOut>100</CutOut></Cut>"
            "<Cut><Order>1</Order><CutIn>100</CutIn><CutOut>500</CutOut></Cut>"
            "</Video></TTCut-Projectfile>\n")) {
      fprintf(stderr, "cannot write %s\n", qPrintable(prj));
      return 2;
    }

    TTAVData avData;
    avData.setNonInteractive(true);
    QEventLoop loop;
    char outcome = '-';
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                     [&](const QString&) { outcome = 'f'; loop.quit(); });
    QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                     [&]() { outcome = 'a'; loop.quit(); });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(prj));
    if (outcome == '-') loop.exec();

    check(outcome != '-', "a project with a rejected cut range still ends the load");
    check(avData.cutCount() == 1,
          "the inverted entry is skipped, the sound one is kept");
  }

  printf("%s\n", failures ? "CUT-RANGE-CHECK FAIL" : "CUT-RANGE-CHECK PASS");
  return failures ? 1 : 0;
}
