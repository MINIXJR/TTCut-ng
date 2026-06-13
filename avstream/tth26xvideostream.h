/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH26XVIDEOSTREAM
// Abstract intermediate base shared by TTH264VideoStream and TTH265VideoStream.
// Owns the ffmpeg wrapper lifecycle and the codec-agnostic flow of
// createHeaderList / createIndexList / GOP forwarding. Codec-specific bits
// (typed SPS, typed access units, RAP-vs-IDR semantics, PAFF correction)
// are implemented by the derived classes via protected hooks.
// ----------------------------------------------------------------------------

#ifndef TTH26XVIDEOSTREAM_H
#define TTH26XVIDEOSTREAM_H

#include "ttavstream.h"
#include "../extern/ttffmpegwrapper.h"
#include "../common/ttmessagelogger.h"

#include <QFileInfo>
#include <QString>

class TTCutParameter;

class TTH26xVideoStream : public TTVideoStream
{
    Q_OBJECT

public:
    explicit TTH26xVideoStream(const QFileInfo& fInfo);
    virtual ~TTH26xVideoStream();

    // From TTAVStream / TTVideoStream
    float frameRate() override;

    // From TTAVStream
    int  createHeaderList() override;
    int  createIndexList() override;
    void cut(int start, int end, TTCutParameter* cp) override;

    bool isCutInPoint(int pos) override;
    bool isCutOutPoint(int pos) override;

    int findIDRBefore(int frameIndex) override;

    int decodeToDisplayIndex(int index) const override;
    int displayToDecodeIndex(int index) const override;

    // GOP forwarding — both H.264 and H.265 just delegate
    int gopCount() const;
    int findGOPForFrame(int frameIndex);
    int getGOPStart(int gopIndex);
    int getGOPEnd(int gopIndex);

    // Display-order map (POC-based, frame granularity) from the open stream's
    // wrapper. Used to inject into TTESSmartCut so cut positions map display->AU
    // consistently (esp. PAFF, where buildFromFile's field-granularity fallback
    // would mismatch the parser's frame count).
    const TTDisplayOrderMap& displayOrderMap() const;

    // --- Canonical frame-index owner ("Owner A") ---
    // This stream builds the FFmpeg frame index ONCE at stream-open
    // (createHeaderList). Other wrappers of the same file should adopt it instead
    // of rescanning themselves (~2 s/scan). Consumers:
    //   - Quickjump (ttquickjumpdialog.cpp) pulls directly via ffmpegFrameIndex().
    //   - mpegWindow (ttmpeg2window2.cpp) adopts via provideFrameIndexTo();
    //     Black/Scene/Logo search + analysisWrapper pull transitively from there.
    //   - framesearch (ttframesearchtask.cpp) adopts via provideFrameIndexTo().
    // See spec docs/superpowers/specs/2026-06-05-frame-index-unification-design.md
    const QList<TTFrameInfo>& ffmpegFrameIndex() const;

    // Hands this stream's already-built index (Owner A) to `consumer`, which has
    // opened the SAME file. File identity is guaranteed by the caller through
    // object identity (it holds this stream object).
    //   true  = adopted → consumer needs NO buildFrameIndex().
    //   false = index still empty/not built → caller must call
    //           consumer->buildFrameIndex() itself.
    bool provideFrameIndexTo(TTFFmpegWrapper* consumer) const;

protected:
    // ffmpeg lifecycle (called from createHeaderList)
    bool openStream();
    bool closeStream();

    // Hooks implemented by derived
    virtual TTVideoCodecType expectedCodec() const = 0;
    virtual const char*      codecLabel() const = 0;     // "H.264" / "H.265"

    virtual void    resetSPS() = 0;                       // delete + null typed mSPS
    virtual void    buildSPSFromStreamInfo(const TTStreamInfo& info) = 0;
    virtual void    setSPSFrameRate(double fps) = 0;
    virtual QString spsDescription() const = 0;           // for log line

    virtual void    buildAccessUnits() = 0;               // populate typed AU list from mFFmpeg->frameIndex()
    virtual int     accessUnitCount() const = 0;
    virtual bool    accessUnitIsIDR(int idx) const = 0;   // strict IDR (DPB reset)
    virtual bool    accessUnitIsRAP(int idx) const = 0;   // RAP (IDR plus CRA/BLA for H.265)
    virtual int     accessUnitToCodingType(int idx) const = 0; // 1=I, 2=P, 3=B for createIndexList

    virtual bool    isPAFFCorrectionApplicable() const { return false; }

protected:
    TTFFmpegWrapper* mFFmpeg;
    TTMessageLogger* mLog;
};

#endif // TTH26XVIDEOSTREAM_H
