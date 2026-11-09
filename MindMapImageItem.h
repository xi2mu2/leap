#pragma once

#include <QGraphicsPixmapItem>
#include <QCursor>
#include <QImage>
#include <QVector>

class MindMapNode;
class MindMapBlock;
class ImageBlock;

// 独立的节点图片附件项
// 职责：显示图片、支持选中边框、四角等比例缩放、右键删除、拖动重排序
class MindMapImageItem : public QGraphicsPixmapItem {
public:
    explicit MindMapImageItem(const QImage& image, MindMapNode* parentNode);

    void setImageSelected(bool selected);
    bool isImageSelected() const { return m_isSelected; }

    void setImageSize(const QSizeF& size);
    QSizeF imageSize() const { return boundingRect().size(); }

    QString imageName() const { return m_imageName; }
    void setImageName(const QString& name);

    QImage originalImage() const { return m_originalImage; }

    // 设置所属的图片块
    void setImageBlock(ImageBlock* block) { m_imageBlock = block; }
    ImageBlock* imageBlock() const { return m_imageBlock; }

    // 缩放状态
    bool isResizing() const { return m_isResizing; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // 悬停事件：用于无鼠标按下时的手柄检测和光标变化
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    enum HandlePos { None, TopLeft, TopRight, BottomLeft, BottomRight };

    HandlePos hitTestHandle(const QPointF& pos) const;
    void updateCursor(HandlePos pos);

    // 检查是否点击在手柄区域内（用于区分缩放和拖动）
    bool isOverHandle(const QPointF& pos) const;

    MindMapNode* m_parentNode;
    ImageBlock* m_imageBlock = nullptr;
    bool m_isSelected = false;
    bool m_isResizing = false;
    bool m_isDragging = false;
    bool m_dragStarted = false;
    HandlePos m_resizeHandle = None;
    QPointF m_resizeStartPos;
    QSizeF m_originalSize;
    qreal m_aspectRatio = 1.0;
    QImage m_originalImage;
    QString m_imageName;

    // 缩放支持
    int m_resizeBlockIndex = -1;

    // 拖动重排序
    QPointF m_dragStartPos;
    int m_originalBlockIndex = -1;
    QVector<MindMapBlock*> m_dragBeforeBlocks;

    static constexpr qreal HANDLE_SIZE = 10;       // 手柄绘制尺寸（像素）
    static constexpr qreal HANDLE_HIT_RADIUS = 20; // 手柄命中半径（像素）
};
