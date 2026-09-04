/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTWINDOWGEOMETRY_H
#define TTWINDOWGEOMETRY_H

// Per-window UI state as plain, hand-editable QSettings keys.
//
// This replaces QWidget::saveGeometry()'s opaque @ByteArray blob. What the blob
// could do and these keys cannot: rescale when the screen width changed since
// the save, and remember a screen number. The callers replace both with the
// on-screen check they already perform plus ttClampToArea().
//
// Deliberately free functions with no widget dependency, so the diag harness
// tools/diag/test_window_geometry can drive them headless.

#include <QRect>
#include <QSize>
#include <QString>

class QSettings;

struct TTWindowGeometry
{
  QRect rect;                 // normal (un-maximised) geometry
  bool  maximized = false;
  bool  valid     = false;    // false when the group is absent or incomplete
};

// [group] x, y, width, height, maximized. A group missing any key, or carrying
// a non-positive width/height, is reported invalid rather than half-applied.
TTWindowGeometry ttLoadWindowGeometry(QSettings& settings, const QString& group);
void ttSaveWindowGeometry(QSettings& settings, const QString& group,
                          const QRect& normalRect, bool maximized);

// [group] width, height — for dialogs that keep a size but not a position.
TTWindowGeometry ttLoadDialogSize(const QSettings& settings, const QString& group);
void ttSaveDialogSize(QSettings& settings, const QString& group, const QSize& size);

// Shrink to the available area, then push the origin back inside it. Returns a
// rect fully contained in `available` whenever `available` is non-empty.
QRect ttClampToArea(const QRect& want, const QRect& available);

// Decode a QWidget::saveGeometry() blob. Returns valid=false on a bad magic,
// an unknown major version, or a short buffer. Reads the blob's own
// normalGeometry field, so it does not depend on window-state timing.
TTWindowGeometry ttDecodeGeometryBlob(const QByteArray& blob);

// One-time upgrade: if `group` has no plain keys but carries a legacy
// `geometry` blob, decode it into plain keys and remove the blob.
// Returns true when a migration happened.
bool ttMigrateGeometryBlob(QSettings& settings, const QString& group);

// One-time cleanup: QuickJumpDialog's size used to be written through an
// argument-less QSettings, which resolved to
// ~/.config/Unknown Organization/TTCut-ng.conf. Import that value once and
// delete the file (and its directory, when empty). No-op when it is gone.
void ttImportStrayQuickJumpSize(QSettings& target);

#endif // TTWINDOWGEOMETRY_H
