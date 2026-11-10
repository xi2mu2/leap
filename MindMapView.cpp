#include "MindMapView.h"
#include "MindMapMiniMap.h"
#include <algorithm>
#include "MindMapNode.h"
#include "MindMapImageItem.h"
#include "MindMapBlock.h"
#include "MindMapCommands.h"
#include "MindMapIO.h"
#include "MindMapHoverGeometry.h"
#include "MindMapHoverTracker.h"
#include "MindMapSceneRect.h"
#include "PerformanceMetrics.h"
#include <QGraphicsScene>
#include <QScrollBar>
#include <QSettings>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPushButton>
#include <QGraphicsPathItem>
#include <QGraphicsItemGroup>
#include <QGraphicsRectItem>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QResizeEvent>
#include <QPen>
#include <QTimer>
#include <limits>

static QJsonArray s_multiClipboardData;

const QSet<int> MindMapView::s_navKeys = {
    Qt::Key_Tab, Qt::Key_Backtab,
    Qt::Key_Return, Qt::Key_Enter,
    Qt::Key_Delete, Qt::Key_Backspace,
    Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
    Qt::Key_F2, Qt::Key_Space
};

MindMapView::MindMapView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), m_dragging(false), m_insertImgBtn(nullptr) {
    if (scene) {
        // 使用场景空间索引，让视口绘制和局部命中查询按空间筛选项目。
        // 所有项目在删除前都会显式从 scene 移除，避免索引悬空。
        scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    }
    setRenderHint(QPainter::Antialiasing);
    const QString viewportUpdateMode = qEnvironmentVariable("LEAP_VIEWPORT_UPDATE_MODE").trimmed().toLower();
    if (viewportUpdateMode == QStringLiteral("minimal")) {
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    } else if (viewportUpdateMode == QStringLiteral("bounding")) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    } else if (viewportUpdateMode == QStringLiteral("smart")) {
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    } else if (viewportUpdateMode == QStringLiteral("full")) {
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    } else {
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    }
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(Qt::white));
    setMouseTracking(true);
    // 【修复】显式设置强焦点策略，确保能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    QString scrollBarStyle = R"(
        QScrollBar:vertical {
            background: #f0f0f0;
            width: 12px;
            margin: 0px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            background: #c0c0c0;
            min-height: 30px;
            border-radius: 6px;
            margin: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background: #a0a0a0;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background: #f0f0f0;
            height: 12px;
            margin: 0px;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal {
            background: #c0c0c0;
            min-width: 30px;
            border-radius: 6px;
            margin: 2px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #a0a0a0;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
    )";
    setStyleSheet(scrollBarStyle);

    // 新建/打开文档时的默认视野缩放（新用户第一眼看到的比例）。
    scale(0.7, 0.7);

    m_miniMap = new MindMapMiniMap(this);
    updateMiniMapGeometry();

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this]() { scheduleMiniMapForViewportChange(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this]() { scheduleMiniMapForViewportChange(); });
    if (scene) {
        connect(scene, &QGraphicsScene::changed, m_miniMap, &MindMapMiniMap::invalidateCache);
        connect(scene, &QGraphicsScene::changed, this, [this]() {
            m_hoverQueryCacheDirty = true;
        });
        connect(scene, &QObject::destroyed, this, [this]() {
            resetStateForSceneChange();
        });
    }
}

QVector<MindMapNode*> MindMapView::selectedNodes() const {
    QVector<MindMapNode*> nodes;
    if (!scene()) return nodes;
    for (auto* item : scene()->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected() && !node->isEditing()) {
                nodes.append(node);
            }
        }
    }
    return nodes;
}

int MindMapView::selectedNodeCount() const {
    return selectedNodes().size();
}

void MindMapView::invalidateHoverQueryCache() {
    m_hoverQueryRect = QRectF();
    m_hoverQueryItems.clear();
    m_hasHoverQueryCache = false;
    m_hoverQueryCacheDirty = false;
}

void MindMapView::resetStateForSceneChange() {
    invalidateHoverQueryCache();
    m_hoverTracker.clear(scene());
    resetDragState();
    m_isResizingNode = false;
    m_resizingNode = nullptr;
    m_resizeEdge = 0;
    m_dragging = false;
    m_rightDragReported = false;
}

void MindMapView::resetForSceneChange() {
    resetStateForSceneChange();
}

void MindMapView::paintEvent(QPaintEvent* event) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::ViewPaint);
    QGraphicsView::paintEvent(event);
}

void MindMapView::clearNodeSelection(MindMapNode* exceptNode) {
    if (!scene()) return;
    for (auto* item : scene()->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node != exceptNode) {
                node->setNodeSelected(false);
                if (node->selectedBlock()) {
                    node->clearBlockSelectionAndUpdate();
                }
            }
        }
    }
}

void MindMapView::notifyGuideEditingExitedByKey() {
    emit guideEditingExitedByKey();
}

void MindMapView::updateRubberBandSelection(const QRectF& selectionRect) {
    if (!scene()) return;
    for (auto* item : scene()->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            const QRectF nodeRect = node->mapRectToScene(node->rect());
            node->setNodeSelected(selectionRect.intersects(nodeRect));
            if (node->selectedBlock()) {
                node->clearBlockSelectionAndUpdate();
            }
        }
    }
}

bool MindMapView::hasSelectedAncestor(MindMapNode* node, const QVector<MindMapNode*>& nodes) const {
    if (!node) return false;
    MindMapNode* parent = node->parentNode();
    while (parent) {
        if (nodes.contains(parent)) return true;
        parent = parent->parentNode();
    }
    return false;
}

QVector<MindMapNode*> MindMapView::selectedRootNodes() const {
    QVector<MindMapNode*> nodes = selectedNodes();
    QVector<MindMapNode*> roots;
    for (MindMapNode* node : nodes) {
        if (node && !hasSelectedAncestor(node, nodes)) {
            roots.append(node);
        }
    }
    return roots;
}

MindMapNode* MindMapView::firstSelectedNode() const {
    QVector<MindMapNode*> nodes = selectedNodes();
    return nodes.isEmpty() ? nullptr : nodes.first();
}

void MindMapView::copySelectedNodesToClipboard() {
    QVector<MindMapNode*> nodes = selectedRootNodes();
    if (nodes.isEmpty()) return;

    s_multiClipboardData = QJsonArray();
    int index = 1;
    for (MindMapNode* node : nodes) {
        if (!node || node->isEditing()) continue;
        s_multiClipboardData.append(MindMapIO::serialize(node, QStringLiteral("clipboard_%1").arg(index++)));
    }

    if (!s_multiClipboardData.isEmpty()) {
        MindMapNode::s_clipboardData = QJsonObject();
        MindMapNode::s_hasClipboard = false;
    }
}

void MindMapView::pasteMultiClipboardTo(MindMapNode* targetNode) {
    if (!targetNode || s_multiClipboardData.isEmpty() || !scene()) return;

    if (MindMapNode::s_undoStack) {
        MindMapNode::s_undoStack->beginMacro(QStringLiteral("粘贴多个节点"));
    }

    QVector<MindMapNode*> pastedNodes;
    for (const QJsonValue& value : s_multiClipboardData) {
        if (!value.isObject()) continue;
        MindMapNode* cloned = MindMapIO::deserialize(value.toObject(), nullptr);
        if (!cloned) continue;
        pastedNodes.append(cloned);
        if (MindMapNode::s_undoStack) {
            MindMapNode::s_undoStack->push(new PasteSubtreeCommand(targetNode, cloned, scene()));
        } else {
            targetNode->addChild(cloned);
            if (!cloned->scene()) scene()->addItem(cloned);
            for (auto* child : cloned->childNodes()) {
                if (!child->scene()) scene()->addItem(child);
            }
        }
    }

    if (MindMapNode::s_undoStack) {
        MindMapNode::s_undoStack->endMacro();
    } else {
        MindMapNode* root = targetNode->getRoot();
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
        scene()->update();
    }

    if (!pastedNodes.isEmpty()) {
        clearNodeSelection();
        for (MindMapNode* node : pastedNodes) {
            node->setNodeSelected(true);
        }
        ensureSceneRectContains();
        ensureNodeVisible(pastedNodes.first());
    }
}

void MindMapView::deleteSelectedNodes() {
    if (!scene()) return;

    QVector<MindMapNode*> nodes = selectedNodes();
    QVector<MindMapNode*> removableNodes;
    for (MindMapNode* node : nodes) {
        if (node && node->parentNode() && !hasSelectedAncestor(node, nodes)) {
            removableNodes.append(node);
        }
    }
    if (removableNodes.isEmpty()) return;

    if (MindMapNode::s_undoStack) {
        MindMapNode::s_undoStack->beginMacro(QStringLiteral("删除多个节点"));
        for (MindMapNode* node : removableNodes) {
            MindMapNode::s_undoStack->push(new RemoveNodeCommand(node, scene()));
        }
        MindMapNode::s_undoStack->endMacro();
    } else {
        for (MindMapNode* node : removableNodes) {
            MindMapNode* parent = node->parentNode();
            if (!parent) continue;
            parent->removeChild(node);
            if (node->scene()) scene()->removeItem(node);
            node->setNodeSelected(false);
            MindMapNode* root = parent->getRoot();
            root->clearEdges();
            root->layoutTree(0, 0);
            root->adjustLayout();
            root->updateAllEdges();
        }
        scene()->update();
    }
}

void MindMapView::updateMiniMapGeometry() {
    if (!m_miniMap) return;

    const int margin = 18;
    const QSize size = m_miniMap->size();
    const QRect viewportRect = viewport()->geometry();
    const int x = viewportRect.right() - size.width() - margin;
    const int y = viewportRect.bottom() - size.height() - margin;
    m_miniMap->move(qMax(margin, x), qMax(margin, y));
}

void MindMapView::showMiniMapForViewportChange() {
    updateMiniMapGeometry();
    if (m_miniMap) {
        m_miniMap->showForMovement();
    }
}

void MindMapView::scheduleMiniMapForViewportChange() {
    if (m_miniMapUpdatePending) return;
    m_miniMapUpdatePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_miniMapUpdatePending = false;
        showMiniMapForViewportChange();
    });
}

void MindMapView::updateDropIndicator() {
    if (m_dropIndicator) {
        if (m_dropIndicator->scene()) m_dropIndicator->scene()->removeItem(m_dropIndicator);
        delete m_dropIndicator;
        m_dropIndicator = nullptr;
    }

    if (!m_dropTargetNode) return;

    QRectF targetRect = m_dropTargetNode->mapRectToScene(m_dropTargetNode->rect());
    qreal cx = targetRect.right();
    qreal cy = targetRect.top() + targetRect.height() / 2;

    qreal insertY = cy;
    auto children = m_dropTargetNode->childNodes();

    if (m_dropInsertIndex >= 0 && m_dropInsertIndex <= children.size()) {
        if (m_dropInsertIndex == 0) {
            if (!children.isEmpty()) {
                QRectF firstRect = children[0]->mapRectToScene(children[0]->rect());
                insertY = firstRect.top() - 20;
            } else {
                insertY = targetRect.bottom() + 20;
            }
        } else if (m_dropInsertIndex >= children.size()) {
            QRectF lastRect = children.last()->mapRectToScene(children.last()->rect());
            insertY = lastRect.bottom() + 20;
        } else {
            QRectF aboveRect = children[m_dropInsertIndex - 1]->mapRectToScene(children[m_dropInsertIndex - 1]->rect());
            QRectF belowRect = children[m_dropInsertIndex]->mapRectToScene(children[m_dropInsertIndex]->rect());
            insertY = (aboveRect.bottom() + belowRect.top()) / 2;
        }
    }

    QPainterPath path;
    qreal midX = cx + 40;
    path.moveTo(cx, cy);
    path.lineTo(midX, cy);
    path.lineTo(midX, insertY);
    path.lineTo(midX + 15, insertY);

    QGraphicsPathItem* pathItem = new QGraphicsPathItem(path);
    QPen pathPen(QColor(0, 120, 255));
    pathPen.setWidth(2);
    pathPen.setStyle(Qt::DashLine);
    pathItem->setPen(pathPen);
    pathItem->setZValue(1000);

    QGraphicsRectItem* boxItem = new QGraphicsRectItem(midX + 15, insertY - 15, 60, 30);
    QPen boxPen(QColor(0, 120, 255));
    boxPen.setWidth(2);
    boxPen.setStyle(Qt::DashLine);
    boxItem->setPen(boxPen);
    boxItem->setBrush(QBrush(QColor(0, 120, 255, 30)));
    boxItem->setZValue(1000);

    m_dropIndicator = new QGraphicsItemGroup();
    static_cast<QGraphicsItemGroup*>(m_dropIndicator)->addToGroup(pathItem);
    static_cast<QGraphicsItemGroup*>(m_dropIndicator)->addToGroup(boxItem);
    m_dropIndicator->setZValue(1000);
    scene()->addItem(m_dropIndicator);
}

static QRectF getSubtreeBoundingRect(MindMapNode* node) {
    if (!node) return QRectF();
    QRectF rect = node->mapRectToScene(node->rect());
    if (node->isExpanded()) {
        for (auto* child : node->childNodes()) {
            QRectF childRect = getSubtreeBoundingRect(child);
            rect = rect.united(childRect);
        }
    }
    return rect;
}

void MindMapView::ensureSceneRectContains() {
    if (!scene()) return;

    QRectF currentRect = scene()->sceneRect();
    QRectF itemsBoundingRect;
    bool hasItems = false;

    for (auto* item : scene()->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            QRectF nodeRect = node->mapRectToScene(node->boundingRect());
            if (!hasItems) {
                itemsBoundingRect = nodeRect;
                hasItems = true;
            } else {
                itemsBoundingRect = itemsBoundingRect.united(nodeRect);
            }
        }
    }

    if (!hasItems) return;

    const QRectF newRect = MindMapSceneRect::expandedToContain(
        currentRect, itemsBoundingRect);
    if (newRect == currentRect) return;

    QPointF oldCenter = mapToScene(viewport()->rect().center());
    scene()->setSceneRect(newRect);
    centerOn(oldCenter);
}

void MindMapView::ensureNodeVisible(MindMapNode* node, qreal margin) {
    if (!node || !scene()) return;
    QRectF nodeRect = node->mapRectToScene(node->boundingRect());
    QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();

    bool needScroll = false;

    if (nodeRect.left() < viewRect.left() + margin) {
        needScroll = true;
    } else if (nodeRect.right() > viewRect.right() - margin) {
        needScroll = true;
    }

    if (nodeRect.top() < viewRect.top() + margin) {
        needScroll = true;
    } else if (nodeRect.bottom() > viewRect.bottom() - margin) {
        needScroll = true;
    }

    if (needScroll) {
        ensureVisible(node, margin, margin);
    }
}

void MindMapView::ensureNodeInEditSafeZone(MindMapNode* node, qreal horizontalMargin,
                                           qreal verticalMargin) {
    if (!node || !scene() || !viewport()) return;

    const QRect viewportRect = viewport()->rect();
    if (viewportRect.isEmpty()) return;

    // 安全区按视图像素定义，避免缩放后编辑体验随场景坐标改变。
    const QRectF safeViewport = viewportRect.adjusted(
        qRound(horizontalMargin), qRound(verticalMargin),
        -qRound(horizontalMargin), -qRound(verticalMargin));
    if (safeViewport.width() <= 0 || safeViewport.height() <= 0) {
        const QRectF nodeViewport = mapFromScene(
            node->mapRectToScene(node->boundingRect())).boundingRect();
        const qreal margin = 20.0;
        int targetX = horizontalScrollBar()->value();
        int targetY = verticalScrollBar()->value();
        if (nodeViewport.left() < margin) targetX += qRound(nodeViewport.left() - margin);
        else if (nodeViewport.right() > viewportRect.width() - margin)
            targetX += qRound(nodeViewport.right() - (viewportRect.width() - margin));
        if (nodeViewport.top() < margin) targetY += qRound(nodeViewport.top() - margin);
        else if (nodeViewport.bottom() > viewportRect.height() - margin)
            targetY += qRound(nodeViewport.bottom() - (viewportRect.height() - margin));
        animateViewportTo(targetX, targetY);
        return;
    }

    const QRectF nodeViewport = mapFromScene(
        node->mapRectToScene(node->boundingRect())).boundingRect();
    if (safeViewport.contains(nodeViewport)) return;

    const QPointF nodeCenter = node->mapRectToScene(node->boundingRect()).center();
    // 编辑目标略偏右，为左侧父节点和连接线保留空间。
    const qreal offsetPixels = qMin<qreal>(80.0, safeViewport.width() * 0.15);
    const QPoint desiredNodeViewportCenter(
        qRound(safeViewport.center().x() + offsetPixels),
        qRound(safeViewport.center().y()));
    const QPoint nodeCenterViewport = mapFromScene(nodeCenter);
    animateViewportTo(
        horizontalScrollBar()->value() + nodeCenterViewport.x() - desiredNodeViewportCenter.x(),
        verticalScrollBar()->value() + nodeCenterViewport.y() - desiredNodeViewportCenter.y());
}

void MindMapView::animateViewportTo(int horizontalValue, int verticalValue) {
    stopViewportAnimation();
    horizontalValue = qBound(horizontalScrollBar()->minimum(), horizontalValue,
                             horizontalScrollBar()->maximum());
    verticalValue = qBound(verticalScrollBar()->minimum(), verticalValue,
                           verticalScrollBar()->maximum());
    if (horizontalValue == horizontalScrollBar()->value() &&
        verticalValue == verticalScrollBar()->value()) return;

    const int durationMs = qBound(0, QSettings().value(
        QStringLiteral("smoothViewportDurationMs"), 200).toInt(), 500);
    if (durationMs == 0) {
        horizontalScrollBar()->setValue(horizontalValue);
        verticalScrollBar()->setValue(verticalValue);
        return;
    }

    auto* group = new QParallelAnimationGroup(this);
    auto* horizontal = new QPropertyAnimation(horizontalScrollBar(), "value", group);
    horizontal->setStartValue(horizontalScrollBar()->value());
    horizontal->setEndValue(horizontalValue);
    auto* vertical = new QPropertyAnimation(verticalScrollBar(), "value", group);
    vertical->setStartValue(verticalScrollBar()->value());
    vertical->setEndValue(verticalValue);
    for (auto* animation : {horizontal, vertical}) {
        animation->setDuration(durationMs);
        animation->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_viewportAnimation = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this, group] {
        if (m_viewportAnimation == group) m_viewportAnimation = nullptr;
        group->deleteLater();
    });
    group->start();
}

void MindMapView::stopViewportAnimation() {
    if (!m_viewportAnimation) return;
    m_viewportAnimation->stop();
    m_viewportAnimation->deleteLater();
    m_viewportAnimation = nullptr;
}

void MindMapView::updateInsertImageButtonState() {
    if (!m_insertImgBtn) return;

    bool hasSelected = false;
    if (scene()) {
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                if (node->isNodeSelected() || node->isEditing()) {
                    hasSelected = true;
                    break;
                }
            }
        }
    }
    m_insertImgBtn->setEnabled(hasSelected);
}

void MindMapView::wheelEvent(QWheelEvent* event) {
    stopViewportAnimation();
    if (event->modifiers() & Qt::ControlModifier) {
        qreal factor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
        scale(factor, factor);
        showMiniMapForViewportChange();
        emit zoomChanged();  // 【优化】通知缩放变化
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

// 辅助：重置拖拽状态
void MindMapView::resetDragState() {
    m_dragCandidate = false;
    m_isDraggingNode = false;
    m_isDraggingSelection = false;
    m_isRubberSelecting = false;
    m_clickCandidateNode = nullptr;
    m_draggedNode = nullptr;
    m_dropTargetNode = nullptr;
    m_dropInsertIndex = -1;
    for (MindMapNode* node : m_multiDragNodes) {
        if (node) node->setOpacity(1.0);
    }
    m_multiDragNodes.clear();
    m_multiDragStartPositions.clear();

    if (m_rubberBandItem) {
        if (m_rubberBandItem->scene()) m_rubberBandItem->scene()->removeItem(m_rubberBandItem);
        delete m_rubberBandItem;
        m_rubberBandItem = nullptr;
    }
    if (m_dragProxy) {
        if (m_dragProxy->scene()) m_dragProxy->scene()->removeItem(m_dragProxy);
        delete m_dragProxy;
        m_dragProxy = nullptr;
    }
    if (m_dropIndicator) {
        if (m_dropIndicator->scene()) m_dropIndicator->scene()->removeItem(m_dropIndicator);
        delete m_dropIndicator;
        m_dropIndicator = nullptr;
    }
    viewport()->setCursor(Qt::ArrowCursor);
}

void MindMapView::mousePressEvent(QMouseEvent* event) {
    stopViewportAnimation();
    invalidateHoverQueryCache();
    if (event->button() == Qt::LeftButton &&
        (m_dragCandidate || m_isDraggingNode || m_isResizingNode || m_isRubberSelecting)) {
        resetDragState();
        m_isResizingNode = false;
        m_resizingNode = nullptr;
        m_resizeEdge = 0;
    }
    if (event->button() == Qt::RightButton) {
        m_dragging = true;
        m_lastPos = event->pos();
        m_rightDragStartPos = event->pos();
        m_rightDragReported = false;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    QPointF scenePos = mapToScene(event->pos());

    auto prepareForNodeInteraction = [this](MindMapNode* targetNode) {
        if (!scene()) return;
        // finishEdit() may rebuild text blocks and mutate the scene item list.
        // Snapshot stable node pointers before changing any node state.
        QVector<MindMapNode*> nodes;
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) nodes.append(node);
        }
        for (auto* node : nodes) {
            if (!node) continue;
            if (node != targetNode && node->isEditing()) {
                node->finishEdit(false);
            }
            if (node != targetNode && node->selectedBlock()) {
                node->clearBlockSelectionAndUpdate();
            }
        }
        if (scene()->mouseGrabberItem()) scene()->mouseGrabberItem()->ungrabMouse();
    };

    // 检查是否点击了图片项或公式项（优先级最高，不启动拖拽）
    for (auto* item : scene()->items(scenePos)) {
        if (dynamic_cast<MindMapImageItem*>(item) || dynamic_cast<FormulaPixmapItem*>(item)) {
            MindMapNode* targetNode = nullptr;
            if (item->parentItem()) {
                if ((targetNode = dynamic_cast<MindMapNode*>(item->parentItem()))) {
                    prepareForNodeInteraction(targetNode);
                    for (auto* other : scene()->items()) {
                        if (auto* otherNode = dynamic_cast<MindMapNode*>(other)) {
                            if (otherNode != targetNode) otherNode->setNodeSelected(false);
                        }
                    }
                    targetNode->setNodeSelected(true);
                }
            }
            // 重置任何待处理的点击状态
            resetDragState();
            QGraphicsItem* currentItem = itemAt(event->pos());
            if (currentItem && (dynamic_cast<MindMapImageItem*>(currentItem) ||
                                dynamic_cast<FormulaPixmapItem*>(currentItem))) {
                QGraphicsView::mousePressEvent(event);
            } else if (targetNode) {
                targetNode->setNodeSelected(true);
                event->accept();
            }
            return;
        }
    }

    // 检查是否点击了某个节点的指示器
    for (auto* item : scene()->items(scenePos)) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            QPointF nodePos = node->mapFromScene(scenePos);
            if (node->hitIndicator(nodePos)) {
                bool willExpand = !node->isExpanded();
                if (MindMapNode::s_undoStack) {
                    MindMapNode::s_undoStack->push(new ToggleExpandCommand(node, willExpand));
                } else {
                    node->setExpanded(willExpand);
                }
                for (auto* other : scene()->items()) {
                    if (auto* otherNode = dynamic_cast<MindMapNode*>(other)) {
                        if (otherNode != node) otherNode->setNodeSelected(false);
                    }
                }
                node->setNodeSelected(true);
                resetDragState();
                event->accept();
                return;
            }
        }
    }

    // 检查编辑节点
    auto* editingNode = MindMapNode::findEditingNode(scene());
    if (editingNode) {
        auto* item = itemAt(event->pos());
        // Keep the viewport stable while finishing the editor.  Finishing an
        // edit can rebuild text items, change the scene rect, or move focus;
        // none of those should scroll the view for a plain selection click.
        const QPointF clickViewportCenter = mapToScene(viewport()->rect().center());
        bool onEditingNode = false;
        auto* p = item;
        while (p) {
            if (p == editingNode) {
                onEditingNode = true;
                break;
            }
            p = p->parentItem();
        }
        if (onEditingNode) {
            // 【修改】点击在编辑节点上，先检测是否点击了边界
            QPointF nodePos = editingNode->mapFromScene(scenePos);
            auto edge = editingNode->hitTestResizeEdge(nodePos);
            if (static_cast<int>(edge) != 0) {
                // 启动节点宽度调整
                m_isResizingNode = true;
                m_resizingNode = editingNode;
                m_resizeEdge = static_cast<int>(edge);
                m_resizeStartPos = nodePos;
                m_resizeStartWidth = editingNode->rect().width();
                // 【新增】记录节点中心在视野中的位置
                QPointF nodeCenterScene = editingNode->mapToScene(editingNode->rect().center());
                m_resizeViewportCenterPos = mapFromScene(nodeCenterScene);
                event->accept();
                return;
            }
            // 点击在编辑节点上但不是边界，取消块选中并让Qt正常路由事件
            editingNode->clearBlockSelectionAndUpdate();
            resetDragState();
            QGraphicsView::mousePressEvent(event);
            return;
        }
        // 点击在编辑节点外部，结束编辑（会自动拆分文本块）
        MindMapNode* targetNode = nullptr;
        // Resolve the target from the scene hit list before finishEdit() can
        // replace text graphics items.  itemAt() alone may return a transient
        // child item whose parent chain is changed during the relayout.
        for (auto* hit : scene()->items(scenePos)) {
            for (QGraphicsItem* current = hit; current; current = current->parentItem()) {
                if ((targetNode = dynamic_cast<MindMapNode*>(current))) break;
            }
            if (targetNode) break;
        }
        editingNode->finishEdit(false);
        // 关键：确保编辑节点及其子item不再grab鼠标
        if (scene()->mouseGrabberItem()) {
            scene()->mouseGrabberItem()->ungrabMouse();
        }
        if (targetNode && targetNode != editingNode) {
            prepareForNodeInteraction(targetNode);
            for (auto* other : scene()->items()) {
                if (auto* otherNode = dynamic_cast<MindMapNode*>(other)) {
                    if (otherNode != targetNode) otherNode->setNodeSelected(false);
                }
            }
            targetNode->setNodeSelected(true);
            centerOn(clickViewportCenter);
            resetDragState();
            event->accept();
            return;
        }
    }

    // 【新增】检查是否点击了选中节点的宽度调整边缘
    // 遍历场景中的所有节点，检查鼠标是否在选中节点的边缘
    for (auto* item : scene()->items(scenePos)) {
        // 向上查找父节点
        QGraphicsItem* checkItem = item;
        while (checkItem) {
            if (auto* node = dynamic_cast<MindMapNode*>(checkItem)) {
                if (node->isNodeSelected()) {
                    QPointF nodePos = node->mapFromScene(scenePos);
                    auto edge = node->hitTestResizeEdge(nodePos);
                    if (static_cast<int>(edge) != 0) {
                        // 启动节点宽度调整
                        m_isResizingNode = true;
                        m_resizingNode = node;
                        m_resizeEdge = static_cast<int>(edge);
                        m_resizeStartPos = nodePos;
                        m_resizeStartWidth = node->rect().width();
                        // 【新增】记录节点中心在视野中的位置
                        QPointF nodeCenterScene = node->mapToScene(node->rect().center());
                        m_resizeViewportCenterPos = mapFromScene(nodeCenterScene);
                        event->accept();
                        return;
                    }
                }
                break; // 找到节点后停止向上查找
            }
            checkItem = checkItem->parentItem();
        }
    }

    // 识别点击的节点
    QGraphicsItem* topItem = itemAt(event->pos());
    MindMapNode* clickedNode = nullptr;
    bool onChildItem = false;

    if (auto* node = dynamic_cast<MindMapNode*>(topItem)) {
        clickedNode = node;
    } else if (topItem) {
        auto* p = topItem;
        while (p) {
            if (auto* node = dynamic_cast<MindMapNode*>(p)) {
                clickedNode = node;
                // 只有图片和公式块不启动拖拽，文本块可以
                if (dynamic_cast<MindMapImageItem*>(topItem) ||
                    dynamic_cast<FormulaPixmapItem*>(topItem)) {
                    onChildItem = true;
                }
                break;
            }
            p = p->parentItem();
        }
    }

    // 双击检测（必须在设置dragCandidate之前）
    if (!clickedNode) {
        clearNodeSelection();
        m_isRubberSelecting = true;
        m_rubberBandStart = scenePos;
        if (!m_rubberBandItem && scene()) {
            m_rubberBandItem = scene()->addRect(QRectF(m_rubberBandStart, QSizeF()),
                                                QPen(QColor(37, 99, 235), 1, Qt::DashLine),
                                                QBrush(QColor(37, 99, 235, 36)));
            m_rubberBandItem->setZValue(3000);
        }
        event->accept();
        return;
    }

    prepareForNodeInteraction(clickedNode);

    if (clickedNode && !clickedNode->isEditing() && !onChildItem &&
        (event->modifiers() & Qt::ControlModifier)) {
        clickedNode->setNodeSelected(!clickedNode->isNodeSelected());
        clickedNode->clearBlockSelectionAndUpdate();
        resetDragState();
        event->accept();
        return;
    }


    // 为子节点（非根节点）启动拖拽候选
    if (clickedNode && !clickedNode->isEditing() && clickedNode->parentNode() && !onChildItem) {
        if (!clickedNode->isNodeSelected()) {
            for (auto* other : scene()->items()) {
                if (auto* otherNode = dynamic_cast<MindMapNode*>(other)) {
                    if (otherNode != clickedNode) otherNode->setNodeSelected(false);
                }
            }
            clickedNode->setNodeSelected(true);
        }
        m_dragCandidate = true;
        m_dragCandidateStartViewportPos = event->pos();
        m_clickCandidateNode = clickedNode;
        m_draggedNode = clickedNode;
        m_multiDragNodes.clear();
        const auto selection = selectedNodes();
        for (MindMapNode* selected : selection) {
            if (selected->parentNode() && !selection.contains(selected->parentNode()))
                m_multiDragNodes.append(selected);
        }
        std::stable_sort(m_multiDragNodes.begin(), m_multiDragNodes.end(),
                         [](MindMapNode* a, MindMapNode* b) {
            const QPointF pa = a->scenePos(), pb = b->scenePos();
            return pa.y() == pb.y() ? pa.x() < pb.x() : pa.y() < pb.y();
        });
    }

    // 点击空白位置：清除所有节点的块选中状态
    if (!clickedNode) {
        QVector<MindMapNode*> nodesToClear;
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                if (node->selectedBlock()) {
                    nodesToClear.append(node);
                }
            }
        }
        for (auto* node : nodesToClear) {
            node->clearBlockSelectionAndUpdate();
        }
    }

    // 如果启动了拖拽候选，拦截事件
    if (m_dragCandidate) {
        event->accept();
        return;
    }

    // 其他情况，让事件正常传递
    QGraphicsView::mousePressEvent(event);
}

void MindMapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && scene()) {
        QGraphicsItem* item = itemAt(event->pos());
        const bool onImageOrFormula = dynamic_cast<MindMapImageItem*>(item) ||
                                      dynamic_cast<FormulaPixmapItem*>(item);
        MindMapNode* clickedNode = nullptr;
        for (QGraphicsItem* current = item; current; current = current->parentItem()) {
            if (auto* node = dynamic_cast<MindMapNode*>(current)) {
                clickedNode = node;
                break;
            }
        }

        if (clickedNode && !onImageOrFormula) {
            clickedNode->setNodeSelected(true);
            clickedNode->startEdit();
            resetDragState();
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void MindMapView::mouseMoveEvent(QMouseEvent* event) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::MouseMove);
    // 【新增】处理节点宽度拖拽
    // 【修改】拖拽调整文本块宽度
    // 【新增】处理节点宽度拖拽
    // 【修改】拖拽调整文本块宽度
    // QGraphicsItem 维护所属场景指针，直接比较即可；不要在每个
    // mouseMove 中调用 scene()->items() 扫描整张思维导图。
    auto nodeInCurrentScene = [this](MindMapNode* node) {
        return node && scene() && node->scene() == scene();
    };
    if (m_isResizingNode && m_resizingNode && !nodeInCurrentScene(m_resizingNode)) {
        m_isResizingNode = false;
        m_resizingNode = nullptr;
        m_resizeEdge = 0;
    }
    if ((m_dragCandidate || m_isDraggingNode) && m_draggedNode && !nodeInCurrentScene(m_draggedNode)) {
        resetDragState();
    }
    if (m_dropTargetNode && !nodeInCurrentScene(m_dropTargetNode)) {
        m_dropTargetNode = nullptr;
        m_dropInsertIndex = -1;
        updateDropIndicator();
    }

    if (m_isResizingNode && m_resizingNode) {
        QPointF scenePos = mapToScene(event->pos());
        QPointF nodePos = m_resizingNode->mapFromScene(scenePos);
        qreal delta = 0;
        if (m_resizeEdge == 2) {
            delta = nodePos.x() - m_resizeStartPos.x();
        } else {
            delta = m_resizeStartPos.x() - nodePos.x();
        }
        qreal newWidth = m_resizeStartWidth + delta;
        newWidth = qBound(60.0, newWidth, 800.0);
        // 【修改】只更新节点宽度，不触发全局重排
        m_resizingNode->setNodeContentWidth(newWidth);
        if (scene()) scene()->update();
        event->accept();
        return;
    }

    if (m_isRubberSelecting) {
        QPointF scenePos = mapToScene(event->pos());
        QRectF selectionRect(m_rubberBandStart, scenePos);
        selectionRect = selectionRect.normalized();
        if (m_rubberBandItem) {
            m_rubberBandItem->setRect(selectionRect);
        }
        updateRubberBandSelection(selectionRect);
        event->accept();
        return;
    }



    if (m_dragging) {
        PerformanceMetrics::ScopedTimer panTimer(PerformanceMetrics::Stage::PanMouseMove);
        if (!m_rightDragReported
            && QLineF(m_rightDragStartPos, event->pos()).length() >= QApplication::startDragDistance()) {
            m_rightDragReported = true;
            emit guideViewportDragged();
        }
        QPointF delta = event->pos() - m_lastPos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPos = event->pos();
        return;
    }

    if (m_dragCandidate && !m_isDraggingNode && m_draggedNode) {
        // 使用视图坐标检测移动距离（不受缩放影响）
        qreal dist = QLineF(m_dragCandidateStartViewportPos, event->pos()).length();
        if (dist >= QApplication::startDragDistance()) {
            // 超过阈值，真正启动拖拽
            m_isDraggingNode = true;
            m_dragStartPos = mapToScene(m_dragCandidateStartViewportPos);

            // 取消任何Item的鼠标抓取，让View完全接管
            if (scene()->mouseGrabberItem()) {
                scene()->mouseGrabberItem()->ungrabMouse();
            }

            if (!m_dragProxy && scene()) {
                QPainterPath path;
                QRectF nodeRect = m_draggedNode->rect();
                qreal proxyW = qMax(nodeRect.width(), 100.0);
                qreal proxyH = qMax(nodeRect.height(), 40.0);
                path.addRoundedRect(0, 0, proxyW, proxyH, 8, 8);
                m_dragProxy = new QGraphicsPathItem(path);
                QColor fillColor = m_draggedNode->m_fillColor;
                fillColor.setAlpha(128);
                m_dragProxy->setBrush(fillColor);
                QPen proxyPen(m_draggedNode->m_borderColor);
                proxyPen.setWidth(1);
                m_dragProxy->setPen(proxyPen);
                m_dragProxy->setZValue(2000);
                scene()->addItem(m_dragProxy);
            }
            m_draggedNode->setOpacity(0.5);
            for (MindMapNode* node : m_multiDragNodes) node->setOpacity(0.5);
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
    }

    if (m_isDraggingNode && m_draggedNode) {
        QPointF scenePos = mapToScene(event->pos());
        if (m_dragProxy) {
            QRectF proxyRect = m_dragProxy->boundingRect();
            m_dragProxy->setPos(scenePos.x() - proxyRect.width() / 2,
                                scenePos.y() - proxyRect.height() / 2);
        }

        MindMapNode* targetNode = nullptr;
        int insertIndex = -1;

        qreal bestScore = std::numeric_limits<qreal>::max();
        const qreal maxAttachDistance = 180.0;

        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                if (m_multiDragNodes.contains(node)) continue;
                bool isDescendant = false;
                MindMapNode* p = node->parentNode();
                while (p) {
                    if (m_multiDragNodes.contains(p)) {
                        isDescendant = true;
                        break;
                    }
                    p = p->parentNode();
                }
                if (isDescendant) continue;

                const QRectF nodeRect = node->mapRectToScene(node->rect());
                const QPointF anchor(nodeRect.right(), nodeRect.center().y());
                const qreal dx = scenePos.x() - anchor.x();
                const qreal dy = scenePos.y() - anchor.y();
                if (dx < -35 || dx > maxAttachDistance) continue;

                qreal verticalAllowance = qMax<qreal>(nodeRect.height() * 1.3, 70.0);
                if (node->isExpanded() && !node->childNodes().isEmpty()) {
                    for (MindMapNode* child : node->childNodes()) {
                        QRectF childRect = child->mapRectToScene(child->rect());
                        verticalAllowance = qMax(verticalAllowance, qAbs(childRect.center().y() - anchor.y()) + 45.0);
                    }
                }
                if (qAbs(dy) > verticalAllowance) continue;

                const qreal score = qAbs(dx) * 0.8 + qAbs(dy);
                if (score < bestScore) {
                    bestScore = score;
                    targetNode = node;

                    if (node->isExpanded() && !node->childNodes().isEmpty()) {
                        auto children = node->childNodes();
                        QRectF firstRect = children[0]->mapRectToScene(children[0]->rect());
                        if (scenePos.y() < firstRect.center().y()) {
                            insertIndex = 0;
                        } else {
                            for (int i = 1; i < children.size(); ++i) {
                                QRectF childRect = children[i]->mapRectToScene(children[i]->rect());
                                if (scenePos.y() < childRect.center().y()) {
                                    insertIndex = i;
                                    break;
                                }
                            }
                            if (insertIndex < 0) {
                                insertIndex = children.size();
                            }
                        }
                    } else {
                        insertIndex = 0;
                    }
                }
            }
        }

        m_dropTargetNode = targetNode;
        m_dropInsertIndex = insertIndex;
        updateDropIndicator();
        event->accept();
        return;
    }

    QPointF hoverScenePos = mapToScene(event->pos());
    const qreal hoverHorizontalReach = MindMapNode::INDICATOR_HIT_RADIUS + 1.0;
    const qreal hoverRightReach = MindMapNode::H_GAP / 2 + 1.0;
    const qreal hoverVerticalReach = MindMapNode::INDICATOR_HIT_RADIUS + 1.0;
    // 查询区域带缓冲，鼠标在附近小范围移动时复用结果，避免每个
    // 高频 mouseMove 都重新触发 QGraphicsScene 的项目查询。
    const QRectF queryRect = MindMapHoverGeometry::candidateSearchRect(
        hoverScenePos, hoverRightReach * 4.0, hoverHorizontalReach * 4.0,
        hoverVerticalReach * 4.0);
    if (!m_hasHoverQueryCache || m_hoverQueryCacheDirty ||
        !m_hoverQueryRect.contains(hoverScenePos)) {
        PerformanceMetrics::ScopedTimer queryTimer(PerformanceMetrics::Stage::HoverQuery);
        m_hoverQueryItems = scene()->items(queryRect, Qt::IntersectsItemBoundingRect);
        m_hoverQueryRect = queryRect;
        m_hasHoverQueryCache = true;
        m_hoverQueryCacheDirty = false;
    }
    const QList<QGraphicsItem*>& nearbyItems = m_hoverQueryItems;

    // 【新增】检测选中节点的宽度调整边缘悬停（更新光标）
    if (!m_isDraggingNode && !m_dragCandidate && !m_isResizingNode) {
        bool onResizeEdge = false;
        for (auto* item : nearbyItems) {
            // 向上查找父节点
            QGraphicsItem* checkItem = item;
            while (checkItem) {
                if (auto* node = dynamic_cast<MindMapNode*>(checkItem)) {
                    if (node->isNodeSelected()) {
                        QPointF nodePos = node->mapFromScene(hoverScenePos);
                        auto edge = node->hitTestResizeEdge(nodePos);
                        if (static_cast<int>(edge) != 0) {
                            viewport()->setCursor(Qt::SizeHorCursor);
                            onResizeEdge = true;
                            break;
                        }
                    }
                    break;
                }
                checkItem = checkItem->parentItem();
            }
            if (onResizeEdge) break;
        }
        if (!onResizeEdge && !m_dragging) {
            viewport()->setCursor(Qt::ArrowCursor);
        }
    }

    {
        PerformanceMetrics::ScopedTimer stateTimer(PerformanceMetrics::Stage::HoverStateUpdate);
        m_hoverTracker.update(scene(), hoverScenePos, nearbyItems);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void MindMapView::mouseReleaseEvent(QMouseEvent* event) {
    invalidateHoverQueryCache();
    auto nodeInCurrentScene = [this](MindMapNode* node) {
        return node && scene() && node->scene() == scene();
    };
    if ((m_dragCandidate || m_isDraggingNode) && m_draggedNode && !nodeInCurrentScene(m_draggedNode)) {
        resetDragState();
    }
    if (m_isResizingNode && m_resizingNode && !nodeInCurrentScene(m_resizingNode)) {
        m_isResizingNode = false;
        m_resizingNode = nullptr;
        m_resizeEdge = 0;
    }
    // 【新增】结束节点宽度拖拽
    // 【修改】结束文本块宽度拖拽
    // 【新增】结束节点宽度拖拽
    // 【修改】结束文本块宽度拖拽
    if (m_isResizingNode && event->button() == Qt::LeftButton) {
        if (m_resizingNode) {
            qreal newWidth = m_resizingNode->rect().width();
            bool pushed = false;
            // 如果宽度有变化，推入操作栈
            if (qAbs(newWidth - m_resizeStartWidth) > 0.5) {
                if (MindMapNode::s_undoStack && scene()) {
                    MindMapNode::s_undoStack->push(new ResizeNodeCommand(
                        m_resizingNode, m_resizeStartWidth, newWidth, scene()));
                    pushed = true;
                }
            }
            // 【修改】只在释放时触发一次全局重排和视野调整
            MindMapNode* root = m_resizingNode->getRoot();
            if (root) {
                root->clearEdges();
                root->layoutTree(0, 0);
                root->adjustLayout();
                root->updateAllEdges();
                // 确保被调整的节点在视野中
                ensureNodeVisible(m_resizingNode);
            }
            if (scene()) scene()->update();
        }
        m_isResizingNode = false;
        m_resizingNode = nullptr;
        m_resizeEdge = 0;
        event->accept();
        return;
    }


    if (event->button() == Qt::RightButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    if (m_isRubberSelecting) {
        if (m_rubberBandItem) {
            if (m_rubberBandItem->scene()) m_rubberBandItem->scene()->removeItem(m_rubberBandItem);
            delete m_rubberBandItem;
            m_rubberBandItem = nullptr;
        }
        m_isRubberSelecting = false;
        event->accept();
        return;
    }

    if (m_isDraggingNode) {
        // 拖拽结束，执行放置
        if (m_draggedNode) {
            m_draggedNode->setOpacity(1.0);
        }

        if (m_dragProxy) {
            if (m_dragProxy->scene()) m_dragProxy->scene()->removeItem(m_dragProxy);
            delete m_dragProxy;
            m_dragProxy = nullptr;
        }

        viewport()->setCursor(Qt::ArrowCursor);

        if (m_dropTargetNode && !m_multiDragNodes.isEmpty()) {
            auto* command = new MultiReparentCommand(m_multiDragNodes, m_dropTargetNode,
                                                     m_dropInsertIndex, scene());
            if (MindMapNode::s_undoStack) MindMapNode::s_undoStack->push(command);
            else { command->redo(); delete command; }
        } else if (m_draggedNode && m_dropTargetNode) {
            MindMapNode* oldParent = m_draggedNode->parentNode();
            int oldIndex = -1;
            if (oldParent) {
                auto siblings = oldParent->childNodes();
                oldIndex = siblings.indexOf(m_draggedNode);
            }

            MindMapNode* newParent = m_dropTargetNode;
            int newIndex = m_dropInsertIndex;
            if (newIndex < 0) newIndex = newParent->childNodes().size();

            if (MindMapNode::s_undoStack && scene()) {
                MindMapNode::s_undoStack->push(
                    new DragMoveCommand(m_draggedNode, oldParent, oldIndex, newParent, newIndex, scene())
                    );
            } else {
                if (oldParent) oldParent->removeChild(m_draggedNode);
                newParent->insertChild(newIndex, m_draggedNode);
                if (scene()) {
                    if (!m_draggedNode->scene()) scene()->addItem(m_draggedNode);
                    for (auto* child : m_draggedNode->childNodes()) {
                        if (!child->scene()) scene()->addItem(child);
                    }
                }
                MindMapNode* root = newParent->getRoot();
                root->clearEdges();
                root->layoutTree(0, 0);
                root->adjustLayout();
                root->updateAllEdges();
                if (scene()) scene()->update();
            }
        }

        resetDragState();
        event->accept();
        return;
    }

    if (m_dragCandidate) {
        // 未真正启动拖拽，记录为一次点击（用于后续双击检测）
        m_dragCandidate = false;

        if (m_clickCandidateNode) {
            m_clickCandidateNode->setNodeSelected(true);
            emit guideNodeSelected();
            m_clickCandidateNode = nullptr;
        }
        m_draggedNode = nullptr;
        event->accept();
        return;
    }

    // 非候选状态下的release，重置双击记录
    QGraphicsView::mouseReleaseEvent(event);
}

void MindMapView::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_A && !MindMapNode::findEditingNode(scene())) {
            if (scene()) {
                for (auto* item : scene()->items()) {
                    if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                        node->setNodeSelected(true);
                        if (node->selectedBlock()) node->clearBlockSelectionAndUpdate();
                    }
                }
            }
            event->accept();
            return;
        }
        switch (event->key()) {
        case Qt::Key_S: emit saveRequested(); return;
        case Qt::Key_O: emit openRequested(); return;
        case Qt::Key_N: emit newRequested(); return;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight: {
            if (selectedNodeCount() > 1 && !MindMapNode::findEditingNode(scene())) {
                const bool expand = (event->key() == Qt::Key_BracketRight);
                QVector<MindMapNode*> nodes = selectedNodes();
                if (MindMapNode::s_undoStack) {
                    MindMapNode::s_undoStack->beginMacro(expand ? QStringLiteral("展开多个节点") : QStringLiteral("折叠多个节点"));
                    for (MindMapNode* node : nodes) {
                        if (node) MindMapNode::s_undoStack->push(new ToggleExpandCommand(node, expand));
                    }
                    MindMapNode::s_undoStack->endMacro();
                } else {
                    for (MindMapNode* node : nodes) {
                        if (node) node->setExpanded(expand);
                    }
                    if (scene()) scene()->update();
                }
                event->accept();
                return;
            }
            break;
        }
        case Qt::Key_C: {
            auto* editingNode = MindMapNode::findEditingNode(scene());
            if (editingNode) break;
            if (selectedNodeCount() > 1) {
                copySelectedNodesToClipboard();
                event->accept();
                return;
            }
            for (auto* item : scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node->isNodeSelected() && !node->isEditing()) {
                        MindMapNode::s_clipboardData = MindMapIO::serialize(node, "clipboard");
                        MindMapNode::s_hasClipboard = true;
                        event->accept();
                        return;
                    }
                }
            }
            break;
        }
        case Qt::Key_X: {
            auto* editingNode = MindMapNode::findEditingNode(scene());
            if (editingNode) break;
            if (selectedNodeCount() > 1) {
                copySelectedNodesToClipboard();
                deleteSelectedNodes();
                event->accept();
                return;
            }
            for (auto* item : scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node->isNodeSelected() && !node->isEditing()) {
                        MindMapNode::s_clipboardData = MindMapIO::serialize(node, "clipboard");
                        MindMapNode::s_hasClipboard = true;
                        if (node->parentNode() && MindMapNode::s_undoStack && scene()) {
                            MindMapNode::s_undoStack->push(new RemoveNodeCommand(node, scene()));
                            event->accept();
                            return;
                        }
                        break;
                    }
                }
            }
            break;
        }
        case Qt::Key_V: {
            if (selectedNodeCount() > 1 && !MindMapNode::findEditingNode(scene())) {
                MindMapNode* targetNode = firstSelectedNode();
                if (targetNode && !s_multiClipboardData.isEmpty()) {
                    pasteMultiClipboardTo(targetNode);
                }
                event->accept();
                return;
            }
            // 优先检测剪贴板图片：粘贴到当前选中/编辑节点
            const QMimeData* mimeData = QApplication::clipboard()->mimeData();
            if (mimeData && mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                if (!image.isNull()) {
                    MindMapNode* targetNode = nullptr;
                    for (auto* item : scene()->items()) {
                        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                            if (node->isNodeSelected() || node->isEditing()) {
                                targetNode = node;
                                break;
                            }
                        }
                    }
                    if (targetNode) {
                        if (!targetNode->isEditing()) {
                            targetNode->startEdit();
                        }
                        // 【修复】使用 InsertBlockCommand 进入 undo 栈
                        int insertIdx = 0;
                        if (targetNode->selectedBlock()) {
                            insertIdx = targetNode->blockIndex(targetNode->selectedBlock()) + 1;
                        }
                        ImageBlock* imgBlock = new ImageBlock(image, targetNode);
                        static qint64 pasteImgCounter = 0;
                        imgBlock->setImageName(QString("pasteimg_%1").arg(++pasteImgCounter));
                        imgBlock->imageItem()->setImageBlock(imgBlock);

                        if (MindMapNode::s_undoStack && scene()) {
                            MindMapNode::s_undoStack->push(new InsertBlockCommand(targetNode, imgBlock, insertIdx, scene()));
                        } else {
                            targetNode->addBlock(imgBlock, insertIdx);
                            imgBlock->imageItem()->setImageBlock(imgBlock);
                            targetNode->setSelectedBlock(imgBlock);
                            MindMapNode* root = targetNode->getRoot();
                            root->clearEdges();
                            root->layoutTree(0, 0);
                            root->adjustLayout();
                            root->updateAllEdges();
                            // 【新增】粘贴图片后，自动调整文本块宽度与节点一致
                            targetNode->autoFitTextBlockWidths();
                            targetNode->calcNodeSize();
                            root->clearEdges();
                            root->layoutTree(0, 0);
                            root->adjustLayout();
                            root->updateAllEdges();
                            if (scene()) scene()->update();
                        }
                        event->accept();
                        return;
                    }
                }
            }

            // 剪贴板非图片：粘贴子树
            auto* editingNode = MindMapNode::findEditingNode(scene());
            if (editingNode) break;
            if (!s_multiClipboardData.isEmpty()) {
                MindMapNode* targetNode = firstSelectedNode();
                if (targetNode) {
                    pasteMultiClipboardTo(targetNode);
                    event->accept();
                    return;
                }
            }
            if (!MindMapNode::s_hasClipboard || MindMapNode::s_clipboardData.isEmpty()) break;
            for (auto* item : scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node->isNodeSelected() && !node->isEditing()) {
                        MindMapNode* cloned = MindMapIO::deserialize(MindMapNode::s_clipboardData, nullptr);
                        if (!cloned) break;
                        if (MindMapNode::s_undoStack && scene()) {
                            MindMapNode::s_undoStack->push(new PasteSubtreeCommand(node, cloned, scene()));
                        } else {
                            node->addChild(cloned);
                            if (scene() && !cloned->scene()) scene()->addItem(cloned);
                            for (auto* child : cloned->childNodes()) {
                                if (scene() && !child->scene()) scene()->addItem(child);
                            }
                            MindMapNode* root = node->getRoot();
                            root->clearEdges();
                            root->layoutTree(0, 0);
                            root->adjustLayout();
                            root->updateAllEdges();
                            if (scene()) scene()->update();
                            if (!scene()->views().isEmpty()) {
                                if (auto* view = dynamic_cast<MindMapView*>(scene()->views().first())) {
                                    view->ensureSceneRectContains();
                                    view->ensureNodeVisible(cloned);
                                }
                            }
                        }
                        event->accept();
                        return;
                    }
                }
            }
            break;
        }
        }
    }

    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        auto* editingNode = MindMapNode::findEditingNode(scene());
        if (editingNode) {
            bool shift = (event->key() == Qt::Key_Backtab) || (event->modifiers() & Qt::ShiftModifier);
            editingNode->handleEditTab(shift);
            if (!shift) emit guideEditingExitedByKey();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape && !event->modifiers()) {
        auto* editingNode = MindMapNode::findEditingNode(scene());
        if (editingNode) {
            editingNode->finishEdit();
            return;
        }
    }

    if (selectedNodeCount() > 1 && !MindMapNode::findEditingNode(scene())) {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            deleteSelectedNodes();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
            event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            const qreal step = (event->modifiers() & Qt::ShiftModifier) ? 24.0 : 8.0;
            QPointF delta;
            if (event->key() == Qt::Key_Left) delta.setX(-step);
            if (event->key() == Qt::Key_Right) delta.setX(step);
            if (event->key() == Qt::Key_Up) delta.setY(-step);
            if (event->key() == Qt::Key_Down) delta.setY(step);

            QVector<MindMapNode*> nodes = selectedNodes();
            QVector<QPointF> oldPositions;
            QVector<QPointF> newPositions;
            for (MindMapNode* node : nodes) {
                oldPositions.append(node->pos());
                newPositions.append(node->pos() + delta);
            }
            if (MindMapNode::s_undoStack && scene()) {
                MindMapNode::s_undoStack->push(new MultiMoveCommand(nodes, oldPositions, newPositions, scene()));
            } else {
                for (int i = 0; i < nodes.size(); ++i) {
                    nodes[i]->setPos(newPositions[i]);
                }
                if (scene()) scene()->update();
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab ||
            event->key() == Qt::Key_F2 || event->key() == Qt::Key_Space) {
            event->accept();
            return;
        }
    }

    if (s_navKeys.contains(event->key())) {
        auto* editingNode = MindMapNode::findEditingNode(scene());
        if (!editingNode) {
            for (auto* item : scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node->isNodeSelected() && !node->isEditing()) {
                        node->handleKeyPress(event);
                        if (event->key() == Qt::Key_Tab) emit guideChildCreated();
                        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) emit guideSiblingCreated();
                        if (event->key() == Qt::Key_Space) emit guideEditingStarted();
                        event->accept();  // 【关键修复】阻止 Qt 默认焦点导航
                        return;
                    }
                }
            }
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void MindMapView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    updateMiniMapGeometry();
    if (m_miniMap && m_miniMap->isVisible()) {
        m_miniMap->update();
    }
}

bool MindMapView::focusNextPrevChild(bool next) {
    // 【修复】Tab键被Qt焦点导航系统拦截的问题
    // 当没有编辑节点时，Tab应创建子节点而不是切换焦点
    auto* editingNode = MindMapNode::findEditingNode(scene());
    if (editingNode) {
        // 编辑状态下，让Qt处理Tab（插入制表符或结束编辑）
        return QGraphicsView::focusNextPrevChild(next);
    }

    if (selectedNodeCount() > 1) {
        return true;
    }

    // 非编辑状态：查找选中节点并创建子节点
    for (auto* item : scene()->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected() && !node->isEditing()) {
                // 模拟Tab键按下，触发创建子节点
                QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
                node->handleKeyPress(&tabEvent);
                emit guideChildCreated();
                return true;  // 返回true阻止默认焦点导航
            }
        }
    }

    // 没有选中节点时，执行默认的焦点导航
    return QGraphicsView::focusNextPrevChild(next);
}
