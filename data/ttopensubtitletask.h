/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTOPENSUBTITLETASK
// ----------------------------------------------------------------------------

#include "../common/ttthreadtask.h"

#include <QString>
#include <QFileInfo>
#include <QMutex>

class TTSubtitleType;
class TTSubtitleStream;
class TTAVItem;
// moc must see TTAVItem complete to register TTAVItem* in its signals/slots
Q_MOC_INCLUDE("data/ttavlist.h")

//! Runable task for opening subtitle streams
class TTOpenSubtitleTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTOpenSubtitleTask(TTAVItem* avItem, const QString& filePath, int order);

  protected:
    void cleanUp() override;
    void operation() override;

  public slots:
    void onUserAbort() override;

  signals:
    void finished(TTAVItem*, TTSubtitleStream*, int);

  private:
    TTAVItem*         mpAVItem;
    int               mOrder;
    QString           mFilePath;
    TTSubtitleStream* mpSubtitleStream;
    TTSubtitleType*   mpSubtitleType;
    //! The task owns mpSubtitleStream until finished() hands it over;
    //! cleanUp() frees it on every other exit. Written and read by the
    //! worker only.
    bool              mStreamHandedOver = false;
    //! Guards mpSubtitleStream between onUserAbort (GUI thread, setAbort)
    //! and its creation and deletion on the worker.
    QMutex            mStreamMutex;
};
