/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH264VIDEOSTREAM
// H.264/AVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

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
