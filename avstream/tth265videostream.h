/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth265videostream.h                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOSTREAM
// H.265/HEVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/

#ifndef TTH265VIDEOSTREAM_H
#define TTH265VIDEOSTREAM_H

#include "tth26xvideostream.h"
#include "tth265videoheader.h"

#include <QString>
#include <QFileInfo>
#include <QList>

class TTCutParameter;

class TTH265VideoStream : public TTH26xVideoStream
{
    Q_OBJECT

public:
    explicit TTH265VideoStream(const QFileInfo& fInfo);
    virtual ~TTH265VideoStream();

    virtual TTAVTypes::AVStreamType streamType() const override;

    // Typed accessors
    TTH265SPS*        getSPS() const { return mSPS; }
    TTH265VPS*        getVPS() const { return mVPS; }
    TTH265AccessUnit* frameAt(int index);
    int               findRAPBefore(int frameIndex);
    int               findRAPAfter(int frameIndex);

protected:
    // Hooks
    TTVideoCodecType expectedCodec() const override;
    const char*      codecLabel() const override { return "H.265"; }

    void    resetSPS() override;
    void    buildSPSFromStreamInfo(const TTStreamInfo& info) override;
    void    setSPSFrameRate(double fps) override;
    QString spsDescription() const override;

    void    buildAccessUnits() override;
    int     accessUnitCount() const override { return mAccessUnits.size(); }
    bool    accessUnitIsIDR(int idx) const override;
    bool    accessUnitIsRAP(int idx) const override;
    int     accessUnitToCodingType(int idx) const override;

    static bool isRAPNalType(int nalType);

private:
    TTH265SPS* mSPS;
    TTH265VPS* mVPS;
    QList<TTH265AccessUnit*> mAccessUnits;
};

#endif // TTH265VIDEOSTREAM_H
