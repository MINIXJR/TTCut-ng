#ifndef TTDONORFILL_H
#define TTDONORFILL_H
#include <QString>
#include <QVector>

// Filling a hole in the sound from a second ("donor") track of the same
// recording (spec 2026-10-10): the search for the hole, the fit of the donor,
// the cross-fade and the reader of the donor's PCM. Positions are samples of
// a track: frame number x samples per frame + n, in the decode of that file.
namespace TTDonorFill {

using Planes = QVector<QVector<float>>;

int crossFadeSamples(int sampleRate);   // 2 ms: original <-> donor on each side of the hole
int guardSamples(int sampleRate);       // 5 ms: distance of the compared sound from the hole
int compareSamples(int sampleRate);     // 150 ms: compared sound on each side
int searchSamples(int sampleRate);      // 50 ms: the shift is searched this far around the expected one
int minHoleSamples(int sampleRate);     // 2 ms: a shorter quiet run is no hole

struct HolePlacement { bool found = false; qint64 start = 0; qint64 end = 0; };   // [start, end)

// Pure. planes: the channels to fill, sample 0 of each at track sample
// firstPos. Per plane: the block boundary (256-sample grid of the anomaly
// scan) with the largest drop inside [from, to), then the longest run of
// samples at or below the level before it minus 30 dB. A run shorter than
// 2 ms or touching from/to is no hole. Several planes: from the earliest
// start to the latest end of those that have one.
HolePlacement locateHole(const Planes& planes, qint64 firstPos, qint64 from, qint64 to, int sampleRate);

struct DonorFit { bool valid = false; qint64 shift = 0; double match = 0.0; QVector<double> gains; };

// Pure. target/donor: one plane per filled channel; sample 0 at track sample
// targetPos resp. donor sample donorPos. Compares the 150 ms before
// holeStart - 5 ms and behind holeEnd + 5 ms at every shift within 50 ms of
// expectedShift (shift = donor position - track position). The shift with
// the best correlation over all planes wins; gains: least squares per plane,
// never negative; match: that correlation, 0..1. valid == false: the compared
// sound is not inside the planes for every shift of the search.
DonorFit fitDonor(const Planes& target, qint64 targetPos, const Planes& donor, qint64 donorPos,
                  qint64 holeStart, qint64 holeEnd, qint64 expectedShift, int sampleRate);

// Share of the donor at track sample pos: 0 outside, a raised cosine up over
// the crossFade samples before the hole, 1 inside, down over those behind it.
double donorWeight(qint64 pos, qint64 holeStart, qint64 holeEnd, int crossFade);

// Writes the fill into one channel. data[n] is track sample pos0 + n;
// donor[i] is the donor's sound for track sample fillFirst + i.
void applyDonor(float* data, int count, qint64 pos0, const QVector<float>& donor, qint64 fillFirst,
                qint64 holeStart, qint64 holeEnd, int crossFade, double gain);

// The donor's PCM for its samples [firstPos, firstPos + count): planes L, R.
// The track must be MP2 or AC3 and two-channel in that range. A frame's
// position comes from where it lies in the file, not from counting packets:
// libavformat skips the first MP2 frame when its header differs from the
// second, which would move every counted position by one frame.
// noSound: the file is readable, but the range is not there as two-channel
// sound (before the start, past the end, another channel count).
struct DonorPcm { bool ok = false; bool noSound = false; Planes planes; int sampleRate = 0; QString error; };
DonorPcm readDonorRange(const QString& donorFile, qint64 firstPos, qint64 count);
}
#endif
