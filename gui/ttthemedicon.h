/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTTHEMEDICON_H
#define TTTHEMEDICON_H

#include <QApplication>
#include <QIcon>
#include <QStyle>

// Theme icon with the widget style's standard icon as fallback - the one
// idiom every button and action icon in the GUI uses.
inline QIcon ttThemedIcon(const char* themeName, QStyle::StandardPixmap fallback)
{
  return QIcon::fromTheme(QLatin1String(themeName), QApplication::style()->standardIcon(fallback));
}

#endif // TTTHEMEDICON_H
