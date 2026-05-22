/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTMPVPROCESSBACKEND_H
#define TTMPVPROCESSBACKEND_H

#include "ittmpvbackend.h"
#include <QProcess>

class QLocalSocket;
class QWidget;

// ITTMpvBackend-Implementierung: mpv als externer Prozess, gesteuert über
// den JSON-IPC-Socket (--input-ipc-server).
class TTMpvProcessBackend : public ITTMpvBackend
{
  Q_OBJECT
public:
  explicit TTMpvProcessBackend(QObject* parent = nullptr);
  ~TTMpvProcessBackend() override;

  bool start() override;
  void shutdown() override;
  void attachToWidget(QWidget* target) override;
  void detach() override;
  void command(const QStringList& args) override;
  void setProperty(const QString& name, const QVariant& value) override;
  void observeProperty(const QString& name) override;

private slots:
  void onSocketReadyRead();
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
  void sendCommand(const QVariantList& elements);   // {"command":[...]}
  void connectIpcSocket();                          // verbindet nach Prozessstart

  QProcess*     mProcess     = nullptr;
  QLocalSocket* mSocket      = nullptr;
  QWidget*      mTarget      = nullptr;
  QString       mSocketPath;
  QByteArray    mRxBuffer;            // Teil-Zeilen-Puffer
  int           mNextObserveId = 1;
  bool          mPlaybackEndedEmitted = false;  // guard against double-emit
};

#endif // TTMPVPROCESSBACKEND_H
