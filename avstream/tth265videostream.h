/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOSTREAM
// H.265/HEVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

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


private:
    TTH265SPS* mSPS;
    TTH265VPS* mVPS;
    QList<TTH265AccessUnit*> mAccessUnits;
};

#endif // TTH265VIDEOSTREAM_H
