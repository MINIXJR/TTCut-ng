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
// TTCUTPARAMETER
// ----------------------------------------------------------------------------

#include "ttcutparameter.h"


/*!
 * TTCutParameter
 */
TTCutParameter::TTCutParameter(TTFileBuffer* fBuffer)
{
  targetStreamBuffer     = fBuffer;
  isDVDCompliantStream   = false;
  numPicturesWritten     = 0;
  dvdCompliantMaxBitrate = 0.0;
  dvdCompliantMaxFrames  = 0;
}

/*!
 * ~TTCutParameter
 */
TTCutParameter::~TTCutParameter()
{
}

/*!
 * Properties
 */
TTFileBuffer* TTCutParameter::getTargetStreamBuffer() { return targetStreamBuffer; }

int  TTCutParameter::getNumPicturesWritten()             { return numPicturesWritten; }
void TTCutParameter::setNumPicturesWritten(int value)    { numPicturesWritten = value; }
int  TTCutParameter::getCutInIndex()                     { return cutInIndex; }
void TTCutParameter::setCutInIndex(int value)            { cutInIndex = value; }
int  TTCutParameter::getCutOutIndex()                    { return cutOutIndex; }
void TTCutParameter::setCutOutIndex(int value)           { cutOutIndex = value; }

/*!
 * firstCall
 */
void TTCutParameter::firstCall()
{
}

/*!
 * lastCall
 */
void TTCutParameter::lastCall()
{
  writeSequenceEndHeader();
}

/*!
 * writeSequenceEndHeader
 */
void TTCutParameter::writeSequenceEndHeader()
{
  quint8 seqEndCode[4];

  seqEndCode[0] = 0x00;
  seqEndCode[1] = 0x00;
  seqEndCode[2] = 0x01;
  seqEndCode[3] = 0xb7;

  targetStreamBuffer->directWrite(seqEndCode, 4);
}
