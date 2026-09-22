// Drift gate for the two preview clip producers.
//
// TTCutPreviewTask builds the preview clips for every cut; TTCutPreview
// rebuilds a SINGLE clip when the user moves a cut edge (burst shift, aspect
// jump). Both are needed and both stay - what must not happen is that they
// drift apart, because a rebuilt clip that is muxed differently from the
// originals looks right and is not. Everything they share now lives in
// data/ttpreviewclip.cpp, and this harness holds the two against each other.
//
// It needs no recorded reference: the run produces BOTH sides itself. First
// the real task (TTAVData::doCutPreview) writes preview_001..NNN.mkv, then
// ttRebuildMpeg2PreviewClip / ttRebuildSmartCutPreviewClip rebuilds one of
// them from the same cut entries - the way the dialog does after an edge move.
// The two files must then describe the same clip.
//
// Driving the rebuild at all is what the extraction bought: TTCutPreview
// cannot run here, it builds a TTMpvWrapper and needs a GL context (see
// test_preview_then_cut.cpp, which refuses to run offscreen for that reason).
//
//   usage: test_preview_clip <video-es> <audio-es> <workdir> [cutIn cutOut]
//
// Two cuts are placed so the preview list has four entries and three clips:
// clip 0 (first cut-in), clip 1 (the transition between the cuts) and clip 2
// (last cut-out). Clip 1 is the one rebuilt - it is the only one built from
// two cut entries, so it exercises the pair branch of ttBuildClipCutList.
//
// What is compared, and why not everything:
//
//   video   packet count, and per packet pts/dts/flags. For H.264/H.265 the
//           payload MD5 as well: same source, same preset, same segment
//           bounds, and the encoder is recreated per segment, so the bytes
//           have to match. For MPEG-2 the payload is deliberately NOT
//           compared - that re-encoder runs with thread_count = 0 and is not
//           reproducible from run to run (TODO.md, "Der MPEG-2-Neucodierer
//           ist von Lauf zu Lauf nicht reproduzierbar"), which is a property
//           of the encoder, not of this refactor.
//
//   audio   codec, channel count and sample rate exactly; length in packets
//           within two frames. The two paths cut audio differently on
//           purpose: the task plans the cut (grid snapping, per-track delay,
//           acmod normalization), the rebuild uses the raw three-argument
//           TTAudioCutter::cut. That is the "Option A" divergence documented
//           in docs/code-map/audio-cut-timing.md. Comparing audio exactly
//           here would assert that divergence away instead of reporting it.
//
// A FAIL on the video side is what this gate exists for: it means the two
// producers no longer agree about a clip.
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
}

#include "avstream/ttavstream.h"
#include "avstream/ttavtypes.h"
#include "avstream/ttvideoindexlist.h"
#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "data/ttpreviewclip.h"

namespace {

int fail(const QString& msg)
{
  printf("FAIL: %s\n", qPrintable(msg));
  return 1;
}

struct Packet {
  qint64     pts  = AV_NOPTS_VALUE;
  qint64     dts  = AV_NOPTS_VALUE;
  int        size = 0;
  int        flags = 0;
  QByteArray md5;
};

struct StreamInfo {
  bool           ok         = false;
  AVCodecID      codecId    = AV_CODEC_ID_NONE;
  int            width      = 0;
  int            height     = 0;
  int            channels   = 0;
  int            sampleRate = 0;
  double         durationSec = 0.0;
  QList<Packet>  packets;
};

struct ClipInfo {
  bool       ok = false;
  StreamInfo video;
  StreamInfo audio;
};

//! Read one MKV into per-stream packet lists. Container-level fields are never
//! compared (a Matroska segment UID differs on every mux by design).
ClipInfo probeClip(const QString& path)
{
  ClipInfo info;

  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return info;
  if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return info; }

  int videoIdx = -1;
  int audioIdx = -1;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    AVStream* st = fmt->streams[i];
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIdx < 0) {
      videoIdx = static_cast<int>(i);
      info.video.ok      = true;
      info.video.codecId = st->codecpar->codec_id;
      info.video.width   = st->codecpar->width;
      info.video.height  = st->codecpar->height;
      if (st->duration > 0)
        info.video.durationSec = st->duration * av_q2d(st->time_base);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioIdx < 0) {
      audioIdx = static_cast<int>(i);
      info.audio.ok         = true;
      info.audio.codecId    = st->codecpar->codec_id;
      info.audio.channels   = st->codecpar->ch_layout.nb_channels;
      info.audio.sampleRate = st->codecpar->sample_rate;
      if (st->duration > 0)
        info.audio.durationSec = st->duration * av_q2d(st->time_base);
    }
  }

  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt, pkt) >= 0) {
    StreamInfo* target = (pkt->stream_index == videoIdx) ? &info.video
                       : (pkt->stream_index == audioIdx) ? &info.audio
                       : nullptr;
    if (target != nullptr) {
      Packet p;
      p.pts   = pkt->pts;
      p.dts   = pkt->dts;
      p.size  = pkt->size;
      p.flags = pkt->flags;
      p.md5   = QCryptographicHash::hash(
          QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size),
          QCryptographicHash::Md5);
      target->packets.append(p);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt);

  info.ok = true;
  return info;
}

//! Compare the video streams. comparePayload is false for MPEG-2 (see header).
bool compareVideo(const StreamInfo& a, const StreamInfo& b, bool comparePayload)
{
  if (!a.ok || !b.ok) {
    fail("one of the clips has no video stream");
    return false;
  }
  if (a.codecId != b.codecId) {
    fail(QString("video codec %1 vs %2").arg(avcodec_get_name(a.codecId))
                                        .arg(avcodec_get_name(b.codecId)));
    return false;
  }
  if (a.width != b.width || a.height != b.height) {
    fail(QString("video size %1x%2 vs %3x%4")
             .arg(a.width).arg(a.height).arg(b.width).arg(b.height));
    return false;
  }
  if (a.packets.count() != b.packets.count()) {
    fail(QString("video packet count %1 vs %2")
             .arg(a.packets.count()).arg(b.packets.count()));
    return false;
  }

  for (int i = 0; i < a.packets.count(); i++) {
    const Packet& pa = a.packets[i];
    const Packet& pb = b.packets[i];
    if (pa.pts != pb.pts || pa.dts != pb.dts) {
      fail(QString("video packet %1: pts/dts %2/%3 vs %4/%5")
               .arg(i).arg(pa.pts).arg(pa.dts).arg(pb.pts).arg(pb.dts));
      return false;
    }
    if ((pa.flags & AV_PKT_FLAG_KEY) != (pb.flags & AV_PKT_FLAG_KEY)) {
      fail(QString("video packet %1: key flag differs").arg(i));
      return false;
    }
    if (comparePayload) {
      if (pa.size != pb.size) {
        fail(QString("video packet %1: size %2 vs %3").arg(i).arg(pa.size).arg(pb.size));
        return false;
      }
      if (pa.md5 != pb.md5) {
        fail(QString("video packet %1: payload differs").arg(i));
        return false;
      }
    }
  }
  return true;
}

//! Compare the audio streams as far as the deliberate divergence allows.
//!
//! Length is measured in PACKETS, not in AVStream::duration: Matroska leaves
//! that field unset for the preview's AC3 track (measured: 0 on both sides),
//! so a duration comparison would pass without ever looking at anything. One
//! packet is one audio frame, which is the unit the divergence works in.
bool compareAudio(const StreamInfo& a, const StreamInfo& b, int frameTolerance)
{
  if (a.ok != b.ok) {
    fail("only one of the clips has audio");
    return false;
  }
  if (!a.ok) return true;
  if (a.codecId != b.codecId) {
    fail(QString("audio codec %1 vs %2").arg(avcodec_get_name(a.codecId))
                                        .arg(avcodec_get_name(b.codecId)));
    return false;
  }
  if (a.channels != b.channels) {
    fail(QString("audio channels %1 vs %2").arg(a.channels).arg(b.channels));
    return false;
  }
  if (a.sampleRate != b.sampleRate) {
    fail(QString("audio sample rate %1 vs %2").arg(a.sampleRate).arg(b.sampleRate));
    return false;
  }
  if (a.packets.isEmpty() || b.packets.isEmpty()) {
    fail("one of the clips has an audio stream without packets");
    return false;
  }

  // The task plans the cut (grid snapping, per-track delay, acmod
  // normalization), the rebuild does not - Option A. A difference beyond a
  // frame or two is more than that divergence explains.
  const int delta = qAbs(a.packets.count() - b.packets.count());
  if (delta > frameTolerance) {
    fail(QString("audio %1 vs %2 packets (%3 frames apart, tolerated %4)")
             .arg(a.packets.count()).arg(b.packets.count()).arg(delta).arg(frameTolerance));
    return false;
  }

  printf("  audio: %s %d ch @ %d Hz, %d vs %d packets (%d frame(s) apart)\n",
         avcodec_get_name(a.codecId), a.channels, a.sampleRate,
         a.packets.count(), b.packets.count(), delta);
  return true;
}

//! Assert that ttBuildClipCutList picks the entries the clip is built from.
bool checkClipCutLists(TTCutList* previewList)
{
  const int numPreview = previewList->count() / 2 + 1;

  for (int i = 0; i < numPreview; i++) {
    TTCutList clip;
    ttBuildClipCutList(previewList, i, &clip);

    const int expected = (i == 0 || i == numPreview - 1) ? 1 : 2;
    if (clip.count() != expected) {
      printf("FAIL: clip %d built from %d entries, expected %d\n", i, clip.count(), expected);
      return false;
    }

    const int iPos = (i == 0) ? 0 : (i - 1) * 2 + 1;
    if (clip.at(0).cutInIndex()  != previewList->at(iPos).cutInIndex() ||
        clip.at(0).cutOutIndex() != previewList->at(iPos).cutOutIndex()) {
      printf("FAIL: clip %d entry 0 is not preview entry %d\n", i, iPos);
      return false;
    }
    if (expected == 2 &&
        (clip.at(1).cutInIndex()  != previewList->at(iPos + 1).cutInIndex() ||
         clip.at(1).cutOutIndex() != previewList->at(iPos + 1).cutOutIndex())) {
      printf("FAIL: clip %d entry 1 is not preview entry %d\n", i, iPos + 1);
      return false;
    }
  }

  // Out-of-range indices must add nothing rather than reach past the list.
  TTCutList spill;
  ttBuildClipCutList(previewList, -1, &spill);
  ttBuildClipCutList(previewList, numPreview, &spill);
  if (spill.count() != 0) {
    printf("FAIL: an out-of-range clip index produced %d entries\n", spill.count());
    return false;
  }

  printf("  clip cut lists: %d clips, entries as expected\n", numPreview);
  return true;
}

//! Assert ttRemovePreviewFiles clears preview* and leaves everything else.
bool checkPreviewFileRemoval(const QString& tempDir)
{
  QDir dir(tempDir);
  const QString decoy = dir.absoluteFilePath("keep_me.txt");
  const QString bait  = dir.absoluteFilePath("preview_999.mkv");

  QFile d(decoy); d.open(QIODevice::WriteOnly); d.write("keep"); d.close();
  QFile b(bait);  b.open(QIODevice::WriteOnly); b.write("drop"); b.close();

  const int removed = ttRemovePreviewFiles();

  if (QFile::exists(bait)) {
    printf("FAIL: ttRemovePreviewFiles left preview_999.mkv behind\n");
    return false;
  }
  if (!QFile::exists(decoy)) {
    printf("FAIL: ttRemovePreviewFiles deleted a file that is not a preview\n");
    return false;
  }
  QFile::remove(decoy);

  printf("  preview cleanup: removed %d file(s), left the decoy\n", removed);
  return true;
}

} // namespace

int main(int argc, char* argv[])
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  if (argc < 4) {
    fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir> [cutIn cutOut]\n", argv[0]);
    return 2;
  }

  const QString videoFile = argv[1];
  const QString audioFile = argv[2];
  const QString workDir   = argv[3];
  const int argCutIn  = (argc > 4) ? QString(argv[4]).toInt() : -1;
  const int argCutOut = (argc > 5) ? QString(argv[5]).toInt() : -1;

  const QString tempDir = QDir(workDir).absoluteFilePath("temp");
  const QString cutDir  = QDir(workDir).absoluteFilePath("cut");
  QDir().mkpath(tempDir);
  QDir().mkpath(cutDir);
  for (const QFileInfo& fi : QDir(tempDir).entryInfoList(QDir::Files))
    QFile::remove(fi.absoluteFilePath());

  TTSettings::instance()->setTempDirPath(tempDir);
  TTSettings::instance()->setCutDirPath(cutDir);
  TTSettings::instance()->setCutPreviewSeconds(8);
  TTMessageLogger::getInstance()->setLogFilePath(
      QDir(workDir).absoluteFilePath("preview_clip.log"));

  // Open the streams the way TTOpenVideoTask / TTOpenAudioTask do.
  TTVideoType    vType(videoFile);
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr)            return fail("could not create the video stream");
  if (vStream->createHeaderList() <= 0) return fail("createHeaderList failed");
  if (vStream->createIndexList()  <= 0) return fail("createIndexList failed");
  if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

  TTAVItem* avItem = new TTAVItem(vStream);

  TTSettings::instance()->setEncoderCodec(
      vStream->streamType() == TTAVTypes::h265_video ? 2 :
      vStream->streamType() == TTAVTypes::h264_video ? 1 : 0);

  TTAudioType    aType(audioFile);
  TTAudioStream* aStream = aType.createAudioStream();
  if (aStream == nullptr) return fail("could not create the audio stream");
  aStream->createHeaderList();
  avItem->appendAudioEntry(aStream);

  const bool isMpeg2 = (vStream->streamType() == TTAVTypes::mpeg2_demuxed_video);

  // Two cuts, so the preview list has four entries and the middle clip is a
  // real transition (two cut entries) rather than a lone window.
  const int frameCount = vStream->frameCount();
  const int cutIn  = (argCutIn  >= 0) ? argCutIn  : frameCount / 8;
  const int cutOut = (argCutOut >= 0) ? argCutOut : frameCount / 3;
  const int cut2In  = cutOut + qMax(1, frameCount / 12);
  const int cut2Out = qMin(frameCount - 1, cut2In + (cutOut - cutIn));
  if (cutIn < 0 || cut2Out >= frameCount || cutIn >= cutOut || cut2In >= cut2Out)
    return fail(QString("cut bounds %1..%2 / %3..%4 do not fit a %5-frame stream")
                    .arg(cutIn).arg(cutOut).arg(cut2In).arg(cut2Out).arg(frameCount));

  printf("source: %d frames at %.3f fps, cuts %d..%d and %d..%d\n",
         frameCount, vStream->frameRate(), cutIn, cutOut, cut2In, cut2Out);

  TTCutList cutList;
  cutList.append(avItem, cutIn,  cutOut);
  cutList.append(avItem, cut2In, cut2Out);

  TTAVData avData;
  avData.setNonInteractive(true);

  TTCutList* previewList = nullptr;
  QEventLoop loop;
  QObject::connect(&avData, &TTAVData::cutPreviewFinished,
                   [&](TTCutList* list) { previewList = list; loop.quit(); });
  QObject::connect(&avData, &TTAVData::threadPoolExit, [&] { loop.quit(); });

  // A watchdog rather than a hang: the pool signals are the only exits.
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [&] { loop.quit(); });
  watchdog.start(10 * 60 * 1000);

  avData.doCutPreview(&cutList);
  loop.exec();

  if (previewList == nullptr)      return fail("the preview task produced no cut list");
  if (previewList->count() != 4)   return fail(QString("preview list has %1 entries, expected 4")
                                                   .arg(previewList->count()));

  if (!checkClipCutLists(previewList)) return 1;

  // Clip 1 is the transition. The task wrote it as preview_002.mkv; keep that
  // file and rebuild the same clip next to it.
  const int  clipIndex  = 1;
  const int  fileIndex  = clipIndex + 1;
  const QString taskClip    = QDir(tempDir).absoluteFilePath("preview_002.mkv");
  const QString keptTaskClip = QDir(workDir).absoluteFilePath("task_clip.mkv");

  if (!QFileInfo::exists(taskClip)) return fail("the task wrote no preview_002.mkv");
  QFile::remove(keptTaskClip);
  if (!QFile::copy(taskClip, keptTaskClip)) return fail("could not keep the task's clip");

  TTCutList clip;
  ttBuildClipCutList(previewList, clipIndex, &clip);
  if (clip.count() != 2) return fail("the transition clip was not built from two entries");

  const bool rebuilt = isMpeg2
      ? ttRebuildMpeg2PreviewClip(&avData, &clip, fileIndex)
      : ttRebuildSmartCutPreviewClip(&clip, fileIndex);
  if (!rebuilt) return fail("the rebuild reported a failure");

  const ClipInfo fromTask    = probeClip(keptTaskClip);
  const ClipInfo fromRebuild = probeClip(taskClip);
  if (!fromTask.ok)    return fail("could not read the task's clip");
  if (!fromRebuild.ok) return fail("could not read the rebuilt clip");

  printf("  video: %s %dx%d, %d packets both sides\n",
         avcodec_get_name(fromTask.video.codecId),
         fromTask.video.width, fromTask.video.height,
         fromTask.video.packets.count());

  // MPEG-2 re-encodes non-reproducibly (thread_count = 0), so its payload is
  // out of scope here - see the file header.
  if (!compareVideo(fromTask.video, fromRebuild.video, /*comparePayload=*/!isMpeg2))
    return 1;
  if (isMpeg2)
    printf("  video payload not compared (MPEG-2 re-encoder is not reproducible)\n");

  // Option A shifts the boundaries by at most the snapping does: one frame
  // per segment end, two segments in a transition clip.
  if (!compareAudio(fromTask.audio, fromRebuild.audio, /*frameTolerance=*/2))
    return 1;

  // PREVIEW_CLIP_KEEP=1 skips the cleanup check so the produced clips stay on
  // disk for a hand measurement (the task's clip is kept as task_clip.mkv
  // either way; the rebuilt one is preview_002.mkv in the temp directory).
  // Same shape as test_preview_then_cut.cpp's PREVIEW_EXEC.
  if (qgetenv("PREVIEW_CLIP_KEEP") == "1") {
    printf("  preview cleanup skipped (PREVIEW_CLIP_KEEP=1), clips kept in %s\n",
           qPrintable(tempDir));
  } else if (!checkPreviewFileRemoval(tempDir)) {
    return 1;
  }

  printf("PASS: the rebuilt clip matches the one the task produced\n");
  return 0;
}
