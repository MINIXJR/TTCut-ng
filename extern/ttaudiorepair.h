#ifndef TTAUDIOREPAIR_H
#define TTAUDIOREPAIR_H
#include <QMap>
#include <QByteArray>
#include <QString>
#include <QVector>
#include "ttaudiorepairitem.h"

struct AVFormatContext;

namespace TTAudioRepair {

// Replacement table: AC3 source frame number -> ready-to-write frame bytes.
using FrameTable = QMap<qint64, QByteArray>;

constexpr int kAc3FrameSamples = 1536;

// Open audioFile with libavformat and find its first audio stream. On
// success *fmtCtx is open (caller closes it) and audioIdx set; on failure
// *fmtCtx is null and *error says why. Shared by buildRepairTable and the
// repair dialog's preview writer.
bool openFirstAudioStream(const QString& audioFile, AVFormatContext** fmtCtx, int* audioIdx, QString* error);

// Build replacement frames for one repair item. Decodes the item's source
// frames, edits them by the item's method and re-encodes them through
// TTAc3Reencoder: with the source frame's bit rate and header fields,
// sample-aligned, in the given target acmod (-1 = keep the source channel
// layout). "silence-fade" silences the masked channels (5 ms raised-cosine
// fades at range start/end); "fade-out" multiplies every channel by
// fadeOutGain; "donor-fill" cross-fades the masked channels to the sound of
// donorFile over the item's hole (the other methods ignore donorFile). On
// failure returns an empty table and sets errorOut — callers MUST treat that
// as abort-the-cut, never as skip-the-repair (spec: Fehlerbild Punkt 2).
FrameTable buildRepairTable(const QString& audioFile,
                            const TTAudioRepairItem& item,
                            int targetAcmod,
                            QString* errorOut,
                            const QString& donorFile = QString());

// The short fade of both methods: 5 ms in samples (240 at 48 kHz).
int fadeLenSamples(int sampleRate);

// Gain of the fade-out repair at track sample pos: 1 before the fade, a
// raised cosine down to 0 over fadeLen samples that ends at fadeEnd, 0 for
// silenceLen samples, a raised cosine back to 1 over fadeInLen samples.
double fadeOutGain(qint64 pos, qint64 fadeEnd, int fadeLen, int silenceLen, int fadeInLen);

// The repair item of a fade-out. Its frame range is every AC3 frame the curve
// touches; frameFrom() is -1 when the fade would start before the track.
TTAudioRepairItem makeFadeOutItem(int track, quint8 channelMask, qint64 fadeEnd,
                                  int fadeLen, int silenceLen, int sampleRate);

// Where a fade-out belongs at an abrupt stop. soundEnd: the first sample of
// the quiet part. fadeEnd: the earliest jump in the 15 ms before it, else
// soundEnd. silence: soundEnd - fadeEnd + 1 ms.
struct StopPlacement { bool found = false; qint64 fadeEnd = 0; qint64 soundEnd = 0; int silence = 0; };

// Pure. planes: the main channels (no LFE), sample 0 of each at track sample
// firstPos. The stop is searched in [from, to); the planes should reach one
// frame further on both sides.
StopPlacement locateStop(const QVector<QVector<float>>& planes, qint64 firstPos,
                         qint64 from, qint64 to, int sampleRate);

// Decodes frames frameFrom-1 .. frameTo+1 exactly as buildRepairTable does
// and runs locateStop on [frameFrom, frameTo]. An error (file, layout change
// inside the frames) leaves found == false and sets *errorOut.
StopPlacement findStop(const QString& audioFile, qint64 frameFrom, qint64 frameTo, QString* errorOut);

// Donor fill (spec 2026-10-10). The channels it fills, as a channel mask:
// the centre of a 3/2 stream, left+right of a 2/0 stream.
constexpr quint8 kDonorFillMaskCentre = 0x04;
constexpr quint8 kDonorFillMaskStereo = 0x03;
constexpr double kDonorFillMaxGain    = 100.0;

// The repair item of a donor fill. Its frame range is every AC3 frame the
// hole and its 2 ms cross-fades touch; frameFrom() is -1 when the fill would
// start before the track.
TTAudioRepairItem makeDonorFillItem(int track, quint8 channelMask, int donorTrack, qint64 holeStart, qint64 holeEnd,
                                    qint64 donorShift, const QVector<double>& gains, double match, int sampleRate);

// Why a donor-fill item cannot be applied, or empty: mask, number and range
// of the gains, the hole, and the frame range its values give at sampleRate
// (the repaired track's).
QString donorFillProblem(const TTAudioRepairItem& item, int sampleRate);

// What the search for a donor fill found at a hole marker.
//   NoHole:            no closed quiet run of 2 ms in the filled channels
//   NoSound:           too little sound next to the hole to compare - the hole
//                      lies at the start or end of the track, or the donor has
//                      no two-channel sound there
//   FormatChange:      channel layout or bit rate changes in the frames needed
//   UnsupportedLayout: the track is neither 3/2 nor 2/0 there
//   DonorUnreadable:   the donor file cannot be read, or does not start on a
//                      frame; error says why
//   Error:             the track itself could not be read; error says why
enum class DonorFillStatus { Found, NoHole, NoSound, FormatChange, UnsupportedLayout, DonorUnreadable, Error };
struct DonorFillSearch {
    DonorFillStatus status = DonorFillStatus::Error;
    TTAudioRepairItem item;     // for Found: the repair, ready for buildRepairTable
    QString error;              // technical detail for the log
};

// Searches the hole in the marker's frames frameFrom..frameTo of audioFile
// (and one frame on each side), fits donorFile to the sound around it and
// returns the repair. expectedShift: where the donor is expected, as donor
// position minus track position in samples (from the start offsets of both
// tracks); the shift is searched 50 ms around it.
DonorFillSearch findDonorFill(const QString& audioFile, int track, qint64 frameFrom, qint64 frameTo,
                              const QString& donorFile, int donorTrack, qint64 expectedShift);
}
#endif
