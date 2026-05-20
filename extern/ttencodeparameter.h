/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTENCODEPARAMETER
// ----------------------------------------------------------------------------

#ifndef TTENCODEPARAMETER_H
#define TTENCODEPARAMETER_H

/* /////////////////////////////////////////////////////////////////////////////
 * Class for parameter common for all encoder
 */
class TTEncodeParameter
{
  public:
    TTEncodeParameter(){};
    ~TTEncodeParameter(){};

    void      setAVIFileInfo(const QFileInfo& value)   { aviFInfo = value; }
    QFileInfo aviFileInfo()                            { return aviFInfo; }
    void      setMpeg2FileInfo(const QFileInfo& value) { mpeg2FInfo = value; }
    QFileInfo mpeg2FileInfo()                          { return mpeg2FInfo; }
    void      setVideoWidth(int value)                 { vWidth = value; }
    int       videoWidth()                             { return vWidth; }
    void      setVideoHeight(int value)                { vHeight = value; }
    int       videoHeight()                            { return vHeight; }
    void      setVideoFPS(float value)                 { vFPS = value; }
    float     videoFPS()                               { return vFPS; }
    void      setVideoAspectCode(int value)            { vAspectCode = value; }
    int       videoAspectCode()                        { return vAspectCode; }
    void      setVideoBitrate(float value)             { vBitrate = value; }
    float     videoBitrate()                           { return vBitrate; }
    void      setVideoMaxBitrate(float value)          { vMaxBitrate = value; }
    float     videoMaxBitrate()                        { return vMaxBitrate; }
    void      setVideoInterlaced(bool value)           { vInterlaced = value; }
    bool      videoInterlaced()                        { return vInterlaced; }
    void      setVideoTopFieldFirst(bool value)        { vTopFieldFirst = value; }
    bool      videoTopFieldFirst()                     { return vTopFieldFirst; }
    int       start() { return mStartIndex; }
    void       setStart(int value) { mStartIndex = value; }
    int       end() { return mEndIndex; }
    void      setEnd(int value) { mEndIndex = value; }

    void      print(char* prefix);

  private:
    QFileInfo aviFInfo;
    QFileInfo mpeg2FInfo;
    int       mStartIndex;
    int       mEndIndex;
    int       vWidth;
    int       vHeight;
    float     vFPS;
    int       vAspectCode;
    float     vBitrate;
    float     vMaxBitrate;
    bool      vInterlaced;
    bool      vTopFieldFirst;
};

#endif //TTENCODEPARAMETER_H
