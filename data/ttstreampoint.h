/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_H
#define TTSTREAMPOINT_H

#include <QString>

enum class StreamPointType {
  ManualMarker = 0,
  VDRImportMarker,
  BlackFrame,
  Silence,
  AudioChange,
  SceneChange,
  AspectChange
};

class TTStreamPoint
{
public:
  TTStreamPoint();
  TTStreamPoint(int frameIndex, StreamPointType type,
                const QString& description,
                float confidence = 0.0f, float duration = 0.0f);

  int              frameIndex()  const { return mFrameIndex; }
  StreamPointType  type()        const { return mType; }
  QString          description() const { return mDescription; }
  float            confidence()  const { return mConfidence; }
  float            duration()    const { return mDuration; }

  void setFrameIndex(int index)              { mFrameIndex = index; }
  void setDescription(const QString& desc)   { mDescription = desc; }

  bool isAutoDetected() const;

  bool operator<(const TTStreamPoint& other) const;
  bool operator==(const TTStreamPoint& other) const;

  // Serialization helpers for .prj file
  static QString typeToString(StreamPointType type);
  static StreamPointType stringToType(const QString& str);

private:
  int              mFrameIndex;
  StreamPointType  mType;
  QString          mDescription;
  float            mConfidence;
  float            mDuration;
};

#endif // TTSTREAMPOINT_H
