/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_audioworker.h"
#include "ttanalysislog.h"
#include "../avstream/ttaudioheaderlist.h"
#include "../avstream/ttac3audioheader.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <QDebug>
#include <QLocale>
#include <QScopeGuard>
#include <clocale>

TTStreamPointAudioWorker::TTStreamPointAudioWorker(
    const QString& audioFilePath, float videoFrameRate,
    bool detectSilence, int silenceThresholdDb, float silenceMinDuration,
    bool detectAudioChange, TTAudioHeaderList* audioHeaderList)
  : TTThreadTask("StreamPointAudioAnalysis"),
    mAudioFilePath(audioFilePath),
    mVideoFrameRate(videoFrameRate),
    mDetectSilence(detectSilence),
    mSilenceThresholdDb(silenceThresholdDb),
    mSilenceMinDuration(silenceMinDuration),
    mDetectAudioChange(detectAudioChange),
    mAudioHeaderList(audioHeaderList),
    mLog([this](const QString& s) {
           onStatusReport(StatusReportArgs::AddProcessLine, s, 0);
         }, 20)
{
}

void TTStreamPointAudioWorker::operation()
{
  // Force C locale for this thread — avfilter internally uses locale-dependent
  // number parsing (av_expr_parse) which fails with German comma separator
  setlocale(LC_NUMERIC, "C");

  QList<TTStreamPoint> allPoints;

  onStatusReport(StatusReportArgs::Start, tr("Analyzing audio..."), 2);

  if (mDetectSilence && !mIsAborted) {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "StreamPointAudio: Detecting silence...";
    onStatusReport(StatusReportArgs::Step, tr("Silence detection..."), mStepCount);
    mLog.line(tr("Silence detection: threshold %1 dB, min duration %2 s")
                  .arg(mSilenceThresholdDb)
                  .arg(QLocale().toString(mSilenceMinDuration, 'f', 1)));
    QList<TTStreamPoint> silencePoints = detectSilencePoints();
    allPoints.append(silencePoints);
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "StreamPointAudio: Found" << silencePoints.size() << "silence regions";
    // Silence detection decodes the whole audio track, so it is a realistic
    // place to hit Cancel - the summary has to say the number is partial.
    // Nothing to summarise when the engine never ran - silenceUnavailable()
    // has already said why, and "0 regions found" would contradict it.
    if (!mSilenceEngineFailed) {
      QString silenceSummary = mIsAborted
          ? tr("Silence detection cancelled: %n region(s) found so far", "",
               silencePoints.size())
          : tr("Silence detection: %n region(s) found", "", silencePoints.size());
      if (mLog.suppressed() > 0)
        silenceSummary += tr(" (%1 more events suppressed)").arg(mLog.suppressed());
      mLog.line(silenceSummary);
    }
    mLog.resetCap();   // the format section gets its own budget
    mStepCount = 1;
  }

  if (mDetectAudioChange && !mIsAborted) {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "StreamPointAudio: Detecting audio format changes...";
    onStatusReport(StatusReportArgs::Step, tr("Audio format detection..."), mStepCount);
    QList<TTStreamPoint> changePoints = detectAudioChanges();
    allPoints.append(changePoints);
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "StreamPointAudio: Found" << changePoints.size() << "format changes";
    mStepCount = 2;
  }

  if (!mIsAborted) {
    emit pointsDetected(allPoints);
  }

  onStatusReport(StatusReportArgs::Finished, tr("Audio analysis complete"), mStepCount);
}

void TTStreamPointAudioWorker::cleanUp()
{
}

void TTStreamPointAudioWorker::onUserAbort()
{
  mIsAborted = true;
}

QList<TTStreamPoint> TTStreamPointAudioWorker::silenceUnavailable(const QString& reason)
{
  mSilenceEngineFailed = true;
  mLog.line(tr("Silence detection skipped: %1").arg(reason));
  return QList<TTStreamPoint>();
}

// ---------------------------------------------------------------------------
// Silence detection via libavfilter silencedetect
// ---------------------------------------------------------------------------
QList<TTStreamPoint> TTStreamPointAudioWorker::detectSilencePoints()
{
  QList<TTStreamPoint> results;

  // Every libav handle is released by one scope guard, whichever exit is
  // taken; each free function tolerates a null handle, so a handle that was
  // never created costs nothing.
  AVFormatContext* fmtCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVFilterGraph* filterGraph = nullptr;
  AVFilterInOut* inputs = nullptr;
  AVFilterInOut* outputs = nullptr;
  AVPacket* pkt = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* filtFrame = nullptr;
  const auto releaseAll = qScopeGuard([&]() {
    av_frame_free(&filtFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_free(&filterGraph);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
  });

  // Open audio file
  if (avformat_open_input(&fmtCtx, mAudioFilePath.toUtf8().constData(),
                           nullptr, nullptr) < 0) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("StreamPointAudio: Cannot open %1").arg(mAudioFilePath));
    return silenceUnavailable(tr("the audio file could not be opened"));
  }
  if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
    return silenceUnavailable(tr("the stream information could not be read"));
  }

  int audioIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioIdx < 0) {
    return silenceUnavailable(tr("the file contains no audio stream"));
  }

  AVStream* aStream = fmtCtx->streams[audioIdx];
  const AVCodec* codec = avcodec_find_decoder(aStream->codecpar->codec_id);
  if (!codec) {
    const QString codecName =
        QString::fromUtf8(avcodec_get_name(aStream->codecpar->codec_id));
    return silenceUnavailable(tr("no decoder available for %1").arg(codecName));
  }

  codecCtx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecCtx, aStream->codecpar);
  if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
    return silenceUnavailable(tr("the audio decoder could not be opened"));
  }

  // Build filter graph: abuffersrc -> silencedetect -> abuffersink
  filterGraph = avfilter_graph_alloc();
  AVFilterContext* bufferSrcCtx = nullptr;
  AVFilterContext* bufferSinkCtx = nullptr;

  const AVFilter* abufferSrc = avfilter_get_by_name("abuffer");
  const AVFilter* abufferSink = avfilter_get_by_name("abuffersink");

  // Build abuffer src args
  char chLayoutStr[64] = {0};
  av_channel_layout_describe(&codecCtx->ch_layout, chLayoutStr, sizeof(chLayoutStr));

  char srcArgs[256];
  snprintf(srcArgs, sizeof(srcArgs),
    "sample_rate=%d:sample_fmt=%s:channel_layout=%s:time_base=%d/%d",
    codecCtx->sample_rate,
    av_get_sample_fmt_name(codecCtx->sample_fmt),
    chLayoutStr,
    aStream->time_base.num, aStream->time_base.den);

  if (avfilter_graph_create_filter(&bufferSrcCtx, abufferSrc, "in",
                                    srcArgs, nullptr, filterGraph) < 0 ||
      avfilter_graph_create_filter(&bufferSinkCtx, abufferSink, "out",
                                    nullptr, nullptr, filterGraph) < 0) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("StreamPointAudio: Failed to create audio buffer src/sink"));
    return silenceUnavailable(tr("the audio filter input could not be created"));
  }

  inputs = avfilter_inout_alloc();
  outputs = avfilter_inout_alloc();
  outputs->name = av_strdup("in");
  outputs->filter_ctx = bufferSrcCtx;
  outputs->pad_idx = 0;
  outputs->next = nullptr;
  inputs->name = av_strdup("out");
  inputs->filter_ctx = bufferSinkCtx;
  inputs->pad_idx = 0;
  inputs->next = nullptr;

  // QLocale::c() always uses dot as decimal separator (avfilter requires it)
  QString filterDescr = QString("silencedetect=noise=%1dB:d=%2")
    .arg(mSilenceThresholdDb)
    .arg(QLocale::c().toString(mSilenceMinDuration, 'f', 2));

  if (avfilter_graph_parse_ptr(filterGraph, filterDescr.toUtf8().constData(),
                                &inputs, &outputs, nullptr) < 0 ||
      avfilter_graph_config(filterGraph, nullptr) < 0) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("StreamPointAudio: Failed to configure silencedetect filter"));
    return silenceUnavailable(tr("the silence filter could not be configured"));
  }
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  // Decode + filter loop
  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  filtFrame = av_frame_alloc();
  if (!pkt || !frame || !filtFrame) {
    return silenceUnavailable(tr("not enough memory for the audio buffers"));
  }

  while (av_read_frame(fmtCtx, pkt) >= 0 && !mIsAborted) {
    if (pkt->stream_index == audioIdx) {
      if (avcodec_send_packet(codecCtx, pkt) >= 0) {
        while (avcodec_receive_frame(codecCtx, frame) >= 0 && !mIsAborted) {
          if (av_buffersrc_add_frame_flags(bufferSrcCtx, frame,
                                            AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
            break;

          while (av_buffersink_get_frame(bufferSinkCtx, filtFrame) >= 0) {
            collectSilenceResult(filtFrame, results);
            av_frame_unref(filtFrame);
          }
          av_frame_unref(frame);
        }
      }
    }
    av_packet_unref(pkt);
  }

  // Flush decoder + filter
  avcodec_send_packet(codecCtx, nullptr);
  while (avcodec_receive_frame(codecCtx, frame) >= 0 && !mIsAborted) {
    if (av_buffersrc_add_frame(bufferSrcCtx, frame) < 0) break;
    while (av_buffersink_get_frame(bufferSinkCtx, filtFrame) >= 0) {
      collectSilenceResult(filtFrame, results);
      av_frame_unref(filtFrame);
    }
    av_frame_unref(frame);
  }
  // Final flush
  (void)av_buffersrc_add_frame(bufferSrcCtx, nullptr);
  while (av_buffersink_get_frame(bufferSinkCtx, filtFrame) >= 0) {
    collectSilenceResult(filtFrame, results);
    av_frame_unref(filtFrame);
  }

  return results;
}

// ---------------------------------------------------------------------------
// Audio format change detection via TTAudioHeaderList iteration
// ---------------------------------------------------------------------------
void TTStreamPointAudioWorker::collectSilenceResult(AVFrame* filtFrame,
                                                     QList<TTStreamPoint>& results)
{
  // silencedetect reports metadata on filtered frames:
  // - lavfi.silence_start: timestamp where silence begins (on that frame)
  // - lavfi.silence_end + lavfi.silence_duration: on the frame where silence ends
  // Both can appear on different frames, so we check all metadata tags.

  AVDictionaryEntry* tag = nullptr;
  while ((tag = av_dict_get(filtFrame->metadata, "lavfi.silence_", tag, AV_DICT_IGNORE_SUFFIX))) {
    QString key = QString::fromUtf8(tag->key);
    QString val = QString::fromUtf8(tag->value);
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "  silencedetect metadata:" << key << "=" << val;
  }

  // Check for silence_start → create point at start position
  AVDictionaryEntry* startTag = av_dict_get(filtFrame->metadata,
    "lavfi.silence_start", nullptr, 0);
  if (startTag) {
    double silenceStart = QString::fromUtf8(startTag->value).replace(',', '.').toDouble();
    int frameIdx = qRound(silenceStart * mVideoFrameRate);

    TTStreamPoint pt(frameIdx, StreamPointType::Silence,
      tr("Silence (%1 dB)").arg(mSilenceThresholdDb),
      static_cast<float>(mSilenceThresholdDb), 0.0f);
    results.append(pt);
    return;
  }

  // Check for silence_end → update last silence point with duration
  const AVDictionaryEntry* endTag = av_dict_get(filtFrame->metadata,
    "lavfi.silence_end", nullptr, 0);
  if (endTag && !results.isEmpty()) {
    const AVDictionaryEntry* durTag = av_dict_get(filtFrame->metadata,
      "lavfi.silence_duration", nullptr, 0);
    double duration = durTag ? QString::fromUtf8(durTag->value).replace(',', '.').toDouble() : 0;

    // Update the last silence point (created by silence_start) with duration
    TTStreamPoint& last = results.last();
    if (last.type() == StreamPointType::Silence) {
      // Display value, not a filter argument: follow the user's locale, like
      // the spin box the threshold comes from. Only the avfilter argument
      // above must stay QLocale::c(). The description is stored in the
      // project file but read back as plain text (ttcutprojectdata.cpp),
      // never parsed as a number, so a comma is safe there.
      last.setDescription(tr("Silence (%1 dB, %2s)")
        .arg(mSilenceThresholdDb)
        .arg(QLocale().toString(duration, 'f', 1)));
      mLog.event(tr("%1: silence %2 s")
                     .arg(ttFormatStreamPosition(last.frameIndex(), mVideoFrameRate))
                     .arg(QLocale().toString(duration, 'f', 1)));
    }
  }
}

QList<TTStreamPoint> TTStreamPointAudioWorker::detectAudioChanges()
{
  QList<TTStreamPoint> results;

  if (!mAudioHeaderList || mAudioHeaderList->size() == 0) {
    mLog.line(tr("Audio format detection: no audio header list - skipped"));
    return results;
  }

  // Detect channel configuration changes in AC3 streams
  // by tracking acmod field across consecutive headers
  int prevChannels = -1;
  int ac3Headers = 0;

  for (int i = 0; i < mAudioHeaderList->size() && !mIsAborted; ++i) {
    TTAudioHeader* hdr = mAudioHeaderList->audioHeaderAt(i);
    if (!hdr) continue;

    // Try AC3 header (has acmod field)
    TTAC3AudioHeader* ac3Hdr = dynamic_cast<TTAC3AudioHeader*>(hdr);
    if (ac3Hdr) {
      ac3Headers++;
      int acmod = ac3Hdr->acmod;
      int channels = AC3AudioCodingMode[acmod];
      if (ac3Hdr->lfeon) channels++;  // +1 for LFE (.1)

      if (prevChannels >= 0 && channels != prevChannels) {
        // Channel count actually changed (not just acmod encoding)
        double timeSec = (double)i * 1536.0 / 48000.0;
        int frameIdx = qRound(timeSec * mVideoFrameRate);

        QString prevStr = (prevChannels >= 5) ? "5.1" : QString::number(prevChannels) + ".0";
        QString newStr = (channels >= 5) ? "5.1" : QString::number(channels) + ".0";

        TTStreamPoint pt(frameIdx, StreamPointType::AudioChange,
          QString("Audio %1 \u2192 %2").arg(prevStr, newStr),
          0.0f, 0.0f);
        results.append(pt);
        mLog.event(tr("%1: audio %2 -> %3")
                       .arg(ttFormatStreamPosition(frameIdx, mVideoFrameRate))
                       .arg(prevStr, newStr));
      }
      prevChannels = channels;
    }
    // MP2 headers don't have acmod — channel changes are less common
    // in DVB MP2 streams. Skip for now.
  }

  if (ac3Headers == 0) {
    // detectAudioChanges only reads AC3 acmod; MP2 headers carry no such
    // field and are skipped. Without this line a run over an MP2 track looks
    // exactly like a run that found nothing.
    mLog.line(tr("Audio format detection: no AC3 headers in %1 frames - "
                 "channel-change detection is AC3 only, skipped")
                  .arg(mAudioHeaderList->size()));
    return results;
  }

  QString summary = tr("Audio format detection: %1 AC3 frames").arg(ac3Headers);
  summary += results.isEmpty()
      ? tr(" - channel layout constant, no changes")
      : tr(" - %n change(s)", "", results.size());
  if (mLog.suppressed() > 0)
    summary += tr(" (%1 more events suppressed)").arg(mLog.suppressed());
  mLog.line(summary);

  return results;
}
