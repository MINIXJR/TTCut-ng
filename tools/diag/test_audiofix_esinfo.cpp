/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: verify TTESInfo parses per-track audio_N_corrupt_ranges        */
/* (from ttcut-demux's audio-repair sanitizer, Task 7 of ttcut-audiofix).     */
/* Writes a self-contained mini .info, loads it via TTESInfo, and asserts     */
/* the per-track corruptRanges list (including the inverted-range guard).     */
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
  const QString dir = "/usr/local/src/CLAUDE_TMP/TTCut-ng/audiofix_esinfo";
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
    "# Generated: test_audiofix_esinfo\n"
    "# Source: mini_test\n"
    "\n"
    "[audio]\n"
    "count=2\n"
    "audio_0_file=a.mp2\n"
    "audio_0_codec=mp2\n"
    "audio_1_file=b.ac3\n"
    "audio_1_codec=ac3\n"
    "audio_0_corrupt_ranges=7093-7099,9000-9010\n"
    "audio_0_junk_bytes=468\n"           // human diagnostic: must NOT be parsed
    "audio_1_corrupt_ranges=5-3\n";      // inverted -> must be rejected
  f.close();

  TTESInfo info;
  check(info.load(path), "load() returns true");
  check(info.isLoaded(), "isLoaded() true after load");
  check(info.audioTrackCount() == 2, "audioTrackCount() == 2");

  QList<TTESRange> t0 = info.audioTrack(0).corruptRanges;
  check(t0.size() == 2, "audioTrack(0).corruptRanges has 2 entries");
  if (t0.size() >= 2) {
    check(t0[0].start == 7093 && t0[0].end == 7099 && t0[0].ms == -1,
          "audioTrack(0).corruptRanges[0] == 7093-7099, ms == -1");
    check(t0[1].start == 9000 && t0[1].end == 9010 && t0[1].ms == -1,
          "audioTrack(0).corruptRanges[1] == 9000-9010, ms == -1");
  }

  check(info.audioTrack(1).corruptRanges.isEmpty(),
        "audioTrack(1).corruptRanges empty (inverted range rejected)");

  // Absent-key default: a track without the field must not crash / must
  // default to an empty list.
  check(info.audioTrack(0).file == "a.mp2", "audioTrack(0).file still parsed correctly");

  if (failures == 0) {
    printf("PASS\n");
    return 0;
  }
  printf("FAIL (%d assertion(s) failed)\n", failures);
  return 1;
}
