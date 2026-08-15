// What does the equal-frame search actually report, and when?
//
// TODO.md claimed "1 Start and 3 Steps for a whole search" for TTFrameSearchTask
// and concluded the coordinator contributes nothing to the progress bar. The
// code says otherwise: data/ttframesearchtask.cpp emits a Step per compared
// frame inside the loop, and the search window is TTSettings::searchLength()
// seconds (45 s by default) - over a thousand frames at 25 fps. Either the
// measured run ended after three iterations, or most iterations took one of
// the two `continue` branches (decode failure, size mismatch) that skip the
// Step. This probe settles it with numbers instead of readings.
//
// It runs a real TTFrameSearchTask synchronously on an elementary stream and
// timestamps every status report. The output shows three things:
//   - how many reports of each kind actually arrive
//   - the largest silent gaps, i.e. where the bar stands still
//   - how the silence splits between preparation (Start -> first Step:
//     initFrameSearch() decodes the reference frame, and the search stream is
//     opened and indexed) and the compare loop itself
//
//   usage: test_framesearch_progress <es-file> [refIndex] [searchIndex]
//
// Default indices suit the tux *_duplicate fixtures (sequence A, black,
// sequence A again): the reference frame is near the start, the search starts
// after it, so the equal frame lies a real distance away.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>
#include <QVector>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/tth264videostream.h"
#include "avstream/tth265videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttsettings.h"
#include "data/ttframesearchtask.h"

#include <cstdio>

namespace {

struct Report
{
  qint64  atMs;
  int     state;
  QString msg;
  quint64 value;
};

const char* stateName(int s)
{
  switch (s) {
    case StatusReportArgs::Init:     return "Init";
    case StatusReportArgs::Start:    return "Start";
    case StatusReportArgs::Step:     return "Step";
    case StatusReportArgs::Exit:     return "Exit";
    case StatusReportArgs::Canceled: return "Canceled";
    case StatusReportArgs::Error:    return "Error";
    case StatusReportArgs::Stage:    return "Stage";
    default:                         return "?";
  }
}

TTVideoStream* makeStream(const QFileInfo& fi)
{
  const QString suffix = fi.suffix().toLower();
  if (suffix == "m2v" || suffix == "mpv" || suffix == "mpeg2")
    return new TTMpeg2VideoStream(fi);
  if (suffix == "264" || suffix == "h264" || suffix == "avc")
    return new TTH264VideoStream(fi);
  if (suffix == "265" || suffix == "h265" || suffix == "hevc")
    return new TTH265VideoStream(fi);
  return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);

  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file> [refIndex] [searchIndex]\n", argv[0]);
    return 2;
  }

  QFileInfo fi(QString::fromLocal8Bit(argv[1]));
  if (!fi.exists()) {
    fprintf(stderr, "no such file: %s\n", qPrintable(fi.absoluteFilePath()));
    return 2;
  }

  TTVideoStream* stream = makeStream(fi);
  if (stream == nullptr) {
    fprintf(stderr, "unsupported extension: %s\n", qPrintable(fi.suffix()));
    return 2;
  }

  // The application searches inside ONE stream object: TTAVData::onDoFrameSearch
  // passes avItem->videoStream() as reference and mpCurrentAVItem->videoStream()
  // as search stream, which is the same object whenever both refer to the same
  // item - the normal case. Reproduced here.
  QElapsedTimer openClock;
  openClock.start();
  stream->createHeaderList();
  stream->createIndexList();
  if (stream->indexList() == nullptr || stream->indexList()->count() == 0) {
    fprintf(stderr, "index list empty - cannot search\n");
    return 1;
  }
  const qint64 openMs = openClock.elapsed();

  const int frames    = stream->frameCount();
  const int refIndex    = (argc > 2) ? QString(argv[2]).toInt() : 0;
  const int searchIndex = (argc > 3) ? QString(argv[3]).toInt() : qMin(50, frames - 1);

  const int searchLen = TTSettings::instance()->searchLength();
  printf("file          : %s\n", qPrintable(fi.fileName()));
  printf("frames        : %d   frame rate: %.3f\n", frames, double(stream->frameRate()));
  printf("index build   : %lld ms (outside the task - the GUI does this at open time)\n",
         static_cast<long long>(openMs));
  printf("searchLength  : %d s -> search window %d frames\n",
         searchLen, int(searchLen * stream->frameRate()));
  printf("reference idx : %d     search start idx: %d\n\n", refIndex, searchIndex);

  TTFrameSearchTask task(stream, refIndex, stream, searchIndex);

  QVector<Report> reports;
  QElapsedTimer clock;

  QObject::connect(&task, &TTThreadTask::statusReport,
                   [&](TTThreadTask*, int state, const QString& msg, quint64 value) {
                     reports.append({clock.elapsed(), state, msg, value});
                   });

  int foundAt = -2;
  QObject::connect(&task, qOverload<int>(&TTFrameSearchTask::finished),
                   [&](int idx) { foundAt = idx; });

  clock.start();
  task.runSynchron();
  const qint64 totalMs = clock.elapsed();

  // --- per-state counts -----------------------------------------------------
  int counts[8] = {0};
  for (const Report& r : reports)
    if (r.state >= 0 && r.state < 8) counts[r.state]++;

  printf("total runtime : %lld ms   result: %s%d\n",
         static_cast<long long>(totalMs),
         foundAt == -2 ? "no finished() signal, idx " : "found index ", foundAt);
  printf("reports       : %lld total —", static_cast<long long>(reports.size()));
  for (int s = 0; s < 8; ++s)
    if (counts[s] > 0) printf("  %s×%d", stateName(s), counts[s]);
  printf("\n\n");

  // --- where the silence is -------------------------------------------------
  // A gap is the wall-clock distance between two consecutive reports. The gap
  // before the first report counts too: that is the time the dialog shows
  // nothing at all.
  qint64  worstGap = 0;
  int     worstIdx = -1;
  qint64  prev     = 0;
  for (int i = 0; i < reports.size(); ++i) {
    const qint64 gap = reports[i].atMs - prev;
    if (gap > worstGap) { worstGap = gap; worstIdx = i; }
    prev = reports[i].atMs;
  }
  const qint64 tailGap = totalMs - prev;

  printf("largest gap   : %lld ms before report #%d (%s \"%s\")\n",
         static_cast<long long>(worstGap), worstIdx,
         worstIdx >= 0 ? stateName(reports[worstIdx].state) : "-",
         worstIdx >= 0 ? qPrintable(reports[worstIdx].msg) : "");
  printf("gap after last: %lld ms\n", static_cast<long long>(tailGap));

  // Preparation = Start .. first Step. That is initFrameSearch() plus opening
  // and indexing the search stream, all of it silent.
  qint64 startAt = -1, firstStepAt = -1;
  for (const Report& r : reports) {
    if (startAt < 0 && r.state == StatusReportArgs::Start)     startAt = r.atMs;
    if (firstStepAt < 0 && r.state == StatusReportArgs::Step)  firstStepAt = r.atMs;
  }
  if (startAt >= 0 && firstStepAt >= 0) {
    const qint64 prep = firstStepAt - startAt;
    printf("preparation   : %lld ms silent between Start and the first Step"
           " (%.0f%% of the run)\n",
           static_cast<long long>(prep), 100.0 * double(prep) / double(qMax<qint64>(totalMs, 1)));
  } else {
    printf("preparation   : not measurable (Start or Step missing)\n");
  }

  // --- full timeline, abbreviated in the middle -----------------------------
  printf("\n%-9s %-9s %-10s %s\n", "at [ms]", "state", "value", "message");
  const int head = 12, tail = 6;
  for (int i = 0; i < reports.size(); ++i) {
    if (reports.size() > head + tail && i == head) {
      printf("   ...     (%lld reports omitted)\n",
             static_cast<long long>(reports.size() - head - tail));
      i = int(reports.size()) - tail - 1;
      continue;
    }
    printf("%9lld %-9s %-10llu %s\n",
           static_cast<long long>(reports[i].atMs), stateName(reports[i].state),
           static_cast<unsigned long long>(reports[i].value), qPrintable(reports[i].msg));
  }

  delete stream;
  return 0;
}
