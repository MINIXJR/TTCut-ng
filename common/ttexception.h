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


#ifndef TTEXCEPTION_H
#define TTEXCEPTION_H

#include <QString>

/* /////////////////////////////////////////////////////////////////////////////
 * Generell base class for all exception types
 *
 * The (caller, line, message) constructor also writes the message to the log
 * as an error line; the message-only one logs nothing.
 */
class TTException
{
  public:
    TTException();
    explicit TTException(const QString& message);
    TTException(const QString& caller, int line, const QString& message);
    virtual ~TTException();

    const QString& getMessage() const;

  protected:
    QString message;
};

class TTIOException : public TTException
{
  public:
    using TTException::TTException;
};

class TTDataFormatException : public TTException
{
  public:
    using TTException::TTException;
};

class TTInvalidOperationException : public TTException
{
  public:
    using TTException::TTException;
};

class TTArgumentException : public TTException
{
  public:
    using TTException::TTException;
};

class TTIndexOutOfRangeException : public TTException
{
  public:
    using TTException::TTException;
};

class TTFileNotFoundException : public TTException
{
  public:
    using TTException::TTException;
};

class TTAbortException : public TTException
{
  public:
    using TTException::TTException;
};

#endif

