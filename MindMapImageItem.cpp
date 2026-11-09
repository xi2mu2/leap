#include "MindMapImageItem.h"
#include "MindMapNode.h"
#include "MindMapBlock.h"
#include "MindMapCommands.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsScene>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QApplication>
#include <QCursor>
#include <QPointer>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QClipboard>
#include <QLineF>

// 【新增】计算受限的初始显示尺寸
static QSizeF constrainDisplaySize(qreal width, qreal height) {
    // 限制的是画布中的初始显示尺寸，不限制原图数据。
    // 旧值 640x360 会让大图在插入时直接降采样，之后放大只能放大低分辨率 pixmap。
    const qreal MAX_WIDTH = 1280.0;
    const qreal MAX_HEIGHT = 720.0;
    if (width <= MAX_WIDTH && height <= MAX_HEIGHT) {
        return QSizeF(width, height);
    }
    qreal scale = qMin(MAX_WIDTH / width, MAX_HEIGHT / height);
    return QSizeF(width * scale, height * scale);
}

MindMapImageItem::MindMapImageItem(const QImage& image, MindMapNode* parentNode)
    : QGraphicsPixmapItem(parentNode), m_parentNode(parentNode), m_originalImage(image) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setZValue(5);
    // QGraphicsPixmapItem 默认可能使用快速变换；显式设置为平滑变换，
    // 覆盖画布缩放以及图片自身 pixmap 更新时的绘制路径。
    setTransformationMode(Qt::SmoothTransformation);
    m_aspectRatio = image.width() / qMax(1.0, static_cast<qreal>(image.height()));

    // 【修改】初始显示尺寸：若超过 640x360 则自动等比缩放
    QSizeF initSize = constrainDisplaySize(image.width(), image.height());
    setImageSize(initSize);
}

void MindMapImageItem::setImageSelected(bool selected) {
    if (m_isSelected == selected) return;
    m_isSelected = selected;
    update();
}

void MindMapImageItem::setImageSize(const QSizeF& size) {
    if (m_originalImage.isNull()) return;
    if (auto* node = m_imageBlock ? m_imageBlock->parentNode() : m_parentNode) {
        node->invalidateSerializationCache();
    }
    int w = qCeil(size.width());
    int h = qCeil(size.height());
    if (w < 20) w = 20;
    if (h < 20) h = 20;

    // 生成高于逻辑显示尺寸的 pixmap，减少高 DPI 屏幕和画布放大时的模糊。
    // 原图始终保留在 m_originalImage 中，调整尺寸时不会基于旧 pixmap 再采样。
    qreal devicePixelRatio = 2.0;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        devicePixelRatio = qMax(devicePixelRatio, screen->devicePixelRatio());
    }
    devicePixelRatio = qBound<qreal>(1.0, devicePixelRatio, 3.0);

    const QSize targetSize(qCeil(w * devicePixelRatio), qCeil(h * devicePixelRatio));
    QImage scaledImage = m_originalImage.scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap pm = QPixmap::fromImage(scaledImage);
    pm.setDevicePixelRatio(devicePixelRatio);
    setPixmap(pm);
}

void MindMapImageItem::setImageName(const QString& name) {
    if (m_imageName == name) return;
    m_imageName = name;
    if (auto* node = m_imageBlock ? m_imageBlock->parentNode() : m_parentNode) {
        node->invalidateSerializationCache();
    }
}

bool MindMapImageItem::isOverHandle(const QPointF& pos) const {
    return hitTestHandle(pos) != None;
}

void MindMapImageItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 检查是否点击在手柄上（缩放模式）
        HandlePos handle = hitTestHandle(event->pos());
        if (handle != None && m_isSelected) {
            m_isResizing = true;
            m_resizeHandle = handle;
            m_resizeStartPos = event->pos();
            m_originalSize = boundingRect().size();
            // 【修复】记录块索引用于undo
            if (m_imageBlock && m_parentNode) {
                m_resizeBlockIndex = m_parentNode->blockIndex(m_imageBlock);
            }
            event->accept();
            return;
        }

        // 点击图片主体：选中并准备拖动
        if (m_parentNode && m_parentNode->scene()) {
            // 取消其他节点的选中
            for (auto* item : m_parentNode->scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node != m_parentNode) {
                        node->setNodeSelected(false);
                        node->clearBlockSelection();
                    }
                }
            }
            m_parentNode->setNodeSelected(true);
            // Clear stale visual selection flags on every block before selecting this image.
            for (auto* block : m_parentNode->blocks()) {
                if (block != m_imageBlock && block->isBlockSelected()) {
                    block->setBlockSelected(false);
                }
            }
            m_parentNode->clearBlockSelection();

            // 选中当前图片块
            if (m_imageBlock) {
                m_parentNode->setSelectedBlock(m_imageBlock);
            }
        }
        setImageSelected(true);

        // 准备拖动重排序
        m_isDragging = true;
        m_dragStarted = false;
        m_dragBeforeBlocks.clear();
        m_dragStartPos = event->pos();
        if (m_imageBlock) {
            m_originalBlockIndex = m_parentNode->blockIndex(m_imageBlock);
        }

        event->accept();
        return;
    }
    // 右键或其他按钮：转发给父节点
    if (m_parentNode) {
        m_parentNode->handleChildPressEvent(event);
    } else {
        QGraphicsPixmapItem::mousePressEvent(event);
    }
}

void MindMapImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isResizing) {
        QPointF currentPos = event->pos();
        QRectF br = boundingRect();
        QPointF anchor;
        switch (m_resizeHandle) {
        case TopLeft:     anchor = br.bottomRight(); break;
        case TopRight:    anchor = br.bottomLeft();  break;
        case BottomLeft:  anchor = br.topRight();    break;
        case BottomRight: anchor = br.topLeft();     break;
        default:          anchor = br.topLeft();     break;
        }

        qreal newW = qAbs(currentPos.x() - anchor.x());
        qreal newH = qAbs(currentPos.y() - anchor.y());

        qreal wRatio = (m_originalSize.width() > 0) ? newW / m_originalSize.width() : 1;
        qreal hRatio = (m_originalSize.height() > 0) ? newH / m_originalSize.height() : 1;
        if (wRatio > hRatio) {
            newH = newW / m_aspectRatio;
        } else {
            newW = newH * m_aspectRatio;
        }

        if (newW < 20) newW = 20;
        if (newH < 20) newH = 20;

        setImageSize(QSizeF(newW, newH));
        if (m_parentNode) m_parentNode->calcNodeSize();
        update();
        event->accept();
        return;
    }

    if (m_isDragging && m_imageBlock && m_parentNode) {
        if (!m_dragStarted && QLineF(m_dragStartPos, event->pos()).length() >= QApplication::startDragDistance()) {
            m_dragBeforeBlocks = m_parentNode->splitTextBlocksForMediaDrag();
            m_dragStarted = true;
        }
        if (!m_dragStarted) {
            event->accept();
            return;
        }
        // 计算在节点坐标系中的 Y 位置
        QPointF nodePos = mapToParent(event->pos());
        int newIndex = m_parentNode->calculateBlockInsertIndex(nodePos.y());

        // 显示插入指示线
        m_parentNode->showDragIndicator(newIndex);

        // 半透明效果表示正在拖动
        setOpacity(0.6);

        event->accept();
        return;
    }

    // 检测手柄悬停
    HandlePos handle = hitTestHandle(event->pos());
    updateCursor(handle);
    // 转发给父节点更新悬停状态
    if (m_parentNode) {
        m_parentNode->handleChildMoveEvent(event);
    } else {
        QGraphicsPixmapItem::mouseMoveEvent(event);
    }
}

void MindMapImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isResizing) {
        m_isResizing = false;
        m_resizeHandle = None;
        HandlePos handle = hitTestHandle(event->pos());
        updateCursor(handle);
        // 【修复】使用 ResizeImageCommand 进入 undo 栈
        if (m_parentNode && m_imageBlock && m_resizeBlockIndex >= 0) {
            QSizeF newSize = boundingRect().size();
            if (newSize != m_originalSize) {
                if (MindMapNode::s_undoStack && m_parentNode->scene()) {
                    MindMapNode::s_undoStack->push(new ResizeImageCommand(
                        m_parentNode, m_resizeBlockIndex, m_originalSize, newSize, m_parentNode->scene()));
                }
            }
        }
        event->accept();
        return;
    }

    if (m_isDragging) {
        m_isDragging = false;
        setOpacity(1.0);

        if (!m_dragStarted && QLineF(m_dragStartPos, event->pos()).length() >= QApplication::startDragDistance() &&
            m_imageBlock && m_parentNode) {
            m_dragBeforeBlocks = m_parentNode->splitTextBlocksForMediaDrag();
            m_dragStarted = true;
        }

        if (m_dragStarted && m_imageBlock && m_parentNode) {
            QPointF nodePos = mapToParent(event->pos());
            int newIndex = m_parentNode->calculateBlockInsertIndex(nodePos.y());

            // 隐藏指示线
            m_parentNode->hideDragIndicator();

            // 【修复】使用 MoveBlockCommand 进入 undo 栈
            int currentIndex = m_parentNode->blockIndex(m_imageBlock);
            if (currentIndex >= 0 && newIndex >= 0 && newIndex <= m_parentNode->blocks().size() &&
                !(newIndex > currentIndex && newIndex - 1 == currentIndex))
                m_parentNode->moveBlock(currentIndex, newIndex);
            m_parentNode->mergeAdjacentTextBlocksForMediaDrag();
            QVector<MindMapBlock*> afterBlocks = m_parentNode->blocks();
            QVector<MindMapBlock*> transientBlocks = m_parentNode->takeMediaDragTransientBlocks();
            if (MindMapNode::s_undoStack && m_parentNode->scene()) {
                MindMapNode::s_undoStack->push(new MoveMediaBlockCommand(
                    m_parentNode, m_imageBlock, m_dragBeforeBlocks, afterBlocks, transientBlocks, m_parentNode->scene()));
            } else {
                const QVector<MindMapBlock*> current = m_parentNode->blocks();
                for (MindMapBlock* block : m_dragBeforeBlocks)
                    if (!current.contains(block) && block->type() == BlockType::Text) delete block;
                for (MindMapBlock* block : transientBlocks)
                    if (!current.contains(block) && block->type() == BlockType::Text) delete block;
            }
        }
        m_dragStarted = false;
        m_dragBeforeBlocks.clear();

        event->accept();
        return;
    }

    // 转发给父节点
    if (m_parentNode) {
        m_parentNode->handleChildReleaseEvent(event);
    } else {
        QGraphicsPixmapItem::mouseReleaseEvent(event);
    }
}

void MindMapImageItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #ffffff; border: 1px solid #dadce0; border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 8px 24px; border-radius: 4px; color: #3c4043; }"
        "QMenu::item:selected { background-color: #e8f0fe; color: #1a73e8; }"));
    QAction* copyAction = menu.addAction(QStringLiteral("复制"));
    QAction* cutAction = menu.addAction(QStringLiteral("剪切"));
    QAction* deleteAction = menu.addAction(QStringLiteral("删除"));
    QAction* selected = menu.exec(event->screenPos());
    if ((selected == copyAction || selected == cutAction) && !m_originalImage.isNull()) {
        QApplication::clipboard()->setImage(m_originalImage);
    }
    if ((selected == cutAction || selected == deleteAction) && m_parentNode && m_imageBlock) {
        // 【修复】使用 RemoveBlockCommand 进入 undo 栈
        int idx = m_parentNode->blockIndex(m_imageBlock);
        if (idx >= 0) {
            if (MindMapNode::s_undoStack && m_parentNode->scene()) {
                MindMapNode::s_undoStack->push(new RemoveBlockCommand(
                    m_parentNode, idx, m_parentNode->scene()));
            } else {
                m_parentNode->removeBlockAt(idx);
                // 【新增】删除图片后，自动调整文本块宽度与节点一致
                m_parentNode->autoFitTextBlockWidths();
                m_parentNode->calcNodeSize();
                MindMapNode* root = m_parentNode->getRoot();
                if (root) {
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                }
                if (m_parentNode->scene()) m_parentNode->scene()->update();
            }
        }
    }
    event->accept();
}

void MindMapImageItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsPixmapItem::paint(painter, option, widget);
    if (m_isSelected) {
        QRectF r = boundingRect();
        QRectF selectRect = r.adjusted(-2, -2, 2, 2);
        QPen pen(QColor(0, 120, 255));
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(selectRect);

        qreal hs = HANDLE_SIZE;
        auto drawHandle = [&](const QPointF& center) {
            QRectF hr(center.x() - hs/2, center.y() - hs/2, hs, hs);
            painter->fillRect(hr, QColor(0, 120, 255));
            painter->setPen(QPen(Qt::white, 1));
            painter->drawRect(hr);
        };
        drawHandle(r.topLeft());
        drawHandle(r.topRight());
        drawHandle(r.bottomLeft());
        drawHandle(r.bottomRight());
    }
}

// ========== 悬停事件 ==========

void MindMapImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    if (m_isSelected) {
        HandlePos handle = hitTestHandle(event->pos());
        updateCursor(handle);
    } else {
        setCursor(Qt::PointingHandCursor);
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void MindMapImageItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}

// ========== 私有方法 ==========

MindMapImageItem::HandlePos MindMapImageItem::hitTestHandle(const QPointF& pos) const {
    if (!m_isSelected) return None;

    QRectF r = boundingRect();
    qreal hr = HANDLE_HIT_RADIUS;

    QRectF tl(r.left() - hr, r.top() - hr, hr * 2, hr * 2);
    QRectF tr(r.right() - hr, r.top() - hr, hr * 2, hr * 2);
    QRectF bl(r.left() - hr, r.bottom() - hr, hr * 2, hr * 2);
    QRectF br(r.right() - hr, r.bottom() - hr, hr * 2, hr * 2);

    if (tl.contains(pos)) return TopLeft;
    if (tr.contains(pos)) return TopRight;
    if (bl.contains(pos)) return BottomLeft;
    if (br.contains(pos)) return BottomRight;
    return None;
}

void MindMapImageItem::updateCursor(HandlePos pos) {
    switch (pos) {
    case TopLeft:
    case BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case TopRight:
    case BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}
