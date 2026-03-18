/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_cutderivation.h"
#include "../avstream/ttavstream.h"

#include <QDebug>
#include <algorithm>

QList<TTDerivedCutPair> TTStreamPointCutDerivation::deriveCutPairs(
    const QList<TTStreamPoint>& points,
    float frameRate,
    float minDistance,
    TTVideoStream* videoStream)
{
  QList<TTDerivedCutPair> result;

  if (points.isEmpty() || frameRate <= 0 || !videoStream)
    return result;

  // 1. Build clusters from temporally close detections
  QList<Cluster> clusters = buildClusters(points, frameRate, minDistance);

  if (clusters.isEmpty())
    return result;

  qDebug() << "CutDerivation:" << clusters.size() << "clusters from" << points.size() << "points";

  // 2. Classify clusters using audio format direction
  //    5.1→2.0 = ad break start = Cut-Out
  //    2.0→5.1 = ad break end   = Cut-In
  //    No audio change: alternating pattern starting with Cut-Out
  QList<QPair<int, bool>> classified;  // (cluster index, isCutIn)

  for (int i = 0; i < clusters.size(); i++) {
    const Cluster& c = clusters.at(i);

    if (c.hasAudioChange) {
      if (c.audioChangeDirection > 0) {
        // To surround = program resumes = Cut-In
        classified.append(qMakePair(i, true));
      } else if (c.audioChangeDirection < 0) {
        // To stereo = ad starts = Cut-Out
        classified.append(qMakePair(i, false));
      } else {
        // Unknown direction, use alternating
        bool isCutIn = (classified.isEmpty() || !classified.last().second);
        classified.append(qMakePair(i, isCutIn));
      }
    } else {
      // No audio change — use alternating pattern (first = Cut-Out)
      bool isCutIn = (!classified.isEmpty() && !classified.last().second);
      classified.append(qMakePair(i, isCutIn));
    }
  }

  // 3. Build cut pairs from classified clusters
  //    Match Cut-Out→Cut-In pairs
  int pendingCutOut = -1;
  QString pendingReason;

  for (int i = 0; i < classified.size(); i++) {
    int clusterIdx = classified.at(i).first;
    bool isCutIn = classified.at(i).second;
    const Cluster& c = clusters.at(clusterIdx);

    // Build reason string
    QStringList reasons;
    if (c.hasBlack) reasons << "Black";
    if (c.hasSilence) reasons << "Silence";
    if (c.hasAudioChange) reasons << "Audio";
    if (c.hasSceneChange) reasons << "Scene";
    QString reason = reasons.join(" + ");

    // Frame index = center of cluster
    int frameIdx = qRound((c.startTimeSec + c.endTimeSec) / 2.0f * frameRate);

    if (!isCutIn) {
      // Cut-Out
      pendingCutOut = snapToCutOutFrame(frameIdx, videoStream);
      pendingReason = reason;
    } else if (pendingCutOut >= 0) {
      // Cut-In (and we have a pending Cut-Out)
      int cutIn = snapToCutInFrame(frameIdx, videoStream);
      if (cutIn > pendingCutOut) {
        TTDerivedCutPair pair;
        pair.cutOutFrame = pendingCutOut;
        pair.cutInFrame = cutIn;
        pair.reason = pendingReason + " | " + reason;
        result.append(pair);
      }
      pendingCutOut = -1;
    }
  }

  qDebug() << "CutDerivation:" << result.size() << "cut pairs derived";
  return result;
}

QList<TTStreamPointCutDerivation::Cluster> TTStreamPointCutDerivation::buildClusters(
    const QList<TTStreamPoint>& points,
    float frameRate,
    float minDistance)
{
  QList<Cluster> clusters;

  if (points.isEmpty()) return clusters;

  // Sort points by frame index (should already be sorted from model)
  Cluster current;
  const TTStreamPoint& first = points.first();
  float firstTime = first.frameIndex() / frameRate;
  current.startTimeSec = firstTime;
  current.endTimeSec = firstTime;
  current.points.append(&first);
  current.hasBlack = (first.type() == StreamPointType::BlackFrame);
  current.hasSilence = (first.type() == StreamPointType::Silence);
  current.hasAudioChange = (first.type() == StreamPointType::AudioChange);
  current.hasSceneChange = (first.type() == StreamPointType::SceneChange);
  current.audioChangeDirection = 0;

  if (current.hasAudioChange) {
    // Parse direction from description: "Audio 5.1 → 2.0" or "Audio 2.0 → 5.1"
    if (first.description().contains("5.1")) {
      if (first.description().indexOf("5.1") < first.description().indexOf("2.0"))
        current.audioChangeDirection = -1;  // 5.1→2.0 = to stereo
      else
        current.audioChangeDirection = +1;  // 2.0→5.1 = to surround
    }
  }

  for (int i = 1; i < points.size(); i++) {
    const TTStreamPoint& pt = points.at(i);
    float timeSec = pt.frameIndex() / frameRate;

    if (timeSec - current.endTimeSec <= minDistance) {
      // Belongs to current cluster
      current.endTimeSec = timeSec;
      current.points.append(&pt);
      if (pt.type() == StreamPointType::BlackFrame)  current.hasBlack = true;
      if (pt.type() == StreamPointType::Silence)     current.hasSilence = true;
      if (pt.type() == StreamPointType::AudioChange) {
        current.hasAudioChange = true;
        if (pt.description().contains("5.1") && pt.description().contains("2.0")) {
          if (pt.description().indexOf("5.1") < pt.description().indexOf("2.0"))
            current.audioChangeDirection = -1;
          else
            current.audioChangeDirection = +1;
        }
      }
      if (pt.type() == StreamPointType::SceneChange) current.hasSceneChange = true;
    } else {
      // New cluster
      clusters.append(current);

      current = Cluster();
      current.startTimeSec = timeSec;
      current.endTimeSec = timeSec;
      current.points.clear();
      current.points.append(&pt);
      current.hasBlack = (pt.type() == StreamPointType::BlackFrame);
      current.hasSilence = (pt.type() == StreamPointType::Silence);
      current.hasAudioChange = (pt.type() == StreamPointType::AudioChange);
      current.hasSceneChange = (pt.type() == StreamPointType::SceneChange);
      current.audioChangeDirection = 0;

      if (current.hasAudioChange && pt.description().contains("5.1") && pt.description().contains("2.0")) {
        if (pt.description().indexOf("5.1") < pt.description().indexOf("2.0"))
          current.audioChangeDirection = -1;
        else
          current.audioChangeDirection = +1;
      }
    }
  }

  clusters.append(current);
  return clusters;
}

int TTStreamPointCutDerivation::snapToCutInFrame(int frame, TTVideoStream* vs)
{
  // Cut-In must be on an I-frame
  // Search forward from frame for nearest I-frame
  if (frame < 0) frame = 0;
  if (frame >= vs->frameCount()) frame = vs->frameCount() - 1;

  // If already on I-frame, return
  if (vs->frameType(frame) == 1)  // 1 = I-frame
    return frame;

  // Search forward
  for (int i = frame + 1; i < vs->frameCount(); i++) {
    if (vs->frameType(i) == 1)
      return i;
  }

  return frame;  // fallback
}

int TTStreamPointCutDerivation::snapToCutOutFrame(int frame, TTVideoStream* vs)
{
  // Cut-Out must be on P-frame or I-frame
  if (frame < 0) frame = 0;
  if (frame >= vs->frameCount()) frame = vs->frameCount() - 1;

  int type = vs->frameType(frame);
  if (type == 1 || type == 2)  // I or P
    return frame;

  // Search backward for P or I frame
  for (int i = frame - 1; i >= 0; i--) {
    int t = vs->frameType(i);
    if (t == 1 || t == 2)
      return i;
  }

  return frame;  // fallback
}
