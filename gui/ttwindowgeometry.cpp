/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttwindowgeometry.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

// [base]width and [base]height, both present and positive.
static bool readSize(const QSettings& settings, const QString& base, int* w, int* h)
{
  if (!settings.contains(base + "width") || !settings.contains(base + "height"))
    return false;
  *w = settings.value(base + "width").toInt();
  *h = settings.value(base + "height").toInt();
  return *w > 0 && *h > 0;
}

static void writeSize(QSettings& settings, const QString& base, const QSize& size)
{
  settings.setValue(base + "width",  size.width());
  settings.setValue(base + "height", size.height());
}

TTWindowGeometry ttLoadWindowGeometry(const QSettings& settings, const QString& group)
{
  TTWindowGeometry g;
  const QString base = group + QLatin1Char('/');

  // Every key must be present: a half-written group means a half-placed
  // window, which is worse than falling back to the default position.
  int w = 0, h = 0;
  if (!settings.contains(base + "x") || !settings.contains(base + "y") ||
      !readSize(settings, base, &w, &h))
    return g;

  g.rect      = QRect(settings.value(base + "x").toInt(),
                      settings.value(base + "y").toInt(), w, h);
  g.maximized = settings.value(base + "maximized", false).toBool();
  g.valid     = true;
  return g;
}

void ttSaveWindowGeometry(QSettings& settings, const QString& group,
                          const QRect& normalRect, bool maximized)
{
  const QString base = group + QLatin1Char('/');
  settings.setValue(base + "x",         normalRect.x());
  settings.setValue(base + "y",         normalRect.y());
  writeSize(settings, base, normalRect.size());
  settings.setValue(base + "maximized", maximized);
}

TTWindowGeometry ttLoadDialogSize(const QSettings& settings, const QString& group)
{
  TTWindowGeometry g;
  int w = 0, h = 0;
  if (!readSize(settings, group + QLatin1Char('/'), &w, &h)) return g;

  g.rect  = QRect(0, 0, w, h);
  g.valid = true;
  return g;
}

void ttSaveDialogSize(QSettings& settings, const QString& group, const QSize& size)
{
  writeSize(settings, group + QLatin1Char('/'), size);
}

QRect ttClampToArea(const QRect& want, const QRect& available)
{
  if (available.isEmpty()) return want;

  QRect r = want;
  r.setWidth (qMin(r.width(),  available.width()));
  r.setHeight(qMin(r.height(), available.height()));

  // Shrink first, then move: moving a too-large rect inside can never succeed.
  if (r.right()  > available.right())  r.moveRight(available.right());
  if (r.bottom() > available.bottom()) r.moveBottom(available.bottom());
  if (r.left()   < available.left())   r.moveLeft(available.left());
  if (r.top()    < available.top())    r.moveTop(available.top());
  return r;
}

TTWindowGeometry ttDecodeGeometryBlob(const QByteArray& blob)
{
  TTWindowGeometry g;

  // Layout of QWidget::saveGeometry(), all big-endian (QDataStream's default):
  //   quint32 magic, quint16 major, quint16 minor,
  //   QRect frameGeometry, QRect normalGeometry,
  //   qint32 screenNumber, quint8 maximized, quint8 fullScreen,
  //   qint32 screenWidth        (major >= 2)
  //   QRect  screenGeometry     (major >= 3)
  // Only the fields up to `maximized` are needed here; the trailing ones are
  // exactly what plain keys give up, so they are read past, not stored.
  static const quint32 kMagic       = 0x1D9D0CB;
  static const int     kMinimumSize = 46;   // through the maximized byte

  if (blob.size() < kMinimumSize) return g;

  QDataStream in(blob);
  in.setVersion(QDataStream::Qt_5_0);

  quint32 magic = 0;
  quint16 major = 0, minor = 0;
  in >> magic >> major >> minor;
  if (magic != kMagic) return g;
  if (major < 1 || major > 3) return g;   // unknown future format: refuse

  QRect frameGeometry, normalGeometry;
  qint32 screenNumber = 0;
  quint8 maximized = 0, fullScreen = 0;
  in >> frameGeometry >> normalGeometry >> screenNumber >> maximized >> fullScreen;
  if (in.status() != QDataStream::Ok) return g;
  if (!normalGeometry.isValid() || normalGeometry.width() <= 0 ||
      normalGeometry.height() <= 0) return g;

  g.rect      = normalGeometry;
  g.maximized = (maximized != 0);
  g.valid     = true;
  return g;
}

bool ttMigrateGeometryBlob(QSettings& settings, const QString& group)
{
  const QString blobKey = group + QLatin1String("/geometry");
  if (!settings.contains(blobKey)) return false;

  // Plain keys already win. The blob is stale either way, so drop it.
  if (ttLoadWindowGeometry(settings, group).valid) {
    settings.remove(blobKey);
    return false;
  }

  const TTWindowGeometry g =
      ttDecodeGeometryBlob(settings.value(blobKey).toByteArray());
  settings.remove(blobKey);
  if (!g.valid) return false;

  ttSaveWindowGeometry(settings, group, g.rect, g.maximized);
  return true;
}

void ttImportStrayQuickJumpSize(QSettings& target)
{
  // Already migrated (or never affected): nothing to do.
  if (target.contains(QLatin1String("QuickJumpDialog/width"))) return;

  const QString configRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString strayDir  = configRoot + QLatin1String("/Unknown Organization");
  const QString strayFile = strayDir + QLatin1String("/TTCut-ng.conf");
  if (!QFile::exists(strayFile)) return;

  {
    QSettings stray(strayFile, QSettings::IniFormat);
    const QSize size = stray.value(QLatin1String("QuickJumpDialog/size")).toSize();
    if (size.isValid() && size.width() > 0 && size.height() > 0)
      ttSaveDialogSize(target, QLatin1String("QuickJumpDialog"), size);

    // Only remove the file when it holds nothing but this one group — another
    // application may have written into the same placeholder location.
    stray.remove(QLatin1String("QuickJumpDialog"));
    stray.sync();
    if (!stray.allKeys().isEmpty()) return;
  }

  QFile::remove(strayFile);
  QDir().rmdir(strayDir);        // fails harmlessly when not empty
}
