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
// Owns the file probe and the frame index bundle and answers every per-frame
// question (random access, IDR, coding type) from the bundle. The derived
// classes supply the codec identity and, for H.264, the PAFF accessors.
// ----------------------------------------------------------------------------

#ifndef TTH26XVIDEOSTREAM_H
#define TTH26XVIDEOSTREAM_H

#include "ttavstream.h"
#include "ttavutil.h"
#include "ttframeindex.h"
#include "ttframeindexer.h"
#include "../common/ttmessagelogger.h"

#include <QFileInfo>
#include <QString>

class TTCutParameter;

class TTH26xVideoStream : public TTVideoStream
{
    Q_OBJECT

public:
    explicit TTH26xVideoStream(const QFileInfo& fInfo);
    ~TTH26xVideoStream() override = default;

    // From TTAVStream / TTVideoStream
    float frameRate() override;

    // Probe result of the best video stream (resolution, profile, level).
    const TTStreamInfo& streamInfo() const { return mProbe.info; }
    // Where frameRate() came from (.info, SPS timing, or assumed 25).
    TTFrameRateOrigin frameRateOrigin() const { return mFrameRateOrigin; }
    virtual const char* codecLabel() const = 0;          // "H.264" / "H.265"

    // From TTAVStream
    int  createHeaderList() override;
    int  createIndexList() override;
    void cut(int start, int end, TTCutParameter* cp) override;

    bool isCutInPoint(int pos) override;
    bool isCutOutPoint(int pos) override;

    int findIDRBefore(int frameIndex) override;

    int decodeToDisplayIndex(int index) const override;
    int displayToDecodeIndex(int index) const override;

    // Display-order map (POC-based, frame granularity) from the index bundle.
    // Injected into TTESSmartCut so cut positions map display->AU consistently
    // (esp. PAFF, where buildFromFile's field-granularity fallback would
    // mismatch the parser's frame count).
    const TTDisplayOrderMap& displayOrderMap() const;

    // --- Canonical frame-index owner ("Owner A") ---
    // This stream builds the frame index ONCE at stream-open (createHeaderList).
    // Every other wrapper of the same file adopts it through
    // TTFFmpegWrapper::setFrameIndex(frameIndexBundle()) instead of rescanning
    // (~2 s/scan): quick jump (ttquickjumpdialog.cpp), the preview window
    // (ttmpeg2window2.cpp; Black/Scene/Logo search and the analysis wrappers
    // pull transitively from there) and the frame search (ttframesearchtask.cpp).
    // An empty bundle means "not built yet" — the caller then runs a
    // TTFrameIndexer itself. See specs 2026-06-05-frame-index-unification,
    // 2026-08-28-frame-index-bundle and 2026-09-03-stream-ownership.
    //
    // Consumers that hand an index across a thread or object boundary MUST use
    // this bundle, never the bare list — see TTFrameIndexBundle.
    const TTFrameIndexBundle& frameIndexBundle() const { return mFrameIndexBundle; }

    // Raw->merged AU translation for .info doubled-PTS candidates (raw AU
    // numbering; see the TTFFmpegWrapper map doc). Display index is -1 for
    // merged frames without a display slot (dropped HEVC RASL pics).
    int  rawAuCount() const;
    int  mapRawAuToDisplayIndex(int raw) const;
    bool rawAuIsCollapsedField(int raw) const;

protected:
    // Probes the file once (ttProbeVideo) and checks expectedCodec(); called
    // from createHeaderList.
    bool openStream();

    // Hook implemented by derived
    virtual TTVideoCodecType expectedCodec() const = 0;

private:
    // Per decode-order access unit, from mFrameIndexBundle.index.
    // Random access = the libav key flag: H.264 IDR or recovery point, HEVC
    // IRAP (IDR, CRA, BLA). IDR = the NAL scan (TTFrameInfo::isIDR).
    int  accessUnitCount() const { return mFrameIndexBundle.index.size(); }
    bool accessUnitIsRAP(int idx) const;
    int  accessUnitCodingType(int idx) const;          // 1=I, 2=P, 3=B

protected:
    // Result of openStream(): codec type, best video stream and its stream
    // info. mProbed guards against probing twice.
    TTVideoProbe mProbe;
    bool         mProbed = false;
    // The canonical index for this file, built once by TTFrameIndexer in
    // createHeaderList(): entries, GOP table, raw->merged map, PAFF metadata
    // and the display-order map. Adopters receive a copy (Qt COW).
    TTFrameIndexBundle mFrameIndexBundle;
    TTFrameRateOrigin  mFrameRateOrigin = TTFrameRateOrigin::Assumed;
    TTMessageLogger* mLog;
};

#endif // TTH26XVIDEOSTREAM_H
