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
 */
class TTException
{
  public:
    TTException();
    TTException(const QString& message);
    TTException(const QString& caller, int line, const QString& message);
    virtual ~TTException();

    const QString& getMessage() const;

  protected:
    QString message;
};

class TTIOException : public TTException
{
  public:
    TTIOException(const QString& msg) : TTException(msg){};
    TTIOException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
};

class TTDataFormatException : public TTException
{
  public:
    TTDataFormatException(const QString& msg) : TTException(msg){};
    TTDataFormatException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
};

class TTInvalidOperationException : public TTException
{
  public:
    TTInvalidOperationException(const QString& msg) : TTException(msg){};
    TTInvalidOperationException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
};

class TTArgumentException : public TTException
{
  public:
    TTArgumentException(const QString& msg) : TTException(msg){};
    TTArgumentException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
};

class TTIndexOutOfRangeException : public TTException
{
  public:
    TTIndexOutOfRangeException(const QString& msg) : TTException(msg) {};
    TTIndexOutOfRangeException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
};

class TTFileNotFoundException : public TTException
{
	public:
		TTFileNotFoundException(const QString& msg) : TTException(msg) {};
    TTFileNotFoundException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
 	protected:
};

class TTAbortException : public TTException
{
	public:
		TTAbortException(const QString& msg) : TTException(msg) {};
    TTAbortException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
 	protected:
};
#endif

