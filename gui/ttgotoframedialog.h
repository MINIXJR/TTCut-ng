/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTGOTOFRAMEDIALOG_H
#define TTGOTOFRAMEDIALOG_H

#include <QDialog>

class QSpinBox;
class QLineEdit;

// Modal dialog to pick a frame position, with synchronized Frame (spin box) and
// Timecode (line edit) fields. selectedFrame() is valid after exec()==Accepted.
class TTGotoFrameDialog : public QDialog
{
  Q_OBJECT
public:
  TTGotoFrameDialog(int currentFrame, int frameCount, double frameRate,
                    QWidget* parent = nullptr);
  int selectedFrame() const;

private slots:
  void onFrameChanged(int frame);
  void onTimecodeEdited(const QString& text);
  void onTimecodeFinished();

private:
  QSpinBox*  mFrameSpin;
  QLineEdit* mTimecodeEdit;
  double     mFrameRate;
  bool       mSyncing;
};

#endif // TTGOTOFRAMEDIALOG_H
