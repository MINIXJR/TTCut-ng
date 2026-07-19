/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: verify TTESInfo parses the demux-repair .info fields —         */
/* es_missing_ranges, corrupt_frame_ranges, audio_N_silence_ms/removed_ms.    */
/* Writes a self-contained mini .info, loads it via TTESInfo, and asserts     */
/* every new getter (including absent-key and out-of-range defaults).        */
/*----------------------------------------------------------------------------*/

#include "../../avstream/ttesinfo.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* what)
{
  if (cond) {
    printf("PASS: %s\n", what);
  } else {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

int main()
{
  const QString dir = "/usr/local/src/CLAUDE_TMP/TTCut-ng/demuxrepair";
  QDir().mkpath(dir);
  const QString path = dir + "/mini.info";

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    fprintf(stderr, "cannot write %s\n", qPrintable(path));
    return 1;
  }
  QTextStream out(&f);
  out <<
    "# TTCut Elementary Stream Info File\n"
    "# Generated: test_esinfo\n"
    "# Source: mini_test\n"
    "\n"
    "[video]\n"
    "file=mini.264\n"
    "codec=h264\n"
    "width=1280\n"
    "height=720\n"
    "frame_rate=50/1\n"
    "start_pts=0.0\n"
    "\n"
    "[audio]\n"
    "count=2\n"
    "audio_0_file=mini_und.mp2\n"
    "audio_0_codec=mp2\n"
    "audio_0_lang=und\n"
    "audio_0_first_pts=0.0\n"
    "audio_0_trimmed_ms=0\n"
    "audio_0_silence_ms=224\n"
    "audio_0_removed_ms=400\n"
    // Track 1 deliberately omits silence_ms/removed_ms — must default to 0.
    "audio_1_file=mini_und.ac3\n"
    "audio_1_codec=ac3\n"
    "audio_1_lang=und\n"
    "audio_1_first_pts=0.0\n"
    "audio_1_trimmed_ms=0\n"
    "\n"
    "[warnings]\n"
    "es_missing_frames=500,501,1480,1481\n"
    "es_missing_ranges=500-501:400,1480-1481:300\n"
    "corrupt_frame_ranges=100-140\n"
    "es_total_aus=89800\n"
    "es_doubled_pts_aus=454,668,1463\n"
    "es_extra_frames=1,2,3\n";           // legacy key: must be IGNORED
  f.close();

  TTESInfo info;
  check(info.load(path), "load() returns true");
  check(info.isLoaded(), "isLoaded() true after load");

  QList<TTESRange> missing = info.esMissingRanges();
  check(missing.size() == 2, "esMissingRanges() has 2 entries");
  if (missing.size() >= 2) {
    check(missing[0].start == 500 && missing[0].end == 501 && missing[0].ms == 400,
          "esMissingRanges()[0] == 500-501:400");
    check(missing[1].start == 1480 && missing[1].end == 1481 && missing[1].ms == 300,
          "esMissingRanges()[1] == 1480-1481:300");
  }

  QList<TTESRange> corrupt = info.corruptFrameRanges();
  check(corrupt.size() == 1, "corruptFrameRanges() has 1 entry");
  if (corrupt.size() >= 1) {
    check(corrupt[0].start == 100 && corrupt[0].end == 140 && corrupt[0].ms == -1,
          "corruptFrameRanges()[0] == 100-140, ms == -1 (unknown)");
  }

  check(info.audioSilenceMs(0) == 224, "audioSilenceMs(0) == 224");
  check(info.audioRemovedMs(0) == 400, "audioRemovedMs(0) == 400");
  check(info.audioSilenceMs(1) == 0, "audioSilenceMs(1) == 0 (keys absent for track 1)");
  check(info.audioRemovedMs(1) == 0, "audioRemovedMs(1) == 0 (keys absent for track 1)");
  check(info.audioSilenceMs(2) == 0, "audioSilenceMs(2) == 0 (out-of-range track)");
  check(info.audioRemovedMs(2) == 0, "audioRemovedMs(2) == 0 (out-of-range track)");

  check(info.esTotalAus() == 89800, "es_total_aus parsed");
  check(info.esDoubledPtsAus().size() == 3, "es_doubled_pts_aus count (legacy es_extra_frames ignored)");
  check(info.esDoubledPtsAus().value(0) == 454 &&
        info.esDoubledPtsAus().value(2) == 1463, "es_doubled_pts_aus values");

  // Absent-key defaults: a minimal .info without the new keys.
  const QString path2 = dir + "/mini_nokeys.info";
  QFile f2(path2);
  if (!f2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    fprintf(stderr, "cannot write %s\n", qPrintable(path2));
    return 1;
  }
  QTextStream out2(&f2);
  out2 << "[video]\nfile=mini.264\ncodec=h264\nwidth=1280\nheight=720\n"
          "frame_rate=50/1\nstart_pts=0.0\n";
  f2.close();
  TTESInfo info2;
  check(info2.load(path2), "load() nokeys returns true");
  check(info2.esTotalAus() == -1, "es_total_aus default -1");
  check(info2.esDoubledPtsAus().isEmpty(), "es_doubled_pts_aus default empty");

  if (failures == 0) {
    printf("PASS\n");
    return 0;
  }
  printf("FAIL (%d assertion(s) failed)\n", failures);
  return 1;
}
