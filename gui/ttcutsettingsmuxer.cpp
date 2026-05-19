/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingsmuxer.cpp                                          */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsmuxer.h"

#include "../common/ttsettings.h"

#include <QStandardItemModel>


TTCutSettingsMuxer::TTCutSettingsMuxer(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  populateCodecMuxers();
  populateMpgTarget();
  populateMpgMode();
  connect(cbMkvCreateChapters, &QCheckBox::stateChanged, this, &TTCutSettingsMuxer::onMkvChaptersChanged);
  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutSettingsMuxer::resetToDefaults);
}

void TTCutSettingsMuxer::resetToDefaults()
{
  // Compile-time defaults — must match common/ttsettings.h
  // (mMkvCreateChapters/mMkvChapterInterval, mMuxDeleteES, mMpeg2Target,
  // mMuxMode, mMpeg2Muxer/mH264Muxer/mH265Muxer).
  cbMkvCreateChapters->setChecked(true);
  sbMkvChapterInterval->setValue(5);
  sbMkvChapterInterval->setEnabled(true);
  cbDeleteES->setChecked(false);
  cbMpgTarget->setCurrentIndex(7);   // DVD with NAV sectors
  cbMpgMode->setCurrentIndex(0);     // Direkt muxen
  // Container je Codec: MPEG-2 → MPG (0=mplex), H.264 → MKV (1=libav),
  // H.265 → MKV (1=libav). Header default mMpeg2Muxer=0 entspricht 'MPG'
  // hier (combo item 1, data 0).
  int mpgIdx = cbMpeg2Muxer->findData(0);
  if (mpgIdx >= 0) cbMpeg2Muxer->setCurrentIndex(mpgIdx);
  int mkvIdxH264 = cbH264Muxer->findData(1);
  if (mkvIdxH264 >= 0) cbH264Muxer->setCurrentIndex(mkvIdxH264);
  int mkvIdxH265 = cbH265Muxer->findData(1);
  if (mkvIdxH265 >= 0) cbH265Muxer->setCurrentIndex(mkvIdxH265);
}

void TTCutSettingsMuxer::populateCodecMuxers()
{
  for (QComboBox* cb : { cbMpeg2Muxer, cbH264Muxer, cbH265Muxer }) {
    cb->clear();
    cb->insertItem(0, "MKV (libav)", 1);
    cb->insertItem(1, "MPG (mplex)", 0);
  }
  // H.264/H.265 do not support MPG: disable 2nd item
  for (QComboBox* cb : { cbH264Muxer, cbH265Muxer }) {
    QStandardItemModel* m = qobject_cast<QStandardItemModel*>(cb->model());
    if (m && m->item(1)) m->item(1)->setEnabled(false);
  }
}

void TTCutSettingsMuxer::populateMpgTarget()
{
  cbMpgTarget->clear();
  cbMpgTarget->insertItem(0, "Generic MPEG1 (f0)");
  cbMpgTarget->insertItem(1, "VCD (f1)");
  cbMpgTarget->insertItem(2, "user-rate VCD (f2)");
  cbMpgTarget->insertItem(3, "Generic MPEG2 (f3)");
  cbMpgTarget->insertItem(4, "SVCD (f4)");
  cbMpgTarget->insertItem(5, "user-rate SVCD (f5)");
  cbMpgTarget->insertItem(6, "VCD Stills (f6)");
  cbMpgTarget->insertItem(7, "DVD mit NAV-Sektoren (f8)");
  cbMpgTarget->insertItem(8, "DVD (f9)");
}

void TTCutSettingsMuxer::populateMpgMode()
{
  cbMpgMode->clear();
  cbMpgMode->insertItem(0, "Direkt muxen");
  cbMpgMode->insertItem(1, "Mux-Skript erstellen");
}

void TTCutSettingsMuxer::setTabData()
{
  TTSettings* s = TTSettings::instance();

  cbMkvCreateChapters->setChecked(s->mkvCreateChapters());
  sbMkvChapterInterval->setValue(s->mkvChapterInterval());
  sbMkvChapterInterval->setEnabled(s->mkvCreateChapters());

  cbMpgTarget->setCurrentIndex(qBound(0, s->mpeg2Target(), cbMpgTarget->count() - 1));
  cbMpgMode->setCurrentIndex(qBound(0, s->muxMode(), cbMpgMode->count() - 1));

  cbDeleteES->setChecked(s->muxDeleteES());

  int m2idx = cbMpeg2Muxer->findData(s->mpeg2Muxer());
  int h4idx = cbH264Muxer->findData(s->h264Muxer());
  int h5idx = cbH265Muxer->findData(s->h265Muxer());
  cbMpeg2Muxer->setCurrentIndex(m2idx >= 0 ? m2idx : 0);
  cbH264Muxer->setCurrentIndex(h4idx >= 0 ? h4idx : 0);
  cbH265Muxer->setCurrentIndex(h5idx >= 0 ? h5idx : 0);
}

void TTCutSettingsMuxer::saveTabData()
{
  TTSettings* s = TTSettings::instance();

  s->setMkvCreateChapters(cbMkvCreateChapters->isChecked());
  s->setMkvChapterInterval(sbMkvChapterInterval->value());

  s->setMpeg2Target(cbMpgTarget->currentIndex());
  s->setMuxMode(cbMpgMode->currentIndex());

  s->setMuxDeleteES(cbDeleteES->isChecked());
  s->setMpeg2Muxer(cbMpeg2Muxer->currentData().toInt());
  s->setH264Muxer(cbH264Muxer->currentData().toInt());
  s->setH265Muxer(cbH265Muxer->currentData().toInt());
}

void TTCutSettingsMuxer::setMode(Mode m)
{
  // Defaults mode: all widgets visible.
  // Override mode no longer used — no-op.
  Q_UNUSED(m);
}

void TTCutSettingsMuxer::onMkvChaptersChanged(int state)
{
  TTSettings::instance()->setMkvCreateChapters(state == Qt::Checked);
  sbMkvChapterInterval->setEnabled(TTSettings::instance()->mkvCreateChapters());
}
