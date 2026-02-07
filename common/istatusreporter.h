/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#ifndef ISTATUSREPORTER_H
#define ISTATUSREPORTER_H

#include <QObject>

//! Status reporter interface
class IStatusReporter : public QObject
{ 
  Q_OBJECT

  public:
    IStatusReporter();
    virtual ~IStatusReporter() {}

  signals:
    void init();
    void started(const QString& msg, quint64 value);
    void step(const QString& msg, quint64 value);
    void finished(const QString& msg, quint64 value);
    void aborted(const QString& msg, quint64 value);
    void error(const QString& msg, quint64 value);
    void exit();
    void statusReport(int state, const QString& msg, quint64 value);
};

//! Status reporter arguments
class StatusReportArgs
{
  public:
    enum State
    {
      Init,
      Start,
      Step,
      Finished,
      Exit,
      Canceled,
      Error,
      ShowProcessForm,
      ShowProcessFormBlocking,
      AddProcessLine,
      HideProcessForm
    };
};

#endif //ISTATUSREPORTER_H
