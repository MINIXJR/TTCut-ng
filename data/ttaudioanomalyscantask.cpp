/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttaudioanomalyscantask.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <QDebug>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>

namespace {

// Nominal AC3 frame duration: 1536 samples at 48 kHz, the DVB-standard AC3
// sample rate this codebase assumes elsewhere too (see
// TTAudioRepairItem's frame-numbering contract and
// TTStreamPointAudioWorker::detectAudioChanges). FrameStat carries one
// entry per AC3 frame by contract (see header), so evaluate() and the
// scan-to-video-time conversion both use this single constant.
constexpr double kFrameDurSec = 1536.0 / 48000.0;   // 0.032 s

int countExtrasBefore(const QList<int>& extras, int frameIndex)
{
  int lo = 0, hi = extras.size();
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (extras[mid] < frameIndex) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

} // namespace

TTAudioAnomalyScanTask::TTAudioAnomalyScanTask(
    const QString& audioFilePath, int trackIndex, double frameRate,
    const QList<int>& extraFrameIndices,
    const QList<QPair<int,int>>& gapFrameRanges)
  : TTThreadTask("AudioAnomalyScan"),
    mAudioFilePath(audioFilePath),
    mTrackIndex(trackIndex),
    mFrameRate(frameRate > 0.0 ? frameRate : 25.0),
    mExtraFrameIndices(extraFrameIndices),
    mGapFrameRanges(gapFrameRanges)
{
}

void TTAudioAnomalyScanTask::cleanUp()
{
}

void TTAudioAnomalyScanTask::onUserAbort()
{
  mIsAborted = true;
}

// ---------------------------------------------------------------------------
// Audio time -> video display frame index. Inverse of buildVideoKeepList's
// (displayIndex - countExtraFramesBefore(displayIndex)) / frameRate: start
// from the extras-free estimate, then correct for the extras that lie below
// the corrected index. Converges in at most two rounds because
// countExtrasBefore(idx) only changes when idx crosses an extra-frame
// boundary, and the correction only ever grows idx forward past extras it
// has not yet accounted for.
// ---------------------------------------------------------------------------
int TTAudioAnomalyScanTask::videoFrameForTime(double seconds, double fps,
                                              const QList<int>& extraFrameIndices)
{
  if (fps <= 0.0) return 0;
  const int base = qRound(seconds * fps);
  int idx = base;
  for (int round = 0; round < 2; ++round) {
    const int extras = countExtrasBefore(extraFrameIndices, idx);
    const int next = base + extras;
    if (next == idx) break;
    idx = next;
  }
  return qMax(0, idx);
}

// ---------------------------------------------------------------------------
// evaluate() — pure, static, no I/O. See header for the contract.
// ---------------------------------------------------------------------------
QList<TTAudioAnomalyScanTask::Finding> TTAudioAnomalyScanTask::evaluate(
    const QVector<FrameStat>& stats,
    double lfeRmsThresholdDb,
    double centerContrastFactor,
    double lfeNullPercent,
    double lfeMinPeakDb,
    GateStatus* gateOut)
{
  QList<Finding> findings;
  if (gateOut) *gateOut = GateStatus();
  const int n = stats.size();
  if (n == 0) return findings;

  // Step 1: material-suitability hard gate. LFE must be null (<= threshold)
  // in the overwhelming majority of 5.1 frames, or the whole track is
  // "unsuitable, no statement" - deliberately no fallback to a weaker
  // heuristic (spec Komponente 1, Vorbedingung).
  int n51 = 0, nullCount = 0;
  for (const FrameStat& s : stats) {
    if (!s.is51) continue;
    ++n51;
    if (s.lfeRms <= lfeRmsThresholdDb) ++nullCount;
  }
  if (n51 == 0) return findings;
  const double nullPct = 100.0 * nullCount / n51;
  if (gateOut) {
    gateOut->lfeNullPercent = nullPct;
    gateOut->frames51 = n51;
    gateOut->materialUnsuitable = (nullPct < lfeNullPercent);
  }
  if (nullPct < lfeNullPercent) return findings;

  const int gapFrames    = qMax(1, qRound(0.1 / kFrameDurSec));   // 100 ms merge tolerance
  const int windowFrames = qMax(1, qRound(0.5 / kFrameDurSec));   // +/-0.5 s fine-seg window

  // Step 2: candidate LFE-active islands - contiguous runs of active 5.1
  // frames, gap-tolerant, same clustering shape as TTAVData's defect
  // clusters (ttavdata.cpp emitCluster/emitGapCluster).
  QList<int> activeIdx;
  activeIdx.reserve(n);
  for (int f = 0; f < n; ++f)
    if (stats[f].is51 && stats[f].lfeRms > lfeRmsThresholdDb) activeIdx.append(f);

  QList<QPair<int,int>> islands;
  if (!activeIdx.isEmpty()) {
    int clusterStart = activeIdx.first();
    int clusterEnd = clusterStart;
    for (int k = 1; k < activeIdx.size(); ++k) {
      if (activeIdx[k] - clusterEnd <= gapFrames) {
        clusterEnd = activeIdx[k];
      } else {
        islands.append({clusterStart, clusterEnd});
        clusterStart = activeIdx[k];
        clusterEnd = clusterStart;
      }
    }
    islands.append({clusterStart, clusterEnd});
  }

  for (const auto& island : islands) {
    const int islandStart = island.first;
    const int islandEnd = island.second;
    if (islandEnd - islandStart + 1 < 2) continue;   // minimum duration 2 frames

    // Step 3: confirm via center-diff contrast against the local
    // neighbourhood median (contrast, not an absolute threshold - lesson
    // from the burst-transient detector).
    float islandMaxContrast = 0.0f;
    for (int f = islandStart; f <= islandEnd; ++f)
      islandMaxContrast = qMax(islandMaxContrast, stats[f].centerMaxDiff);

    const int nStart = qMax(0, islandStart - windowFrames);
    const int nEnd   = qMin(n - 1, islandEnd + windowFrames);
    QVector<float> neighbourhood;
    neighbourhood.reserve(nEnd - nStart + 1);
    for (int f = nStart; f <= nEnd; ++f)
      if (f < islandStart || f > islandEnd) neighbourhood.append(stats[f].centerMaxDiff);

    float neighbourhoodMedian = 0.0f;
    if (!neighbourhood.isEmpty()) {
      std::sort(neighbourhood.begin(), neighbourhood.end());
      neighbourhoodMedian = neighbourhood[neighbourhood.size() / 2];
    }
    const float contrastFloor =
        static_cast<float>(centerContrastFactor) * qMax(neighbourhoodMedian, 1e-6f);
    if (islandMaxContrast < contrastFloor) continue;   // not confirmed

    // Step 4: fine segmentation (spec Komponente 1, Punkt 4). The reported
    // range is the actual burst blocks within +/-0.5 s of the island, not
    // the whole island - a wide LFE-active island can be mostly an
    // inaudible decay tail. A frame belongs to a burst block when it
    // clears the SAME contrast floor used above, OR its LFE is genuinely
    // loud (>= MinPeak - much stricter than the island's bare activity
    // gate). The start is allowed to precede the island (a real defect can
    // have a center precursor before the LFE onset).
    // contrastFloor is deliberately the SAME value computed for the island
    // confirmation above (island-neighbourhood median * centerContrastFactor)
    // - it is not recomputed per +/-0.5s fine-seg window. A per-window
    // median would let a wide, mostly-quiet window dilute the floor and
    // re-admit the very decay-tail frames the confirmation step already
    // measured against the real neighbourhood; reusing one floor keeps the
    // "burst" criterion anchored to the same loudness baseline throughout.
    QList<QPair<int,int>> blocks;
    int blockStart = -1, blockEnd = -1;
    for (int f = nStart; f <= nEnd; ++f) {
      const bool burst = stats[f].centerMaxDiff >= contrastFloor ||
                          stats[f].lfeRms >= lfeMinPeakDb;
      if (!burst) continue;
      if (blockStart < 0) {
        blockStart = f; blockEnd = f;
      } else if (f - blockEnd <= gapFrames) {
        blockEnd = f;
      } else {
        blocks.append({blockStart, blockEnd});
        blockStart = f; blockEnd = f;
      }
    }
    if (blockStart >= 0) blocks.append({blockStart, blockEnd});
    if (blocks.isEmpty()) continue;   // defensive - the island's own max
                                       // already clears contrastFloor, so
                                       // this should not happen

    // Merge every block chaining (<=100 ms gap) into/from the island into
    // one reported range; unrelated loud moments elsewhere in the wide
    // +/-0.5 s window are dropped.
    int fineFrom = -1, fineTo = -1;
    for (const auto& b : blocks) {
      const bool touchesIsland = b.second >= islandStart - gapFrames &&
                                  b.first  <= islandEnd + gapFrames;
      const bool chainsToRange = fineFrom >= 0 && b.first - fineTo <= gapFrames;
      if (touchesIsland || chainsToRange) {
        if (fineFrom < 0) fineFrom = b.first;
        fineTo = b.second;
      }
    }
    if (fineFrom < 0) continue;

    // Already AC3-frame granularity here (FrameStat contract); add the
    // +/-1 frame safety margin the spec calls for.
    const qint64 frameFrom = qMax(0, fineFrom - 1);
    const qint64 frameTo   = qMin(n - 1, fineTo + 1);

    float lfePeak = -200.0f;
    for (qint64 f = frameFrom; f <= frameTo; ++f)
      lfePeak = qMax(lfePeak, stats[f].lfeRms);

    // Step 5: MinPeak gate - quiet islands (decay tails, low-level program
    // LFE) are contrast-confirmed but not genuinely loud, and are not
    // reported (User-Klassifikation: real defects >= -19.7 dBFS, false
    // positives mostly < -25 dBFS).
    if (lfePeak < lfeMinPeakDb) continue;

    const float contrastRatio = neighbourhoodMedian > 1e-6f
        ? islandMaxContrast / neighbourhoodMedian
        : islandMaxContrast / 1e-6f;
    const float confidence = qBound(0.0f,
        static_cast<float>(contrastRatio / qMax(1.0, centerContrastFactor)), 1.0f);

    findings.append({frameFrom, frameTo, lfePeak, confidence});
  }

  return findings;
}

// ---------------------------------------------------------------------------
// collectFrameStats() — single-pass libav decode. See header for the
// separation-of-concerns rationale.
// ---------------------------------------------------------------------------
QVector<TTAudioAnomalyScanTask::FrameStat> TTAudioAnomalyScanTask::collectFrameStats(
    const QString& audioFilePath,
    int* decodeFailures,
    const std::function<bool()>& shouldAbort,
    const std::function<void(qint64)>& onFrame)
{
  QVector<FrameStat> stats;
  int failures = 0;

  AVFormatContext* fmtCtx   = nullptr;
  AVCodecContext*  codecCtx = nullptr;
  AVPacket*        pkt      = nullptr;
  AVFrame*         frame    = nullptr;
  // Every exit releases what was acquired (all four free functions accept a
  // null handle) and hands the failure count back, so the early returns
  // below carry no cleanup of their own. Same pattern as
  // TTStreamPointAudioWorker::detectSilencePoints().
  const auto releaseAll = qScopeGuard([&]() {
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    if (decodeFailures) *decodeFailures = failures;
  });

  if (avformat_open_input(&fmtCtx, audioFilePath.toUtf8().constData(), nullptr, nullptr) < 0)
    return stats;
  if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
    return stats;
  const int audioIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioIdx < 0)
    return stats;

  AVStream* aStream = fmtCtx->streams[audioIdx];

  // kFrameDurSec (1536/48000) is the ONLY thing that turns a FrameStat index
  // into a time, and from there into a video frame and an AC3 repair range.
  // At any other sample rate an AC3 frame is still 1536 samples but no longer
  // 32 ms, so every position the scan reports would be wrong by the ratio -
  // silently, and the resulting repair would silence the wrong frames. DVB
  // AC3 is 48 kHz by specification; anything else means this is not the kind
  // of material this scan was built and calibrated for, so refuse it instead
  // of computing nonsense (final review M6).
  if (aStream->codecpar->sample_rate != 48000) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("TTAudioAnomalyScanTask: %1 has a sample rate of %2 Hz - the anomaly "
                "scan's 32 ms AC3 frame grid only holds at 48000 Hz; skipping this track "
                "instead of reporting positions computed against the wrong grid")
            .arg(audioFilePath).arg(aStream->codecpar->sample_rate));
    return stats;   // empty: operation() reports this as "not scanned"
  }

  const AVCodec* codec = avcodec_find_decoder(aStream->codecpar->codec_id);
  if (!codec)
    return stats;

  codecCtx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecCtx, aStream->codecpar);
  codecCtx->thread_count = 1;   // deterministic, matches other decode paths in this codebase
  if (avcodec_open2(codecCtx, codec, nullptr) < 0)
    return stats;

  pkt   = av_packet_alloc();
  frame = av_frame_alloc();
  qint64 frameIdx = 0;
  bool loggedFormatWarning = false;

  auto appendStat = [&](AVFrame* f, bool decodeFailed) {
    FrameStat st{};
    st.is51 = false;
    st.lfeRms = -200.0f;
    st.centerRms = -200.0f;
    st.centerMaxDiff = 0.0f;

    if (!decodeFailed && f && f->ch_layout.nb_channels == 6) {
      if (f->format == AV_SAMPLE_FMT_FLTP) {
        const int centerIdx = av_channel_layout_index_from_channel(&f->ch_layout, AV_CHAN_FRONT_CENTER);
        const int lfeIdx    = av_channel_layout_index_from_channel(&f->ch_layout, AV_CHAN_LOW_FREQUENCY);
        if (centerIdx >= 0 && lfeIdx >= 0) {
          const float* c = reinterpret_cast<const float*>(f->extended_data[centerIdx]);
          const float* l = reinterpret_cast<const float*>(f->extended_data[lfeIdx]);
          const int ns = f->nb_samples;
          double cSumSq = 0.0, lSumSq = 0.0;
          float  maxDiff = 0.0f;
          for (int s = 0; s < ns; ++s) {
            cSumSq += double(c[s]) * double(c[s]);
            lSumSq += double(l[s]) * double(l[s]);
            if (s > 0) maxDiff = qMax(maxDiff, std::fabs(c[s] - c[s - 1]));
          }
          const double cRms = ns > 0 ? std::sqrt(cSumSq / ns) : 0.0;
          const double lRms = ns > 0 ? std::sqrt(lSumSq / ns) : 0.0;
          st.centerRms     = cRms > 1e-9 ? float(20.0 * std::log10(cRms)) : -180.0f;
          st.lfeRms        = lRms > 1e-9 ? float(20.0 * std::log10(lRms)) : -180.0f;
          st.centerMaxDiff = maxDiff;
          st.is51 = true;
        }
      } else if (!loggedFormatWarning) {
        // AC3's libav decoder emits planar float; anything else is an
        // upstream change this scan does not understand yet. Not a decode
        // failure (the frame is valid audio) - just unusable for the 5.1
        // channel-plane math, so it is treated like a non-5.1 frame.
        loggedFormatWarning = true;
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("TTAudioAnomalyScanTask: unexpected sample format %1 - "
                    "5.1 frames in this format are skipped")
                .arg(av_get_sample_fmt_name(static_cast<AVSampleFormat>(f->format))));
      }
    }

    if (decodeFailed) ++failures;
    stats.append(st);
    ++frameIdx;
    if (onFrame) onFrame(frameIdx);
  };

  bool aborted = false;
  while (!aborted && av_read_frame(fmtCtx, pkt) >= 0) {
    if (shouldAbort && shouldAbort()) { av_packet_unref(pkt); break; }
    if (pkt->stream_index == audioIdx) {
      if (avcodec_send_packet(codecCtx, pkt) >= 0) {
        while (avcodec_receive_frame(codecCtx, frame) >= 0) {
          appendStat(frame, false);
          av_frame_unref(frame);
          if (shouldAbort && shouldAbort()) { aborted = true; break; }
        }
      } else {
        // Could not even submit the packet - count it and keep the frame
        // index aligned with the source's AC3 frame numbering (one packet
        // == one AC3 frame for this codec).
        appendStat(nullptr, true);
      }
    }
    av_packet_unref(pkt);
    if (shouldAbort && shouldAbort()) aborted = true;
  }

  if (!aborted) {
    avcodec_send_packet(codecCtx, nullptr);
    while (avcodec_receive_frame(codecCtx, frame) >= 0) {
      appendStat(frame, false);
      av_frame_unref(frame);
    }
  }

  return stats;
}

// ---------------------------------------------------------------------------
// operation() — decode, evaluate, translate findings to video-frame markers.
// ---------------------------------------------------------------------------
void TTAudioAnomalyScanTask::operation()
{
  onStatusReport(StatusReportArgs::Start,
      tr("Scanning audio track %1 for anomalies...").arg(mTrackIndex + 1), 0);

  int decodeFailures = 0;
  qint64 lastReported = 0;
  QVector<FrameStat> stats = collectFrameStats(
      mAudioFilePath, &decodeFailures,
      [this]() { return mIsAborted.load(); },
      [this, &lastReported](qint64 idx) {
        // ~32 ms per frame -> one Step report roughly every 2 s of audio.
        if (idx - lastReported >= 64) {
          lastReported = idx;
          onStatusReport(StatusReportArgs::Step,
              tr("Scanning audio for anomalies..."), quint64(idx));
        }
      });

  if (mIsAborted) {
    // Partial results are deliberately discarded: pointsDetected only
    // fires here, at the very end of operation() - a cancel must not look
    // like a completed scan with a short list (spec Komponente 1,
    // Sichtbarkeit).
    onStatusReport(StatusReportArgs::Finished,
        tr("Audio anomaly scan cancelled"), quint64(stats.size()));
    emit pointsDetected(QList<TTStreamPoint>());
    return;
  }

  if (stats.isEmpty()) {
    // Either the track could not be decoded at all, or collectFrameStats
    // refused it (sample rate other than 48 kHz - it logs the reason itself).
    log->warningMsg(__FILE__, __LINE__,
        QString("TTAudioAnomalyScanTask: no frame statistics for %1 - see the preceding "
                "log line for the reason (undecodable track, or a sample rate the 32 ms "
                "AC3 frame grid does not apply to)").arg(mAudioFilePath));
    onStatusReport(StatusReportArgs::Finished,
        tr("Audio anomaly scan not run: track could not be decoded, or its sample "
           "rate is not 48 kHz (see the log)"), 0);
    emit pointsDetected(QList<TTStreamPoint>());
    return;
  }

  TTSettings* cfg = TTSettings::instance();
  GateStatus gate;
  const QList<Finding> findings = evaluate(stats,
      cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
      cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb(), &gate);

  // The gate result is the only way to tell "material unsuitable, no
  // statement possible" apart from "material fine, nothing found" - an
  // empty findings list looks identical for both otherwise. The spec
  // explicitly calls for a log line on the unsuitable path.
  if (gate.materialUnsuitable) {
    log->infoMsg(__FILE__, __LINE__,
        QString("Audio anomaly scan track %1: LFE not predominantly silent "
                "(%2%% null over %3 5.1 frames, need >= %4%%) - no anomaly "
                "statement possible for this track")
            .arg(mTrackIndex + 1)
            .arg(QString::number(gate.lfeNullPercent, 'f', 1))
            .arg(gate.frames51)
            .arg(QString::number(cfg->anomalyLfeNullPercent(), 'f', 1)));
  }

  QList<TTStreamPoint> points;
  for (const Finding& f : findings) {
    const double startSec = f.frameFrom * kFrameDurSec;
    const double endSec   = (f.frameTo + 1) * kFrameDurSec;
    const int videoFrom = videoFrameForTime(startSec, mFrameRate, mExtraFrameIndices);
    const int videoTo   = videoFrameForTime(endSec,   mFrameRate, mExtraFrameIndices);

    QString desc = tr("Audio anomaly: C+LFE burst (track %1, LFE peak %2 dB)")
        .arg(mTrackIndex + 1)
        .arg(QString::number(f.lfePeak, 'f', 1));

    for (const auto& gap : mGapFrameRanges) {
      if (videoFrom <= gap.second && videoTo >= gap.first) {
        desc += tr(" (overlaps gap repair)");
        break;
      }
    }

    TTStreamPoint pt(videoFrom, StreamPointType::AudioAnomaly, desc,
                     f.confidence, float(endSec - startSec));
    // Hand the finding's own AC3 frame numbers through untouched (final
    // review I3). videoFrom/duration are a lossy projection onto the video
    // grid - three roundings deep by the time the repair dialog inverts
    // them - and the repair the user confirms is written in AC3 frames.
    // Both bounds inclusive, the same convention as TTAudioRepairItem.
    pt.setAudioFrameRange(f.frameFrom, f.frameTo);
    points.append(pt);
  }

  if (decodeFailures > 0) {
    log->infoMsg(__FILE__, __LINE__,
        QString("Audio anomaly scan: %1 of %2 AC3 frames on track %3 could not "
                "be decoded and were skipped")
            .arg(decodeFailures).arg(stats.size()).arg(mTrackIndex + 1));
  }
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "TTAudioAnomalyScanTask: track" << mTrackIndex << stats.size()
               << "AC3 frames," << decodeFailures << "decode failures,"
               << findings.size() << "finding(s)";

  onStatusReport(StatusReportArgs::Finished,
      tr("Audio anomaly scan complete: %n finding(s)", "", points.size()),
      quint64(stats.size()));

  emit pointsDetected(points);
}
