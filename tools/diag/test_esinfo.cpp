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

  // --- audio_N_start_offset_ms (slot rule, 2026-10-07) ---
  {
    const QString p2 = dir + "/offsets.info";
    QFile f2(p2);
    if (!f2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) { fprintf(stderr, "cannot write\n"); return 1; }
    QTextStream o2(&f2);
    o2 << "[video]\nfile=o.264\ncodec=h264\nframe_rate=50/1\n\n"
          "[timing]\nfirst_video_pts=100.0\nfirst_audio_pts=99.5\naudio_trimmed_ms=500\nav_offset_ms=-8\n\n"
          "[audio]\ncount=3\n"
          "audio_0_file=o_deu.mp2\naudio_0_codec=mp2\naudio_0_lang=deu\naudio_0_start_offset_ms=-8\n"
          "audio_1_file=o_mis.mp2\naudio_1_codec=mp2\naudio_1_lang=mis\n"                      // no offset
          "audio_2_file=o_deu.ac3\naudio_2_codec=ac3\naudio_2_lang=deu\naudio_2_start_offset_ms=20000\n"; // out of range
    f2.close();
    TTESInfo e2(p2);
    check(e2.isLoaded(), "offsets: loaded");
    check(e2.audioTrack(0).hasStartOffset && e2.audioTrack(0).startOffsetMs == -8, "offsets: track 0 = -8");
    check(!e2.audioTrack(1).hasStartOffset && e2.audioTrack(1).startOffsetMs == 0, "offsets: track 1 absent -> 0");
    check(!e2.audioTrack(2).hasStartOffset, "offsets: 20000 ms rejected");
    check(e2.hasTrackStartOffsets(), "offsets: hasTrackStartOffsets");
    check(e2.startOffsetMsForAudioFile("/x/y/o_deu.mp2") == -8, "offsets: lookup by file name");
    check(e2.startOffsetMsForAudioFile("/x/y/o_mis.mp2") == 0, "offsets: unlisted file -> 0");
    check(e2.avOffsetMs() == 0, "offsets: av_offset_ms superseded -> 0");
    const TTESInfoTiming t2 = TTESInfo::timingForVideo(dir + "/offsets.264");
    check(t2.found && t2.hasTrackStartOffsets && t2.trackStartOffsetMs.value("o_deu.mp2") == -8 && t2.avOffsetMs == 0,
          "offsets: timingForVideo carries the table and a zero global offset");
  }
  {
    const QString p3 = dir + "/nooffsets.info";
    QFile f3(p3);
    if (!f3.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) { fprintf(stderr, "cannot write\n"); return 1; }
    QTextStream o3(&f3);
    o3 << "[video]\nfile=n.264\ncodec=h264\nframe_rate=50/1\n\n[timing]\nfirst_video_pts=1\nfirst_audio_pts=1\nav_offset_ms=-8\n\n[audio]\ncount=1\naudio_0_file=n_deu.mp2\naudio_0_codec=mp2\naudio_0_lang=deu\n";
    f3.close();
    TTESInfo e3(p3);
    check(!e3.hasTrackStartOffsets() && e3.avOffsetMs() == -8, "no offsets: old .info keeps av_offset_ms");
  }

  // --- effective per-track delay = user delay + .info start offset ---
  {
    const QString v = dir + "/offsets.264";   // resolves offsets.info written above
    check(TTESInfo::effectiveAudioDelayMs(100, v, "/x/y/o_deu.mp2") == 92, "effective delay: 100 + (-8) = 92");
    check(TTESInfo::effectiveAudioDelayMs(0, v, "/x/y/o_deu.mp2") == -8, "effective delay: no user delay -> the offset");
    check(TTESInfo::effectiveAudioDelayMs(100, v, "/x/y/o_mis.mp2") == 100, "effective delay: track without offset -> user delay");
    check(TTESInfo::effectiveAudioDelayMs(100, v, "/x/y/o_deu.ac3") == 100, "effective delay: rejected offset (20000) -> user delay");
    check(TTESInfo::effectiveAudioDelayMs(100, dir + "/nosuch.264", "/x/y/o_deu.mp2") == 100, "effective delay: no .info -> user delay");
    check(TTESInfo::effectiveAudioDelayMs(100, QString(), "/x/y/o_deu.mp2") == 100, "effective delay: no video path -> user delay");
  }

  if (failures == 0) {
    printf("PASS\n");
    return 0;
  }
  printf("FAIL (%d assertion(s) failed)\n", failures);
  return 1;
}
