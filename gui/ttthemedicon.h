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

// Application icon from the PNGs bundled in mainwindow.qrc, one file per size,
// so 16-32 px use the simplified small drawing (tools/logo/render-icons.sh).
// Deliberately not QIcon::fromTheme("ttcut-ng"): Qt's fallback search path
// includes /usr/share/pixmaps, where older packages installed the previous
// logo. Panels and task bars take the icon from ttcut-ng.desktop anyway.
inline QIcon ttAppIcon()
{
  QIcon icon;
  for (int n : {16, 22, 24, 32, 48, 64, 128, 256})
    icon.addFile(QStringLiteral(":/pixmaps/pixmaps/icons/%1/ttcut-ng.png").arg(n), QSize(n, n));
  return icon;
}

#endif // TTTHEMEDICON_H
