/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
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
         mType == StreamPointType::AspectChange;
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
  return StreamPointType::ManualMarker;
}
