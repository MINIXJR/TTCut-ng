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

class TTSubtitleType;
class TTSubtitleStream;
class TTAVItem;

//! Runable task for opening subtitle streams
class TTOpenSubtitleTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTOpenSubtitleTask(TTAVItem* avItem, QString filePath, int order);

  protected:
    void cleanUp();
    void operation();

  public slots:
    void onUserAbort();

  signals:
    void finished(TTAVItem*, TTSubtitleStream*, int);

  private:
    TTAVItem*         mpAVItem;
    int               mOrder;
    QString           mFilePath;
    TTSubtitleStream* mpSubtitleStream;
    TTSubtitleType*   mpSubtitleType;
};
