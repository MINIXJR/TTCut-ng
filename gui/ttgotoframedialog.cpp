/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttgotoframedialog.h"

#include <QSpinBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>

#include <climits>

namespace {

// frame -> "hh:mm:ss.zzz"
QString frameToTimecode(int frame, double fps)
{
  if (fps <= 0.0) return QString();
  double sec = static_cast<double>(frame) / fps;
  int totalMs  = static_cast<int>(sec * 1000.0 + 0.5);
  int ms       = totalMs % 1000;
  int totalSec = totalMs / 1000;
  int s = totalSec % 60;
  int m = (totalSec / 60) % 60;
  int h = totalSec / 3600;
  return QString("%1:%2:%3.%4")
      .arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0'))
      .arg(s, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));
}

// "[hh:]mm:ss[.zzz]" or a plain integer -> frame. false on failure.
bool timecodeToFrame(const QString& text, double fps, int& outFrame)
{
  const QString t = text.trimmed();
  if (t.isEmpty()) return false;

  // Accept German decimal comma ("00:01:30,500") the same as a dot.
  QString norm = t; norm.replace(',', '.');

  if (norm.contains(':')) {
    if (fps <= 0.0) return false;
    const QStringList parts = norm.split(':');
    int h = 0, m = 0; double s = 0.0; bool ok = false;
    if (parts.size() == 3) {
      h = parts[0].toInt(&ok); if (!ok) return false;
      m = parts[1].toInt(&ok); if (!ok) return false;
      s = parts[2].toDouble(&ok); if (!ok) return false;
    } else if (parts.size() == 2) {
      m = parts[0].toInt(&ok); if (!ok) return false;
      s = parts[1].toDouble(&ok); if (!ok) return false;
    } else {
      return false;
    }
    if (h < 0 || m < 0 || s < 0.0) return false;
    double sec = h * 3600.0 + m * 60.0 + s;
    outFrame = static_cast<int>(qBound(0.0, sec * fps + 0.5, static_cast<double>(INT_MAX)));
    return true;
  }

  bool ok = false;
  int f = norm.toInt(&ok);
  if (!ok) return false;
  outFrame = f;
  return true;
}

} // namespace

TTGotoFrameDialog::TTGotoFrameDialog(int currentFrame, int frameCount,
                                     double frameRate, QWidget* parent)
  : QDialog(parent), mFrameRate(frameRate), mSyncing(false)
{
  setWindowTitle(tr("Go to Frame"));

  mFrameSpin = new QSpinBox(this);
  mFrameSpin->setRange(0, frameCount > 0 ? frameCount - 1 : 0);
  mFrameSpin->setValue(currentFrame);

  mTimecodeEdit = new QLineEdit(this);
  mTimecodeEdit->setText(frameToTimecode(currentFrame, mFrameRate));

  QFormLayout* form = new QFormLayout;
  form->addRow(tr("Frame:"), mFrameSpin);
  form->addRow(tr("Timecode:"), mTimecodeEdit);

  QDialogButtonBox* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(mFrameSpin, qOverload<int>(&QSpinBox::valueChanged),
          this, &TTGotoFrameDialog::onFrameChanged);
  connect(mTimecodeEdit, &QLineEdit::textEdited,
          this, &TTGotoFrameDialog::onTimecodeEdited);
  connect(mTimecodeEdit, &QLineEdit::editingFinished,
          this, &TTGotoFrameDialog::onTimecodeFinished);
}

int TTGotoFrameDialog::selectedFrame() const
{
  return mFrameSpin->value();
}

void TTGotoFrameDialog::onFrameChanged(int frame)
{
  if (mSyncing) return;
  mSyncing = true;
  mTimecodeEdit->setText(frameToTimecode(frame, mFrameRate));
  mSyncing = false;
}

void TTGotoFrameDialog::onTimecodeEdited(const QString& text)
{
  if (mSyncing) return;
  int frame = 0;
  if (timecodeToFrame(text, mFrameRate, frame)) {
    mSyncing = true;
    mFrameSpin->setValue(frame);   // spin box clamps to range
    mSyncing = false;
  }
}

void TTGotoFrameDialog::onTimecodeFinished()
{
  mSyncing = true;
  mTimecodeEdit->setText(frameToTimecode(mFrameSpin->value(), mFrameRate));
  mSyncing = false;
}
