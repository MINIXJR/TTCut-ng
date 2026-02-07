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
// H.264/AVC Video Stream handling for frame-accurate cutting
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#ifndef TTH264VIDEOSTREAM_H
#define TTH264VIDEOSTREAM_H

#include "ttavstream.h"
#include "tth264videoheader.h"
#include "../extern/ttffmpegwrapper.h"
#include "../common/ttmessagelogger.h"

#include <QString>
#include <QFileInfo>
#include <QList>

class TTCutParameter;
class TTVideoHeaderList;
class TTVideoIndexList;

// -----------------------------------------------------------------------------
// TTH264VideoStream
// Handles H.264/AVC elementary streams for frame-accurate cutting
// -----------------------------------------------------------------------------
class TTH264VideoStream : public TTVideoStream
{
public:
    TTH264VideoStream(const QFileInfo& fInfo);
    virtual ~TTH264VideoStream();

    // Stream type identification
    virtual TTAVTypes::AVStreamType streamType() const override;

    // Frame rate (override to use stored value instead of MPEG-2 sequence header)
    virtual float frameRate() override;

    // Header and index list creation (using libav)
    virtual int createHeaderList() override;
    virtual int createIndexList() override;

    // Cut operations
    virtual void cut(int start, int end, TTCutParameter* cp) override;

    // Cut point validation
    virtual bool isCutInPoint(int pos) override;
    virtual bool isCutOutPoint(int pos) override;

    // Access to SPS information
    TTH264SPS* getSPS() const { return mSPS; }

    // Access to frame info
    TTH264AccessUnit* frameAt(int index);
    int findIDRBefore(int frameIndex);
    int findIDRAfter(int frameIndex);

    // GOP information
    int gopCount() const;
    int findGOPForFrame(int frameIndex);
    int getGOPStart(int gopIndex);
    int getGOPEnd(int gopIndex);

    // Re-encoding support
    void encodePartH264(int start, int end, TTCutParameter* cp);

protected:
    // Internal helpers
    bool openStream();
    bool closeStream();
    void buildHeaderListFromFFmpeg();
    void buildIndexListFromFFmpeg();

    // Copy segment without re-encoding (between keyframes)
    void copyFrameSegment(int startFrame, int endFrame, TTCutParameter* cp);

    // Encode segment (at cut boundaries)
    void encodeSegment(int startFrame, int endFrame, TTCutParameter* cp);

private:
    TTFFmpegWrapper* mFFmpeg;
    TTH264SPS* mSPS;
    QList<TTH264AccessUnit*> mAccessUnits;
    TTMessageLogger* mLog;

    // Encoding parameters for partial re-encoding
    QString mEncoderPreset;
    int mEncoderCrf;
    QString mEncoderProfile;
};

#endif // TTH264VIDEOSTREAM_H
