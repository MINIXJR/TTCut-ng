/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTQUICKJUMPDELEGATE_H
#define TTQUICKJUMPDELEGATE_H

#include <QStyledItemDelegate>

class TTQuickJumpDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  TTQuickJumpDelegate(int thumbWidth, int thumbHeight, QObject* parent = 0);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

  void setHighlightFrameIndex(int frameIndex);
  QSize thumbnailSize() const;

private:
  int mThumbWidth;
  int mThumbHeight;
  int mLabelHeight;
  int mPadding;
  int mHighlightFrameIndex;
};

#endif // TTQUICKJUMPDELEGATE_H
