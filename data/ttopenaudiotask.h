/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTOPENAUDIOTASK
// ----------------------------------------------------------------------------

#include "../common/ttthreadtask.h"

#include <QString>
#include <QFileInfo>
#include <QMutex>

class TTAudioType;
class TTAudioStream;
class TTAVItem;
// moc must see TTAVItem complete to register TTAVItem* in its signals/slots
Q_MOC_INCLUDE("data/ttavlist.h")

//! Runable task for opening audio streams
class TTOpenAudioTask : public TTThreadTask
{
  Q_OBJECT;

  public:
    TTOpenAudioTask(TTAVItem* avItem, const QString& filePath, int order);

  protected:
    void cleanUp() override;
    void operation() override;

  public slots:
    void onUserAbort() override;

  signals:
    void finished(TTAVItem*, TTAudioStream*, int);

  private:
    TTAVItem*      mpAVItem;
    int            mOrder;
    QString        mFilePath;
    TTAudioStream* mpAudioStream;
    TTAudioType*   mpAudioType;
    //! The task owns mpAudioStream until finished() hands it over; cleanUp()
    //! frees it on every other exit. Written and read by the worker only.
    bool           mStreamHandedOver = false;
    //! Guards mpAudioStream between onUserAbort (GUI thread, setAbort) and
    //! its creation and deletion on the worker.
    QMutex         mStreamMutex;
};
