/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth264videostream.h                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH264VIDEOSTREAM
// H.264/AVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/

#ifndef TTH264VIDEOSTREAM_H
#define TTH264VIDEOSTREAM_H

#include "tth26xvideostream.h"
#include "tth264videoheader.h"

#include <QString>
#include <QFileInfo>
#include <QList>

class TTCutParameter;

class TTH264VideoStream : public TTH26xVideoStream
{
    Q_OBJECT

public:
    explicit TTH264VideoStream(const QFileInfo& fInfo);
    virtual ~TTH264VideoStream();

    // Stream identity
    virtual TTAVTypes::AVStreamType streamType() const override;
    virtual bool isPAFF() const override { return mFFmpeg && mFFmpeg->isPAFF(); }
    virtual int  paffLog2MaxFrameNum() const override;

    // Typed accessors (kept for callers that need concrete H.264 types)
    TTH264SPS*        getSPS() const { return mSPS; }
    TTH264AccessUnit* frameAt(int index);
    int               findIDRAfter(int frameIndex);

protected:
    // Hooks
    TTVideoCodecType expectedCodec() const override;
    const char*      codecLabel() const override { return "H.264"; }

    void    resetSPS() override;
    void    buildSPSFromStreamInfo(const TTStreamInfo& info) override;
    void    setSPSFrameRate(double fps) override;
    QString spsDescription() const override;

    void    buildAccessUnits() override;
    int     accessUnitCount() const override { return mAccessUnits.size(); }
    bool    accessUnitIsIDR(int idx) const override;
    bool    accessUnitIsRAP(int idx) const override { return accessUnitIsIDR(idx); }  // H.264 RAP == IDR
    int     accessUnitToCodingType(int idx) const override;

    bool    isPAFFCorrectionApplicable() const override { return true; }

private:
    TTH264SPS* mSPS;
    QList<TTH264AccessUnit*> mAccessUnits;
};

#endif // TTH264VIDEOSTREAM_H
