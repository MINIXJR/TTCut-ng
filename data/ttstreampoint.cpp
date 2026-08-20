/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint.h"

TTStreamPoint::TTStreamPoint()
  : mFrameIndex(0),
    mType(StreamPointType::ManualMarker),
    mConfidence(0.0f),
    mDuration(0.0f)
{
}

TTStreamPoint::TTStreamPoint(int frameIndex, StreamPointType type,
                             const QString& description,
                             float confidence, float duration)
  : mFrameIndex(frameIndex),
    mType(type),
    mDescription(description),
    mConfidence(confidence),
    mDuration(duration)
{
}

bool TTStreamPoint::isAutoDetected() const
{
  return mType == StreamPointType::BlackFrame ||
         mType == StreamPointType::Silence ||
         mType == StreamPointType::AudioChange ||
         mType == StreamPointType::SceneChange ||
         mType == StreamPointType::AspectChange ||
         mType == StreamPointType::PillarboxChange ||
         mType == StreamPointType::AudioAnomaly;
}

bool TTStreamPoint::operator<(const TTStreamPoint& other) const
{
  return mFrameIndex < other.mFrameIndex;
}

bool TTStreamPoint::operator==(const TTStreamPoint& other) const
{
  return mFrameIndex == other.mFrameIndex && mType == other.mType;
}

QString TTStreamPoint::typeToString(StreamPointType type)
{
  switch (type) {
    case StreamPointType::ManualMarker:    return "ManualMarker";
    case StreamPointType::VDRImportMarker: return "VDRImportMarker";
    case StreamPointType::BlackFrame:      return "BlackFrame";
    case StreamPointType::Silence:         return "Silence";
    case StreamPointType::AudioChange:     return "AudioChange";
    case StreamPointType::SceneChange:     return "SceneChange";
    case StreamPointType::AspectChange:    return "AspectChange";
    case StreamPointType::PillarboxChange: return "PillarboxChange";
    case StreamPointType::Error:          return "Error";
    case StreamPointType::AudioAnomaly:   return "AudioAnomaly";
  }
  return "ManualMarker";
}

StreamPointType TTStreamPoint::stringToType(const QString& str)
{
  if (str == "VDRImportMarker") return StreamPointType::VDRImportMarker;
  if (str == "BlackFrame")      return StreamPointType::BlackFrame;
  if (str == "Silence")         return StreamPointType::Silence;
  if (str == "AudioChange")     return StreamPointType::AudioChange;
  if (str == "SceneChange")     return StreamPointType::SceneChange;
  if (str == "AspectChange")    return StreamPointType::AspectChange;
  if (str == "PillarboxChange") return StreamPointType::PillarboxChange;
  if (str == "Error")           return StreamPointType::Error;
  if (str == "AudioAnomaly")    return StreamPointType::AudioAnomaly;
  return StreamPointType::ManualMarker;
}

QStringList TTStreamPoint::repairPlannedSuffixVariants()
{
  // Source EN string (TTStreamPointWidget::onContextMenu()) plus every
  // shipped translation (trans/ttcut-ng_de_DE.ts, contexts TTCutMainWindow
  // and TTStreamPointWidget, both " (Reparatur geplant)"). Add a line here
  // whenever a new translation of that source string ships.
  static const QStringList variants = {
    QStringLiteral(" (repair planned)"),
    QStringLiteral(" (Reparatur geplant)")
  };
  return variants;
}

QStringList TTStreamPoint::repairDisabledSuffixVariants()
{
  // Source EN string (TTCutMainWindow::onStreamPointsLoaded()) plus every
  // shipped translation (trans/ttcut-ng_de_DE.ts, context TTCutMainWindow).
  static const QStringList variants = {
    QStringLiteral(" (repair DISABLED - it no longer fits the audio file)"),
    QStringLiteral(" (Reparatur ABGESCHALTET – sie passt nicht mehr zur Tondatei)")
  };
  return variants;
}
