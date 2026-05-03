/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                                */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth26xvideostream.h                                             */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH26XVIDEOSTREAM
// Abstract intermediate base shared by TTH264VideoStream and TTH265VideoStream.
// Owns the ffmpeg wrapper lifecycle and the codec-agnostic flow of
// createHeaderList / createIndexList / GOP forwarding. Codec-specific bits
// (typed SPS, typed access units, RAP-vs-IDR semantics, PAFF correction)
// are implemented by the derived classes via protected hooks.
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/

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

    // GOP forwarding — both H.264 and H.265 just delegate
    int gopCount() const;
    int findGOPForFrame(int frameIndex);
    int getGOPStart(int gopIndex);
    int getGOPEnd(int gopIndex);

    // Frame index forwarding
    const QList<TTFrameInfo>& ffmpegFrameIndex() const;

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
