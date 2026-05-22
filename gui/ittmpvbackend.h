/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef ITTMPVBACKEND_H
#define ITTMPVBACKEND_H

#include <QObject>
#include <QStringList>
#include <QVariant>

class QWidget;

// Abstraktes mpv-Steuer-Interface. Bildet das gemeinsame Modell von
// mpv-IPC-JSON und libmpv-C-API ab (command / set_property / observe_property).
class ITTMpvBackend : public QObject
{
  Q_OBJECT
public:
  explicit ITTMpvBackend(QObject* parent = nullptr) : QObject(parent) {}
  ~ITTMpvBackend() override {}

  // Lifecycle
  virtual bool start()    = 0;   // Backend hochfahren
  virtual void shutdown() = 0;   // sauber beenden

  // Render-Ziel
  virtual void attachToWidget(QWidget* target) = 0;
  virtual void detach()                        = 0;

  // mpv-Steuermodell
  virtual void command(const QStringList& args)                     = 0;
  virtual void setProperty(const QString& name, const QVariant& v)  = 0;
  virtual void observeProperty(const QString& name)                 = 0;

signals:
  void propertyChanged(const QString& name, const QVariant& value);
  void connected();        // IPC connection established — safe to send commands
  void fileLoaded();
  void playbackFinished();
  void mpvError(const QString& message);
};

#endif // ITTMPVBACKEND_H
