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
  connect(cbMkvCreateChapters, &QCheckBox::stateChanged, this, &TTCutSettingsMuxer::onMkvChaptersChanged);
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

void TTCutSettingsMuxer::setTabData()
{
  cbMkvCreateChapters->setChecked(TTSettings::instance()->mkvCreateChapters());
  sbMkvChapterInterval->setValue(TTSettings::instance()->mkvChapterInterval());
  sbMkvChapterInterval->setEnabled(TTSettings::instance()->mkvCreateChapters());
  cbDeleteES->setChecked(TTSettings::instance()->muxDeleteES());

  int m2idx = cbMpeg2Muxer->findData(TTSettings::instance()->mpeg2Muxer());
  int h4idx = cbH264Muxer->findData(TTSettings::instance()->h264Muxer());
  int h5idx = cbH265Muxer->findData(TTSettings::instance()->h265Muxer());
  cbMpeg2Muxer->setCurrentIndex(m2idx >= 0 ? m2idx : 0);
  cbH264Muxer->setCurrentIndex(h4idx >= 0 ? h4idx : 0);
  cbH265Muxer->setCurrentIndex(h5idx >= 0 ? h5idx : 0);
}

void TTCutSettingsMuxer::saveTabData()
{
  TTSettings::instance()->setMkvCreateChapters(cbMkvCreateChapters->isChecked());
  TTSettings::instance()->setMkvChapterInterval(sbMkvChapterInterval->value());
  TTSettings::instance()->setMuxDeleteES(cbDeleteES->isChecked());
  TTSettings::instance()->setMpeg2Muxer(cbMpeg2Muxer->currentData().toInt());
  TTSettings::instance()->setH264Muxer(cbH264Muxer->currentData().toInt());
  TTSettings::instance()->setH265Muxer(cbH265Muxer->currentData().toInt());
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
