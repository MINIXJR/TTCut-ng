/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAC3REENCODER_H
#define TTAC3REENCODER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>
#include <functional>

struct AVCodecParameters;
struct AVFrame;
struct AVPacket;

// Header fields (BSI) of one AC3 frame, as far as an encoder can be given
// them. -1: the frame does not carry the field.
struct TTAc3FrameMeta
{
    int  bsid = 8;
    int  bsmod = 0;
    int  acmod = 2;
    bool lfeon = false;
    int  cmixlev = -1;
    int  surmixlev = -1;
    int  dsurmod = -1;
    int  dialnorm = -31;       // dB, -31..-1
    int  mixlevel = -1;        // audio production info, 0..31
    int  roomtyp = -1;         // audio production info, 0..3
    int  copyright = 0;
    int  origbs = 1;
    int  dmixmod = -1;         // xbsi1 (bsid 6)
    int  ltrtcmixlev = -1;
    int  ltrtsurmixlev = -1;
    int  lorocmixlev = -1;
    int  lorosurmixlev = -1;
    int  dsurexmod = -1;       // xbsi2 (bsid 6)
    int  dheadphonmod = -1;
    int  adconvtyp = -1;
};

// Reads the BSI of the AC3 frame that starts at data (sync word 0B 77,
// bsid <= 8). False when the bytes are no such frame header. For dual mono
// (acmod 0) the first channel's dialnorm and production info are returned.
bool ttParseAc3FrameMeta(const uint8_t* data, int size, TTAc3FrameMeta* meta);

// Re-encodes AC3 frames so that each replacement plays like the frame it
// replaces between stream-copied neighbours:
//  - the decoder runs without the stream's dynamic range compression
//    (drc_scale 0; ffmpeg's AC3 encoder cannot write compression words, so
//    compression applied here would be baked into the audio);
//  - the encoder gets the header fields of the source frame;
//  - the audio sits at the same place: the AC3 encoder delays its input by
//    256 samples, so each run feeds it 1280 zero samples first, discards the
//    first packet and completes the last frame with the 256 samples that
//    follow the run.
// A replacement is therefore finished one frame late.
class TTAc3Reencoder
{
public:
    struct Request {
        int    targetAcmod = -1;   // -1: keep the source layout; 6, 7: 5.1; other: stereo
        qint64 bitRate = 0;        // 0: from the source frame's size
    };
    struct Replacement {
        qint64     tag;            // as given to push()
        QByteArray bytes;
    };
    // Edits the decoded PCM (source layout, AV_SAMPLE_FMT_FLTP) of a frame
    // that is about to be re-encoded. False with *error set fails the push.
    using PcmEdit = std::function<bool(AVFrame* frame, QString* error)>;

    TTAc3Reencoder();
    ~TTAc3Reencoder();
    TTAc3Reencoder(const TTAc3Reencoder&) = delete;
    TTAc3Reencoder& operator=(const TTAc3Reencoder&) = delete;

    bool open(const AVCodecParameters* par, QString* error);

    // Every source packet of a contiguous stretch, in order.
    //   reencode == false: the frame is remembered; it is decoded only when
    //   a neighbouring run uses it (as warm-up before a run - optional, see
    //   push() - or as the lookahead behind a run, which is needed).
    //   reencode == true: the frame is to be replaced.
    // Finished replacements are appended to out in the order their frames
    // were pushed. After a false return call reset() before pushing again.
    bool push(const AVPacket* pkt, bool reencode, const Request& req, qint64 tag,
              const PcmEdit& edit, QList<Replacement>* out, QString* error);

    // Nothing follows (end of file): completes a pending run with silence.
    bool finish(QList<Replacement>* out, QString* error);

    // The source position jumps (seek). Drops a pending run; call it only
    // after finish() or after a push with reencode == false.
    void reset();

private:
    struct Private;
    Private* d;
};

#endif // TTAC3REENCODER_H
