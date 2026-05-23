/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvlibbackend.h"
#include "ttmpvrenderwidget.h"

#include <QStringList>
#include <QVariant>

// libmpv-Header werden in Task 5/7/8 angezogen.

TTMpvLibBackend::TTMpvLibBackend(QObject* parent)
  : ITTMpvBackend(parent)
{
}

TTMpvLibBackend::~TTMpvLibBackend()
{
  shutdown();
}

bool TTMpvLibBackend::start()
{
  // wird in Task 5 implementiert
  return false;
}

void TTMpvLibBackend::shutdown()
{
  // wird in Task 5 implementiert
}

void TTMpvLibBackend::command(const QStringList& /*args*/)
{
  // wird in Task 7 implementiert
}

void TTMpvLibBackend::setProperty(const QString& /*name*/, const QVariant& /*value*/)
{
  // wird in Task 7 implementiert
}

void TTMpvLibBackend::observeProperty(const QString& /*name*/)
{
  // wird in Task 7 implementiert
}

QWidget* TTMpvLibBackend::renderWidget()
{
  return mWidget;
}

void TTMpvLibBackend::drainEvents()
{
  // wird in Task 8 implementiert
}

void TTMpvLibBackend::wakeupCallback(void* ctx)
{
  auto* self = static_cast<TTMpvLibBackend*>(ctx);
  QMetaObject::invokeMethod(self, "drainEvents", Qt::QueuedConnection);
}
