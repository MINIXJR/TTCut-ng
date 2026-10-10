/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_H
#define TTSTREAMPOINT_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

enum class StreamPointType {
  ManualMarker = 0,
  VDRImportMarker,
  BlackFrame,
  Silence,
  AudioChange,
  SceneChange,
  AspectChange,
  PillarboxChange,
  Error,
  AudioAnomaly
};

// What an AudioAnomaly marker stands for. Unknown: every other marker type,
// and anomaly markers from project files written before the scan had a
// second search - those were findings of the LFE search.
enum class AudioAnomalyKind {
  Unknown = 0,
  LfeBurst,     // short burst in centre and LFE
  AbruptStop,   // a stop whose form was not recorded (projects of 2026-10-04/05)
  LastingStop,  // the sound of all channels stops and stays away
  Hole          // the sound is back within 32 ms
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
  // Duration in SECONDS, END-EXCLUSIVE: a marker covering audio frames
  // [from, to] (both inclusive) carries ((to + 1) - from) * frameDuration.
  // This convention is what makes frameIndex + duration convertible back to
  // an inclusive frame range at all - see audioFrameFrom()/audioFrameTo().
  float            duration()    const { return mDuration; }

  // Exact source AC3 frame range of an AudioAnomaly finding, BOTH BOUNDS
  // INCLUSIVE - the same convention TTAudioRepairItem::frameFrom()/frameTo()
  // uses, so the repair dialog can hand the scanner's own numbers straight
  // to a TTAudioRepairItem without a lossy detour.
  //
  // -1 on both means "not known" (every marker type other than AudioAnomaly,
  // and AudioAnomaly markers from a project file written before this field
  // existed). Consumers MUST fall back to the frameIndex/duration estimate
  // in that case; that estimate goes through three quantizations (video
  // frame rounding at 40 ms vs the 32 ms audio grid, duration rounded to two
  // decimals on save, ms<->frame rounding in the dialog) and lands within
  // about +/-1 AC3 frame - fine for "does a repair overlap this marker?",
  // not fine for what actually gets written.
  qint64           audioFrameFrom() const { return mAudioFrameFrom; }
  qint64           audioFrameTo()   const { return mAudioFrameTo; }
  bool             hasAudioFrameRange() const
                     { return mAudioFrameFrom >= 0 && mAudioFrameTo >= mAudioFrameFrom; }
  // Decoder planes a repair should preset for an AudioAnomaly marker (bit n =
  // plane n, the convention of TTAudioRepairItem::channelMask()). 0 = not
  // known: every other marker type, and markers from older project files.
  quint8           audioChannelMask() const { return mAudioChannelMask; }
  AudioAnomalyKind audioAnomalyKind() const { return mAudioAnomalyKind; }
  // Whether a new repair is offered for this marker: muting for an LFE
  // finding (and for a marker without a kind, which was one), a fade-out for
  // a lasting stop. Not for a stop whose form was not recorded. A hole is
  // filled from a second track; whether that is offered depends on the
  // item's tracks and is decided where the menu is built
  // (TTStreamPointWidget::buildContextMenu), so this says false for it. A
  // repair that already exists behind any marker stays editable and
  // removable.
  bool             offersNewAudioRepair() const
                     { return mType == StreamPointType::AudioAnomaly
                           && mAudioAnomalyKind != AudioAnomalyKind::AbruptStop
                           && mAudioAnomalyKind != AudioAnomalyKind::Hole; }

  void setFrameIndex(int index)              { mFrameIndex = index; }
  void setDescription(const QString& desc)   { mDescription = desc; }
  void setAudioFrameRange(qint64 from, qint64 to)
                     { mAudioFrameFrom = from; mAudioFrameTo = to; }
  void setAudioChannelMask(quint8 mask)      { mAudioChannelMask = mask; }
  void setAudioAnomalyKind(AudioAnomalyKind kind) { mAudioAnomalyKind = kind; }

  bool isAutoDetected() const;

  bool operator<(const TTStreamPoint& other) const;
  bool operator==(const TTStreamPoint& other) const;

  // Serialization helpers for .prj file
  static QString typeToString(StreamPointType type);
  static StreamPointType stringToType(const QString& str);
  // Project file text of an anomaly kind; Unknown has none (empty string).
  static QString anomalyKindToString(AudioAnomalyKind kind);
  static AudioAnomalyKind stringToAnomalyKind(const QString& str);

  // Known literal variants of the " (repair planned)" marker-description
  // suffix TTStreamPointWidget appends to an AudioAnomaly marker when a
  // repair is planned for it (source EN string plus every shipped
  // translation - currently just de_DE). The suffix is stored as plain text
  // inside TTStreamPoint::description() (persisted verbatim in the .ttcut
  // project file), so a marker touched in one UI language and reloaded or
  // re-touched in another carries whichever variant was active THEN, not
  // now. Checking only the current tr() result against it (residuals R6)
  // fails to recognize an existing suffix from a different language and
  // appends a second one instead of replacing it. Callers check every entry
  // through hasSuffixVariant()/stripSuffixVariant() below.
  // Kept here as literal strings (not tr()) deliberately: TTStreamPoint is
  // not a QObject, and the point is to match ANY known language, not just
  // whichever one tr() would resolve to right now.
  static QStringList repairPlannedSuffixVariants();
  // True when desc ends with one of the variants.
  static bool hasSuffixVariant(const QString& desc, const QStringList& variants);
  // Chop the first matching variant off desc; true when one was there.
  static bool stripSuffixVariant(QString& desc, const QStringList& variants);

  // Same idea as repairPlannedSuffixVariants(), for the two
  // " (repair DISABLED - ...)" suffixes of
  // TTStreamPointWidget::disabledRepairSuffix(), which
  // TTCutMainWindow::onStreamPointsLoaded() and the widget append. Without this, a project
  // reloaded in a different UI language than the one that last annotated it
  // would fail to recognize the stored suffix and append a SECOND one in
  // the new language on top of it.
  static QStringList repairDisabledSuffixVariants();

private:
  int              mFrameIndex;
  StreamPointType  mType;
  QString          mDescription;
  float            mConfidence;
  float            mDuration;
  qint64           mAudioFrameFrom = -1;   // inclusive, -1 = unknown
  qint64           mAudioFrameTo   = -1;   // inclusive, -1 = unknown
  quint8           mAudioChannelMask = 0;  // 0 = unknown
  AudioAnomalyKind mAudioAnomalyKind = AudioAnomalyKind::Unknown;
};

#endif // TTSTREAMPOINT_H
