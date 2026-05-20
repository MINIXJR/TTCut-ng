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
// TTAVTYPES
// TTAUDIOTYPES
// TTVIDEOTYPES
// TTSUBTITLETYPES
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//            +- TTAudioType
//            |
// TTAVTypes -+- TTSubtitleType
//            |
//            +- TTVideoType
//
// -----------------------------------------------------------------------------

#ifndef TTAVTYPES_H
#define TTAVTYPES_H

#include "ttcommon.h"
#include "ttfilebuffer.h"

class QString;
class QFileInfo;

class TTMessageLogger;
class TTAudioStream;
class TTVideoStream;
class TTSubtitleStream;

/* /////////////////////////////////////////////////////////////////////////////
 * Base class for AV stream types
 */
class TTAVTypes
{
 public:
  TTAVTypes( QString f_name );
  virtual ~TTAVTypes();

  // known AV stream types
  enum AVStreamType
  {
    mpeg_audio,
    ac3_audio,
    mpeg2_demuxed_video,
    h264_video,           // H.264/AVC video
    h265_video,           // H.265/HEVC video
    srt_subtitle,
    unknown
  };

  virtual QFileInfo&   avStreamInfo();
  virtual AVStreamType avStreamType();
  virtual quint64      typeHeaderOffset();
  virtual long         typeHeaderLength();

 protected:
	 TTMessageLogger* log;
  QFileInfo*      av_stream_info;
  bool            av_stream_exists;
  TTFileBuffer*   av_stream;
  AVStreamType    av_stream_type;
  quint64         type_header_offset;
  long            type_header_length;
};

/* /////////////////////////////////////////////////////////////////////////////
 * Audio-stream type
 */
class TTAudioType : public TTAVTypes
{
 public:
  TTAudioType( QString f_name );
  ~TTAudioType();

  TTAudioStream* createAudioStream();

 protected:
  void getAudioStreamType();

 private:
  int     start_pos;
  quint8  frame_size_code;
  quint16 sample_rate;
  quint16 frame_len;
};

/* /////////////////////////////////////////////////////////////////////////////
 * Video stream type
 */
class TTVideoType : public TTAVTypes
{
 public:
  TTVideoType( QString f_name );
  ~TTVideoType();

  TTVideoStream* createVideoStream();

 protected:
  void getVideoStreamType();
};

/* /////////////////////////////////////////////////////////////////////////////
 * Subtitle stream type
 */
class TTSubtitleType : public TTAVTypes
{
 public:
  TTSubtitleType( QString f_name );
  ~TTSubtitleType();

  TTSubtitleStream* createSubtitleStream();

 protected:
  void getSubtitleStreamType();
};

#endif //TTAVTYPES_H
