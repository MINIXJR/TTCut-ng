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
#include "../extern/ttframeindex.h"
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

    // Display-order map (POC-based, frame granularity) from the open stream's
    // wrapper. Used to inject into TTESSmartCut so cut positions map display->AU
    // consistently (esp. PAFF, where buildFromFile's field-granularity fallback
    // would mismatch the parser's frame count).
    const TTDisplayOrderMap& displayOrderMap() const;

    // --- Canonical frame-index owner ("Owner A") ---
    // This stream builds the FFmpeg frame index ONCE at stream-open
    // (createHeaderList). Other wrappers of the same file adopt it instead of
    // rescanning themselves (~2 s/scan). Every adopter takes the index as a
    // TTFrameIndexBundle, so the stream metadata cannot be left behind:
    //   - Quickjump (ttquickjumpdialog.cpp) via ffmpegFrameIndexBundle().
    //   - mpegWindow (ttmpeg2window2.cpp) via provideFrameIndexTo();
    //     Black/Scene/Logo search + analysisWrapper pull transitively from there.
    //   - framesearch (ttframesearchtask.cpp) via provideFrameIndexTo().
    // See specs 2026-06-05-frame-index-unification-design.md and
    // 2026-08-28-frame-index-bundle-design.md.
    //
    // The index together with the metadata the indexer measured. Consumers
    // that hand an index across a thread or object boundary MUST use this
    // bundle, never the bare list — see TTFrameIndexBundle.
    TTFrameIndexBundle ffmpegFrameIndexBundle() const;

    // Hands this stream's already-built index (Owner A) to `consumer`, which has
    // opened the SAME file. File identity is guaranteed by the caller through
    // object identity (it holds this stream object).
    //   true  = adopted → consumer needs no index of its own.
    //   false = index still empty/not built → caller must run a
    //           TTFrameIndexer itself and install the result.
    bool provideFrameIndexTo(TTFFmpegWrapper* consumer) const;

    // Raw->merged AU translation for .info doubled-PTS candidates (raw AU
    // numbering; see the TTFFmpegWrapper map doc). Display index is -1 for
    // merged frames without a display slot (dropped HEVC RASL pics).
    int  rawAuCount() const;
    int  mapRawAuToDisplayIndex(int raw) const;
    bool rawAuIsCollapsedField(int raw) const;

protected:
    // ffmpeg lifecycle (called from createHeaderList)
    bool openStream();

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
    // The canonical index for this file, built once by TTFrameIndexer in
    // createHeaderList(). The wrapper gets a copy through setFrameIndex(), but
    // this stream stays the owner: only the bundle kept here carries the GOP
    // table and the raw->merged map (a wrapper re-exporting an adopted index
    // leaves both empty).
    TTFrameIndexBundle mFrameIndexBundle;
    TTMessageLogger* mLog;
};

#endif // TTH26XVIDEOSTREAM_H
