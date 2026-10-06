#ifndef TTAUDIOREPAIRITEM_H
#define TTAUDIOREPAIRITEM_H
#include <QString>
#include <QtGlobal>

// One planned repair on one audio track. Frame numbers are AC3 source frame
// indices (32 ms grid, frame k starts at byte k*1536 for 384 kbit/s 48 kHz).
// channelMask bits: 0=FL 1=FR 2=C 3=LFE 4=SL 5=SR (ffmpeg 5.1(side) order);
// the method "fade-out" ignores it (its curve applies to every channel).
class TTAudioRepairItem
{
public:
  static constexpr const char* kMethodSilenceFade = "silence-fade";
  static constexpr const char* kMethodFadeOut     = "fade-out";

  TTAudioRepairItem() = default;
  TTAudioRepairItem(int track, qint64 from, qint64 to, quint8 mask,
                    const QString& method = QString::fromLatin1(kMethodSilenceFade))
    : mTrack(track), mFrameFrom(from), mFrameTo(to),
      mChannelMask(mask), mMethod(method) {}

  int     trackIndex()  const { return mTrack; }
  qint64  frameFrom()   const { return mFrameFrom; }   // inclusive
  qint64  frameTo()     const { return mFrameTo; }     // inclusive
  quint8  channelMask() const { return mChannelMask; }
  QString method()      const { return mMethod; }
  bool    isEnabled()   const { return mEnabled; }
  void    setEnabled(bool e)       { mEnabled = e; }
  void    setTrackIndex(int track) { mTrack = track; }

  // Method "fade-out": all channels fade to 0 before an abrupt stop. Values
  // in samples of the track: the gain reaches 0 at fadeEnd after fadeLength
  // samples of fading, silenceLength samples of silence follow. frameFrom/
  // frameTo are every frame that curve touches (TTAudioRepair::makeFadeOutItem).
  bool    isFadeOut()     const { return mMethod == QLatin1String(kMethodFadeOut); }
  qint64  fadeEnd()       const { return mFadeEnd; }
  int     fadeLength()    const { return mFadeLength; }
  int     silenceLength() const { return mSilenceLength; }
  void    setFadeOut(qint64 fadeEnd, int fadeLength, int silenceLength)
  {
    mMethod = QString::fromLatin1(kMethodFadeOut);
    mFadeEnd = fadeEnd; mFadeLength = fadeLength; mSilenceLength = silenceLength;
  }

private:
  int     mTrack = 0;
  qint64  mFrameFrom = 0;
  qint64  mFrameTo = 0;
  quint8  mChannelMask = 0;
  QString mMethod = QString::fromLatin1(kMethodSilenceFade);
  bool    mEnabled = true;   // load validation can disable, never silently drop
  qint64  mFadeEnd = 0;
  int     mFadeLength = 0;
  int     mSilenceLength = 0;
};
#endif
