#ifndef TTAUDIOREPAIRITEM_H
#define TTAUDIOREPAIRITEM_H
#include <QString>
#include <QtGlobal>

// One planned repair on one audio track. Frame numbers are AC3 source frame
// indices (32 ms grid, frame k starts at byte k*1536 for 384 kbit/s 48 kHz).
// channelMask bits: 0=FL 1=FR 2=C 3=LFE 4=SL 5=SR (ffmpeg 5.1(side) order).
class TTAudioRepairItem
{
public:
  TTAudioRepairItem() = default;
  TTAudioRepairItem(int track, qint64 from, qint64 to, quint8 mask,
                    const QString& method = QStringLiteral("silence-fade"))
    : mTrack(track), mFrameFrom(from), mFrameTo(to),
      mChannelMask(mask), mMethod(method) {}

  int     trackIndex()  const { return mTrack; }
  qint64  frameFrom()   const { return mFrameFrom; }   // inclusive
  qint64  frameTo()     const { return mFrameTo; }     // inclusive
  quint8  channelMask() const { return mChannelMask; }
  QString method()      const { return mMethod; }
  bool    isEnabled()   const { return mEnabled; }
  void    setEnabled(bool e)       { mEnabled = e; }

private:
  int     mTrack = 0;
  qint64  mFrameFrom = 0;
  qint64  mFrameTo = 0;
  quint8  mChannelMask = 0;
  QString mMethod = QStringLiteral("silence-fade");
  bool    mEnabled = true;   // load validation can disable, never silently drop
};
#endif
