// Equivalence gate for ttAnalyzeAcmodWindow() (avstream/ttac3acmod.cpp),
// which replaced two copies of the majority-acmod rule on 2026-09-11: the
// cut pipeline's sync-word file scan (TTAudioCutter::analyzeAcmod, fixed
// 32 ms frame, first + last 100 frames of the window) and the cut list's
// in-memory scan (TTCutTreeView::updateAcmodIcon, real frame_time, last 100
// frames only for windows of 200+ frames). The old file scan is kept HERE as
// the reference; every window of a sweep is evaluated by both and the
// results compared. Windows of 100+ frames must agree on all three values.
// Shorter windows are excluded from the comparison: the reference is wrong
// there - its cut-out sample range [cutOutFrame - 100, cutOutFrame) starts
// BEFORE the cut-in, so its "cut-in acmod" was the acmod of frame
// cutOut - 100 and its majority counted frames outside the window (measured
// on mixed5.ac3: window [8.41 s, 8.91 s) reported cut-in acmod 2 for a frame
// that is 5.1). The shared function samples the window only.
//
//   usage: test_acmod_majority <file.ac3>
//
// The file should carry several acmod switches (gate_ac3fix.sh's mixed.ac3
// recipe: stereo + 5.1 + stereo concatenated ES).
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <cmath>
#include <cstdio>

#include "avstream/ttac3acmod.h"
#include "avstream/ttac3audiostream.h"
#include "avstream/ttaudioheaderlist.h"
#include "avstream/ttac3audioheader.h"

namespace {

struct Ref { int main = -1, in = -1, out = -1; };

// The former TTAudioCutter::analyzeAcmod(), verbatim in behaviour.
Ref referenceScan(const QString& audioFile, double cutInTime, double cutOutTime)
{
  Ref info;
  QFile file(audioFile);
  if (!file.open(QIODevice::ReadOnly)) return info;
  static const int AC3FrameWords[3][38] = {
    { 64, 64, 80, 80, 96, 96,112,112,128,128,160,160,192,192,224,224,256,256,320,320,
     384,384,448,448,512,512,640,640,768,768,896,896,1024,1024,1152,1152,1280,1280},
    { 69, 70, 87, 88,104,105,121,122,139,140,174,175,208,209,243,244,278,279,348,349,
     417,418,487,488,557,558,696,697,835,836,975,976,1114,1115,1253,1254,1393,1394},
    { 96, 96,120,120,144,144,168,168,192,192,240,240,288,288,336,336,384,384,480,480,
     576,576,672,672,768,768,960,960,1152,1152,1344,1344,1536,1536,1728,1728,1920,1920}
  };
  static const int SAMPLE_FRAMES = 100;
  int acmodCount[8] = {0};
  int firstAcmod = -1, lastAcmod = -1, totalFrames = 0, frameIndex = 0;
  const double frameTime = 0.032;
  const int cutInFrame  = static_cast<int>(cutInTime / frameTime);
  const int cutOutFrame = static_cast<int>(cutOutTime / frameTime);
  quint8 header[8]; qint64 pos = 0;
  while (pos < file.size() - 8) {
    file.seek(pos);
    if (file.read(reinterpret_cast<char*>(header), 8) != 8) break;
    if (header[0] != 0x0B || header[1] != 0x77) { pos++; continue; }
    int fscod = (header[4] >> 6) & 0x03, frmsizecod = header[4] & 0x3F;
    if (fscod >= 3 || frmsizecod >= 38) { pos++; continue; }
    int frameSize = AC3FrameWords[fscod][frmsizecod] * 2;
    if (frameSize <= 0) { pos++; continue; }
    int acmod = (header[6] >> 5) & 0x07;
    bool inCutInRange  = (frameIndex >= cutInFrame && frameIndex < cutInFrame + SAMPLE_FRAMES);
    bool inCutOutRange = (frameIndex >= cutOutFrame - SAMPLE_FRAMES && frameIndex < cutOutFrame);
    if (inCutInRange || inCutOutRange) {
      acmodCount[acmod]++; totalFrames++;
      if (firstAcmod < 0) firstAcmod = acmod;
      lastAcmod = acmod;
    }
    if (frameIndex > cutOutFrame + SAMPLE_FRAMES) break;
    frameIndex++; pos += frameSize;
  }
  if (totalFrames == 0) return info;
  int mainAcmod = 0, maxCount = 0;
  for (int i = 0; i < 8; i++) if (acmodCount[i] > maxCount) { maxCount = acmodCount[i]; mainAcmod = i; }
  info.main = mainAcmod; info.in = firstAcmod; info.out = lastAcmod;
  return info;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <file.ac3>\n", argv[0]); return 2; }
  const QString path = QString::fromLocal8Bit(argv[1]);

  const QFileInfo fi(path);
  TTAC3AudioStream stream(fi);
  stream.createHeaderList();
  const int frames = stream.headerList() ? stream.headerList()->count() : 0;
  if (frames < 300) { fprintf(stderr, "header list too short (%d frames)\n", frames); return 2; }
  const double dur = frames * 0.032;
  printf("file=%s frames=%d duration=%.2fs\n", qPrintable(QFileInfo(path).fileName()), frames, dur);
  {
    // acmod runs of the header list, to read the sweep results against
    int runAcmod = -1, runLen = 0; QString runs;
    for (int i = 0; i < frames; ++i) {
      auto* h = dynamic_cast<TTAC3AudioHeader*>(stream.headerList()->audioHeaderAt(i));
      int a = h ? h->acmod : -1;
      if (a != runAcmod) { if (runLen) runs += QString("%1x%2,").arg(runAcmod).arg(runLen); runAcmod = a; runLen = 0; }
      ++runLen;
    }
    runs += QString("%1x%2").arg(runAcmod).arg(runLen);
    auto* h0 = dynamic_cast<TTAC3AudioHeader*>(stream.headerList()->audioHeaderAt(0));
    printf("header list: frame_time=%.6f runs=%s\n", h0 ? h0->frame_time : -1.0, qPrintable(runs));
  }

  int windows = 0, compared = 0, shortWindows = 0, mainDiff = 0, inDiff = 0, outDiff = 0;
  // Sweep: window lengths from 0.5 s to 12 s, starts every 0.7 s; cut-out
  // times both off (+0.010 s) and exactly on a frame boundary.
  for (double len = 0.5; len <= 12.0; len += 1.1) {
    for (double start = 0.0; start + len < dur; start += 0.7) {
      for (int shape = 0; shape < 2; ++shape) {
        const double cutIn  = start + 0.010;
        const double cutOut = (shape == 0) ? start + len + 0.010
                                           : std::floor((start + len) / 0.032) * 0.032;
        ++windows;
        const int lenFrames = static_cast<int>(cutOut / 0.032) - static_cast<int>(cutIn / 0.032);
        if (lenFrames < kAcmodSampleFrames) { ++shortWindows; continue; }
        const Ref r = referenceScan(path, cutIn, cutOut);
        const TTAcmodInfo n = ttAnalyzeAcmodWindow(&stream, cutIn, cutOut);
        ++compared;
        if (r.main != n.mainAcmod) {
          ++mainDiff;
          if (mainDiff <= 5)
            printf("  main differs: [%.3f,%.3f) ref=%d new=%d (shape %d)\n", cutIn, cutOut, r.main, n.mainAcmod, shape);
        }
        if (r.in != n.cutInAcmod) {
          ++inDiff;
          if (inDiff <= 5)
            printf("  in differs: [%.3f,%.3f) ref=%d new=%d (shape %d)\n", cutIn, cutOut, r.in, n.cutInAcmod, shape);
        }
        if (r.out != n.cutOutAcmod) {
          ++outDiff;
          if (outDiff <= 5)
            printf("  out differs: [%.3f,%.3f) ref=%d new=%d (shape %d)\n", cutIn, cutOut, r.out, n.cutOutAcmod, shape);
        }
      }
    }
  }
  printf("windows=%d compared=%d (short, skipped: %d) mainDiff=%d inDiff=%d outDiff=%d\n",
         windows, compared, shortWindows, mainDiff, inDiff, outDiff);
  int failures = 0;
  auto check = [&](bool ok, const char* what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures; };
  check(compared > 200, "enough windows of 100+ frames compared");
  check(mainDiff == 0, "majority acmod identical to the former file scan on every compared window");
  check(inDiff == 0, "cut-in acmod identical");
  check(outDiff == 0, "cut-out acmod identical");

  // The short-window case the reference got wrong: [8.41 s, 8.91 s) lies
  // inside the first 5.1 run of mixed5.ac3 (frames 250..374, acmod 7).
  {
    const TTAcmodInfo n = ttAnalyzeAcmodWindow(&stream, 8.41, 8.91);
    printf("short window [8.41,8.91): main=%d in=%d out=%d\n", n.mainAcmod, n.cutInAcmod, n.cutOutAcmod);
    check(n.mainAcmod == 7 && n.cutInAcmod == 7 && n.cutOutAcmod == 7,
          "short window inside one run: all three values are that run's acmod");
  }
  printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
