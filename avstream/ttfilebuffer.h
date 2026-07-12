/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTFILEBUFFER_H
#define TTFILEBUFFER_H

// Qt header files
#include <QString>
#include <QFile>
#include <QByteArray>

// -----------------------------------------------------------------------------
// TTFileBuffer: class declaration
// -----------------------------------------------------------------------------
class TTFileBuffer
{
public:
  // create / delete
  TTFileBuffer(QString name, QIODevice::OpenMode mode);
  TTFileBuffer(QString name, QIODevice::OpenMode mode, int bufferSize);
  ~TTFileBuffer();

  // file stream
  bool    open();    
  void    close();
  quint64 size();
  bool    atEnd();
  quint64 position();

  // read / write
  void    readByte( quint8 &byte1 );
  int     readByte( quint8* byteArray, int length);
  QString readLine(QString delimiter = "\n");

  // search
  void    initTSearch();
  void    nextStartCodeTS();
   
  // positioning
  void    seekForward(quint64 offset);
  void    seekBackward(quint64 offset);
  void    seekRelative(quint64 offset);
  void    seekAbsolute(quint64 offset);

  // migration stuff

  quint64 directWrite(quint8 byte1);
  quint64 directWrite(const quint8* w_buffer, int w_length);

 protected:
  void    initInstance();
  void    initInstance(int bufferSize);
  void    fillBuffer();
  void    fillBuffer(int length);
  quint8  readByte();

 private:
  QFile*     file;
  QIODevice::OpenMode mode;
  bool       isAtEnd;
  quint8*    cBuffer;
  int        bufferSize;
  int        bufferMask;
  qint64     writePos;
  qint64     readPos;
  int        readInc;

  quint8     tsStartCode[3];
  int        tsShift[256];
  int        tsLookAt;
};

class TTFileBufferException
{
  public:
    enum ExceptionType
    {
      StreamEOF,
      SeekError
    };

    TTFileBufferException(ExceptionType type);

    QString message();

  protected:
    ExceptionType ex_type;
};

#endif //TTFILEBUFFER_H
