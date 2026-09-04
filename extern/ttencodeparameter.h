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

    void      setAVIFileInfo(const QFileInfo& value)   { mAviFileInfo = value; }
    QFileInfo aviFileInfo()                            { return mAviFileInfo; }
    void      setMpeg2FileInfo(const QFileInfo& value) { mMpeg2FileInfo = value; }
    QFileInfo mpeg2FileInfo()                          { return mMpeg2FileInfo; }
    void      setVideoWidth(int value)                 { mVideoWidth = value; }
    int       videoWidth()                             { return mVideoWidth; }
    void      setVideoHeight(int value)                { mVideoHeight = value; }
    int       videoHeight()                            { return mVideoHeight; }
    void      setVideoFPS(float value)                 { mVideoFps = value; }
    float     videoFPS()                               { return mVideoFps; }
    void      setVideoAspectCode(int value)            { mVideoAspectCode = value; }
    int       videoAspectCode()                        { return mVideoAspectCode; }
    void      setVideoBitrate(float value)             { mVideoBitrate = value; }
    float     videoBitrate()                           { return mVideoBitrate; }
    void      setVideoInterlaced(bool value)           { mVideoInterlaced = value; }
    bool      videoInterlaced()                        { return mVideoInterlaced; }
    void      setVideoTopFieldFirst(bool value)        { mVideoTopFieldFirst = value; }
    bool      videoTopFieldFirst()                     { return mVideoTopFieldFirst; }
    int       start() { return mStartIndex; }
    void       setStart(int value) { mStartIndex = value; }
    int       end() { return mEndIndex; }
    void      setEnd(int value) { mEndIndex = value; }

    void      print(char* prefix);

  private:
    QFileInfo mAviFileInfo;
    QFileInfo mMpeg2FileInfo;
    int       mStartIndex;
    int       mEndIndex;
    int       mVideoWidth;
    int       mVideoHeight;
    float     mVideoFps;
    int       mVideoAspectCode;
    float     mVideoBitrate;
    bool      mVideoInterlaced;
    bool      mVideoTopFieldFirst;
};

#endif //TTENCODEPARAMETER_H
