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

    virtual QString getClassName() const {return "TTException";};
    QString getMessage() const;

  protected:
    QString message;
};

class TTCommonException : public TTException
{
  public:
    TTCommonException(const QString& msg) : TTException(msg){};
    TTCommonException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
  protected:
    virtual QString getClassName() const { return "TTCommonException"; };
};

class TTMemoryAllocationException : public TTException
{
  public:
    TTMemoryAllocationException(const QString&msg) : TTException(msg){};
    TTMemoryAllocationException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTMemoryAllocationException";}
};

class TTMethodNotImplementedException : public TTException
{
  public:
    TTMethodNotImplementedException(const QString&msg) : TTException(msg){};
    TTMethodNotImplementedException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTMethodNotImplementedException";};
};

class TTMissingMethodException : public TTException
{
  public:
    TTMissingMethodException(const QString&msg) : TTException(msg){};
    TTMissingMethodException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTMissingMethodException";};
};

class TTIOException : public TTException
{
  public:
    TTIOException(const QString& msg) : TTException(msg){};
    TTIOException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTIOException";};
};

class TTDataFormatException : public TTException
{
  public:
    TTDataFormatException(const QString& msg) : TTException(msg){};
    TTDataFormatException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTDataFormatException";};
};

class TTInvalidOperationException : public TTException
{
  public:
    TTInvalidOperationException(const QString& msg) : TTException(msg){};
    TTInvalidOperationException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTInvalidOperationException";};
};

class TTArgumentException : public TTException
{
  public:
    TTArgumentException(const QString& msg) : TTException(msg){};
    TTArgumentException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTArgumentException";};
};

class TTArgumentNullException : public TTException
{
  public:
    TTArgumentNullException(const QString& msg) : TTException(msg) {};
    TTArgumentNullException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTArgumentNullException";};
};

class TTArgumentOutOfRangeException : public TTException
{
  public:
    TTArgumentOutOfRangeException(const QString& msg) : TTException(msg) {};
    TTArgumentOutOfRangeException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTArgumentOutOfRangeException";};
};

class TTIndexOutOfRangeException : public TTException
{
  public:
    TTIndexOutOfRangeException(const QString& msg) : TTException(msg) {};
    TTIndexOutOfRangeException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
   protected:
    virtual QString getClassName() const {return "TTIndexOutOfRangeException";};
};

class TTFileNotFoundException : public TTException
{
	public:
		TTFileNotFoundException(const QString& msg) : TTException(msg) {};
    TTFileNotFoundException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
 	protected:
		virtual QString getClassName() const {return "TTFileNotFoundException";};
};

class TTAbortException : public TTException
{
	public:
		TTAbortException(const QString& msg) : TTException(msg) {};
    TTAbortException(const QString& caller, int line, const QString& msg) : TTException(caller, line, msg){};
 	protected:
		virtual QString getClassName() const {return "TTAbortException";};
};
#endif

