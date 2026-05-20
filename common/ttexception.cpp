/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// -----------------------------------------------------------------------------
// TTEXCEPTION
// -----------------------------------------------------------------------------


#include "ttexception.h"
#include "ttmessagelogger.h"

TTException::TTException()
{
}

TTException::~TTException()
{
}

TTException::TTException(const QString& msg) : message(msg)
{
}

TTException::TTException(const QString& caller, int line, const QString& msg)
{
  message = msg;

  TTMessageLogger* log = TTMessageLogger::getInstance();
  log->fatalMsg(caller, line, msg);
}

QString TTException::getMessage() const
{
  return message;
}


