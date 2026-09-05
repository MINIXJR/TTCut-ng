/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : tttracktreeview.h                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                       DATE: 09/05/2026 */
/*----------------------------------------------------------------------------*/

#ifndef TTTRACKTREEVIEW_H
#define TTTRACKTREEVIEW_H

#include <QWidget>

class QAction;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

//! Common part of the three file-list widgets (video, audio, subtitle): the
//! button row and the context menu (insert / up / delete / down), the
//! selection-based up/down/remove slots that turn into the removeItem/
//! swapItems signals, and the per-row delay spin box and language combo of
//! the track lists. Each subclass keeps its own .ui form and hands the
//! widgets over with bindListWidgets() after setupUi(); the model wiring
//! (which TTAVItem/TTAVData signals feed the list) stays in the subclass
//! because the signal sets differ.
class TTTrackTreeView : public QWidget
{
  Q_OBJECT

  public:
    explicit TTTrackTreeView(QWidget* parent = nullptr);
    void clear();

  signals:
    void openFile();
    void removeItem(int index);
    void swapItems(int oldIndex, int newIndex);
    void languageChanged(int index, const QString& language);
    void delayChanged(int index, int delayMs);

  public slots:
    virtual void onItemUp();
    virtual void onItemDown();
    virtual void onRemoveItem();
    virtual void onItemRemoved(int index);
    void onClearList();
    void onContextMenuRequest(const QPoint& point);

  protected:
    //! Item-type specific texts of the actions, translated by the subclass
    //! in its own tr() context (the shared "Move up/Delete/Move down" labels
    //! are translated here).
    struct ActionTexts
    {
      QString insertText;
      QString insertTip;
      QString upTip;
      QString deleteTip;
      QString downTip;
    };
    //! Called once from the subclass constructor after setupUi(): root
    //! decoration off, theme icons on the buttons, the four actions, and the
    //! button/context-menu connections.
    void bindListWidgets(QTreeWidget* list, QPushButton* open, QPushButton* up,
                         QPushButton* down, QPushButton* del, const ActionTexts& texts);
    //! Row of the current item, -1 when nothing is selected.
    int currentRow() const;
    //! Per-row editors, each wired to its signal with the row looked up at
    //! edit time (rows move when items are swapped or removed).
    void addDelaySpin(QTreeWidgetItem* item, int column, int delayMs, const QString& toolTip);
    void addLanguageCombo(QTreeWidgetItem* item, int column, const QString& currentLang);

    QTreeWidget* mpListView = nullptr;

  private:
    QAction* mpItemNewAction    = nullptr;
    QAction* mpItemUpAction     = nullptr;
    QAction* mpItemDeleteAction = nullptr;
    QAction* mpItemDownAction   = nullptr;
};

#endif // TTTRACKTREEVIEW_H
