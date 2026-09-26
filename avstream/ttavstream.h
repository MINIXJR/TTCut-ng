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
// TTAVSTREAM (abstract)
// TTAUDIOSTREAM
// TTVIDEOSTREAM
// TTSUBTITLESTREAM
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioStream
//             +- TTAudioStream -|
//             |                 +- TTAC3AudioStream
// TTAVStream -|
//             |
//             +- TTVideoStream - TTMpeg2VideoStream
//             |
//             +- TTSubtitleStream - TTSrtSubtitleStream
//
// -----------------------------------------------------------------------------

#ifndef TTAVSTREAM_H
#define TTAVSTREAM_H

#include "ttavtypes.h"

#include <QProcess>
#include <QObject>
#include <QString>
#include <QFileInfo>

class TTMessageLogger;
class TTSequenceHeader;
class TTVideoHeaderList;
class TTVideoIndexList;
class TTVideoIndex;
class TTAudioHeaderList;
class TTAudioHeader;
class TTSubtitleHeaderList;
class TTSubtitleHeader;
class TTCutParameter;

// -----------------------------------------------------------------------------
// *** TTAVStream: Abstract class TTAVStream
// -----------------------------------------------------------------------------
class TTAVStream : public QObject
{
  Q_OBJECT

protected:
  TTAVStream(const QFileInfo &f_info);
  virtual ~TTAVStream();

public:
  QString fileName();
  QString filePath();
  QString fileExtension();
  quint64 streamLengthByte();
  virtual QTime streamLengthTime() = 0;
  virtual TTAVTypes::AVStreamType streamType() const = 0;
  virtual bool isCutInPoint(int pos) = 0;
  virtual bool isCutOutPoint(int pos) = 0;
  void         setAbort(bool value);

public:
  virtual int  createHeaderList() = 0;
  virtual int  createIndexList() = 0;
  virtual void copySegment(TTFileBuffer* cut_stream, quint64 start_adr, quint64 end_adr);

protected:
  // Debug trail every createHeaderList() ends with: count and stream length.
  void logHeaderListCreated(int count, const char* timeFormat = "hh:mm:ss");
	TTAVTypes::AVStreamType stream_type;
  QFileInfo*              stream_info;
  TTFileBuffer*           stream_buffer;
  bool                    mAbort;
  TTMessageLogger*        log;

signals:
  void statusReport(int state, const QString& msg, quint64 value);
};



// -----------------------------------------------------------------------------
// *** TTAudioStream: Class TTAudioStream
// -----------------------------------------------------------------------------
class TTAudioStream : public TTAVStream
{
public:
  explicit TTAudioStream(const QFileInfo &f_info, int s_pos=0);
  ~TTAudioStream() override;

  // header list
  TTAudioHeaderList* headerList();

  TTAudioHeader* headerAt( int index );

  //! Start time of the last frame (so one frame short of the file), 0 for
  //! an empty list - the same for MPEG audio and AC3.
  QTime streamLengthTime() override;

  // virtual cut methods
  bool isCutInPoint(int) override  {return true;}
  bool isCutOutPoint(int) override {return true;}

protected:
  // header list
  TTAudioHeaderList* header_list;

  int    start_pos;
};


// -----------------------------------------------------------------------------
// *** TTVideoStream: Class TTVideoStream
// -----------------------------------------------------------------------------
class TTVideoStream : public TTAVStream
{
 public:
  TTVideoStream( const QFileInfo &f_info );
  ~TTVideoStream() override;

  // Audio is not cut through this interface: audio tracks go through
  // TTAudioCutter::cut() (libav stream copy), so only the
  // video and subtitle subtrees declare cut().
  virtual void cut(int start, int end, TTCutParameter* cp) = 0;

  // header- and index-list
  TTVideoHeaderList* headerList();
  TTVideoIndexList* indexList();

  int     frameCount();
  virtual float   frameRate();
  virtual bool    isPAFF() const { return false; }
  virtual int     paffLog2MaxFrameNum() const { return 4; }
  float   bitRate();
  QTime   streamLengthTime() override;
  int     frameType(int i_pos);
  QTime   frameTime(int i_pos);
  quint64 frameOffset(int i_pos);

  TTSequenceHeader* currentSequenceHeader();
  TTSequenceHeader* getSequenceHeader(int pos);

  // navigation in index-list
  int currentIndex();


  int moveToIndexPos(int index, int f_type=0);
  int moveToNextFrame(int f_type=0);
  int moveToPrevFrame(int f_type=0);
  int moveToNextIFrame();
  int moveToPrevIFrame();
  int moveToNextPIFrame();
  int moveToPrevPIFrame();

  // Find nearest IDR-safe keyframe at or before frameIndex
  // MPEG-2: returns nearest I-frame (all are keyframes)
  // H.264/H.265: overridden to find true IDR frames
  virtual int findIDRBefore(int frameIndex);

  // Display-order <-> decode-order index conversion. Navigation indices are
  // display positions for ALL codecs (MPEG-2 via temporal_reference sort,
  // H.26x via the POC display-order map). Default: identity (MPEG-2 list
  // positions are already display positions after sortDisplayOrder()).
  virtual int decodeToDisplayIndex(int index) const { return index; }
  virtual int displayToDecodeIndex(int index) const { return index; }

protected:
  // List-objects
  TTVideoHeaderList* header_list;
  TTVideoIndexList*  index_list;

  // Navigation
  int           current_index;
  int           current_marker_index;

  // intern
  float         frame_rate;
  float         bit_rate;
};


// -----------------------------------------------------------------------------
// *** TTSubtitleStream: Class TTSubtitleStream
// -----------------------------------------------------------------------------
class TTSubtitleStream : public TTAVStream
{
public:
  TTSubtitleStream(const QFileInfo &f_info);
  ~TTSubtitleStream() override;

  virtual void cut(int start, int end, TTCutParameter* cp) = 0;

  // header list
  TTSubtitleHeaderList* headerList();

  // virtual cut methods
  bool isCutInPoint(int) override  { return true; }
  bool isCutOutPoint(int) override { return true; }

protected:
  TTSubtitleHeaderList* header_list;
};

#endif //TTAVSTREAM_H
