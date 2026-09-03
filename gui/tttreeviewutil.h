/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTTREEVIEWUTIL
// Small helpers shared by the audio/subtitle/video/cut tree views: theme
// icons with a Qt standard-icon fallback, context-menu actions, and the
// per-row editor widgets (delay spin box) that the list views embed.
// ----------------------------------------------------------------------------

#ifndef TTTREEVIEWUTIL_H
#define TTTREEVIEWUTIL_H

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QSpinBox>
#include <QStyle>
#include <QTreeWidget>

// Theme icon with the application style's standard icon as fallback.
inline QIcon ttThemeIcon(const char* themeName, QStyle::StandardPixmap fallback)
{
  return QIcon::fromTheme(QLatin1String(themeName), QApplication::style()->standardIcon(fallback));
}

// Action owned by parent with text, theme icon and status tip.
inline QAction* ttMakeAction(QObject* parent, const QString& text, const char* themeName,
                             QStyle::StandardPixmap fallback, const QString& statusTip)
{
  QAction* action = new QAction(text, parent);
  action->setIcon(ttThemeIcon(themeName, fallback));
  action->setStatusTip(statusTip);
  return action;
}

// Top-level row whose item widget in column is widget, or -1.
inline int ttRowOfItemWidget(const QTreeWidget* tree, int column, const QWidget* widget)
{
  for (int row = 0; row < tree->topLevelItemCount(); row++) {
    if (tree->itemWidget(tree->topLevelItem(row), column) == widget) return row;
  }
  return -1;
}

// Per-row delay editor (±9999 ms, mkvmerge sign convention).
inline QSpinBox* ttMakeDelaySpin(int valueMs, const QString& toolTip)
{
  QSpinBox* spin = new QSpinBox();
  spin->setRange(-9999, 9999);
  spin->setSuffix(" ms");
  spin->setToolTip(toolTip);
  spin->setValue(valueMs);
  return spin;
}

#endif // TTTREEVIEWUTIL_H
