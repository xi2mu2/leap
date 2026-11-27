#include "MindMapMiniMap.h"
#include "MindMapNode.h"
#include "PerformanceMetrics.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>

namespace {

// 落点预览与视口框共用的一套颜色，方便集中调整。
const QColor kAccent(26, 115, 232);
const QColor kSelectedNode(26, 115, 232, 220);
const QColor kNormalNode(74, 144, 226, 170);
const QColor kConnector(119, 134, 155, 115);

constexpr int kFadeDurationMs = 520;
constexpr int kHideDelayMs = 850;
constexpr int kRebuildDelayMs = 80;

} // namespace

MindMapMiniMap::MindMapMiniMap(QGraphicsView* view)
    : QWidget(view), m_view(view) {
    // 需要接收鼠标事件才能做"点击/拖拽传送"，所以这里不能是
    // WA_TransparentForMouseEvents。关闭模式靠 hide() 屏蔽交互。
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setFixedSize(kBaseWidth, kBaseHeight);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    hide();

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(kFadeDurationMs);
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        // 常亮模式或指针停在小地图上时，淡出结束也不该真的藏起来。
        if (m_displayMode == DisplayMode::Always || m_pressed || m_inside) return;
        if (m_opacityEffect && m_opacityEffect->opacity() <= 0.01) {
            hide();
        }
    });

    m_hideDelayTimer = new QTimer(this);
    m_hideDelayTimer->setSingleShot(true);
    m_hideDelayTimer->setInterval(kHideDelayMs);
    connect(m_hideDelayTimer, &QTimer::timeout, this, [this]() {
        if (m_displayMode != DisplayMode::OnMovement) return;
        if (m_pressed || m_inside) return;
        if (!m_fadeAnimation) return;
        m_fadeAnimation->stop();
        m_fadeAnimation->setStartValue(m_opacityEffect ? m_opacityEffect->opacity() : 1.0);
        m_fadeAnimation->setEndValue(0.0);
        m_fadeAnimation->start();
    });

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(kRebuildDelayMs);
    connect(m_rebuildTimer, &QTimer::timeout, this, [this]() {
        if (!m_cacheDirty) return;
        rebuildCache();
        update();
    });
}

void MindMapMiniMap::setDisplayMode(DisplayMode mode) {
    if (m_displayMode == mode) return;
    m_displayMode = mode;

    // 切模式时必须停掉动画，否则淡出结束回调里的 hide() 会把"常亮"又藏起来。
    if (m_fadeAnimation) m_fadeAnimation->stop();
    if (m_hideDelayTimer) m_hideDelayTimer->stop();

    switch (m_displayMode) {
    case DisplayMode::Always:
        setOpacityInstant(1.0);
        show();
        raise();
        update();
        break;
    case DisplayMode::OnMovement:
        setOpacityInstant(0.0);
        hide();
        break;
    case DisplayMode::Off:
        setOpacityInstant(0.0);
        clearHoverState();
        hide();
        break;
    }
}

void MindMapMiniMap::setSizePercent(int percent) {
    const int clamped = qBound(kMinSizePercent, percent, kMaxSizePercent);
    if (clamped == m_sizePercent) return;
    m_sizePercent = clamped;

    const int width = qMax(32, kBaseWidth * clamped / 100);
    const int height = qMax(24, kBaseHeight * clamped / 100);
    setFixedSize(width, height);

    // 缓存图是按旧尺寸和目标矩形生成的，必须作废重画。
    invalidateCache();
    update();
}

void MindMapMiniMap::showForMovement() {
    if (m_displayMode == DisplayMode::Off) return;
    if (!m_view || !m_view->scene()) return;

    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
    }
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(1.0);
    }

    if (!isVisible()) {
        show();
        raise();
    }
    update();

    // 常亮模式下没有"自动消失"，行为完全由可见性接管。
    if (m_displayMode == DisplayMode::OnMovement && m_hideDelayTimer && !m_inside && !m_pressed) {
        m_hideDelayTimer->start();
    }
}

void MindMapMiniMap::invalidateCache() {
    m_cacheDirty = true;
    if (m_rebuildTimer) m_rebuildTimer->start();
    update();
}

void MindMapMiniMap::rebuildCache() {
    if (!m_view || !m_view->scene()) return;

    m_nodes.clear();
    m_cachedWorldRect = QRectF();
    bool hasNodes = false;
    for (auto* item : m_view->scene()->items()) {
        auto* node = dynamic_cast<MindMapNode*>(item);
        if (!node || !node->isVisible()) continue;

        NodeSnapshot snapshot;
        snapshot.rect = node->mapRectToScene(node->rect());
        snapshot.center = snapshot.rect.center();
        snapshot.selected = node->isNodeSelected();
        if (node->parentNode()) {
            const QRectF parentRect = node->parentNode()->mapRectToScene(node->parentNode()->rect());
            snapshot.parentCenter = parentRect.center();
            snapshot.hasParent = true;
        }
        m_nodes.append(snapshot);

        if (!hasNodes) {
            m_cachedWorldRect = snapshot.rect;
            hasNodes = true;
        } else {
            m_cachedWorldRect = m_cachedWorldRect.united(snapshot.rect);
        }
    }
    if (hasNodes) m_cachedWorldRect.adjust(-120, -120, 120, 120);
    // Rasterize the map once per structural change. Viewport movement then only
    // composites this fixed-size image and draws the moving viewport rectangle.
    m_mapImage = QImage(160, 100, QImage::Format_ARGB32_Premultiplied);
    m_mapImage.fill(Qt::transparent);
    if (!m_cachedWorldRect.isEmpty()) {
        QPainter mapPainter(&m_mapImage);
        mapPainter.setRenderHint(QPainter::Antialiasing, false);
        // 栅格图像的目标就是整张缓存图，与视口框共用同一套投影公式。
        const auto fitted = MindMapMiniMapGeometry::fit(
            m_cachedWorldRect, QRectF(QPointF(0, 0), QSizeF(m_mapImage.size())));
        auto mapRect = [&](const QRectF& sceneRect) {
            QRectF mapped = fitted.worldToMap(sceneRect);
            // 缩到极小后仍要留得住可见像素，否则远处节点整片消失。
            mapped.setWidth(qMax(2.0, mapped.width()));
            mapped.setHeight(qMax(2.0, mapped.height()));
            return mapped;
        };
        mapPainter.setPen(QPen(kConnector, 1));
        for (const NodeSnapshot& node : m_nodes) {
            if (!node.hasParent) continue;
            mapPainter.drawLine(fitted.worldToMap(node.parentCenter),
                                fitted.worldToMap(node.center));
        }
        mapPainter.setPen(Qt::NoPen);
        for (const NodeSnapshot& node : m_nodes) {
            mapPainter.setBrush(node.selected ? kSelectedNode : kNormalNode);
            mapPainter.drawRoundedRect(mapRect(node.rect), 2, 2);
        }
    }
    m_cacheDirty = false;
}

QRectF MindMapMiniMap::contentRect() const {
    QRectF rect = contentsRect().adjusted(10, 10, -10, -10);
    if (rect.width() < 1 || rect.height() < 1) {
        return QRectF();
    }
    return rect;
}

MindMapMiniMapGeometry::Projection MindMapMiniMap::projection(const QRectF& targetRect) const {
    // 背景地图和视口框必须使用同一个固定世界范围，否则视口移动到
    // 缓存范围外时，缩放比例会变化，导致缓存图像与视口框错位。
    return MindMapMiniMapGeometry::fit(m_cachedWorldRect, targetRect);
}

void MindMapMiniMap::setOpacityInstant(qreal value) {
    if (m_fadeAnimation) m_fadeAnimation->stop();
    if (m_opacityEffect) m_opacityEffect->setOpacity(value);
}

QPointF MindMapMiniMap::scenePointAt(const QPointF& widgetPos) const {
    const QRectF targetRect = contentRect();
    if (targetRect.isEmpty()) return QPointF();
    return MindMapMiniMapGeometry::clampedMapToWorld(widgetPos, projection(targetRect));
}

void MindMapMiniMap::updateHoverState(const QPointF& widgetPos) {
    const QRectF targetRect = contentRect();
    if (targetRect.isEmpty() || m_cachedWorldRect.isEmpty()) {
        clearHoverState();
        return;
    }
    m_hoverScenePoint = scenePointAt(widgetPos);
    m_hoverValid = true;
    update();
}

void MindMapMiniMap::clearHoverState() {
    if (!m_hoverValid) return;
    m_hoverValid = false;
    update();
}

void MindMapMiniMap::enterEvent(QEnterEvent* event) {
    m_inside = true;
    if (m_displayMode != DisplayMode::Off) {
        if (m_hideDelayTimer) m_hideDelayTimer->stop();
        setOpacityInstant(1.0);
        if (!isVisible()) {
            show();
            raise();
        }
        updateHoverState(event->position());
    }
    QWidget::enterEvent(event);
}

void MindMapMiniMap::leaveEvent(QEvent* event) {
    m_inside = false;
    // 拖动时指针划出控件要继续按边缘跟踪，不能清掉悬停态。
    if (!m_pressed) {
        clearHoverState();
    }
    if (m_displayMode == DisplayMode::OnMovement && !m_pressed && m_hideDelayTimer) {
        m_hideDelayTimer->start();
    }
    QWidget::leaveEvent(event);
}

void MindMapMiniMap::mousePressEvent(QMouseEvent* event) {
    // 只认左键；右键/中键原样交回去，不做任何导航。
    if (event->button() != Qt::LeftButton || m_displayMode == DisplayMode::Off) {
        event->ignore();
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressPos = event->position().toPoint();
    m_pressScenePoint = scenePointAt(event->position());
    if (m_hideDelayTimer) m_hideDelayTimer->stop();
    emit interactionStarted();
    updateHoverState(event->position());
    event->accept();
}

void MindMapMiniMap::mouseMoveEvent(QMouseEvent* event) {
    if (!m_pressed) {
        updateHoverState(event->position());
        event->accept();
        return;
    }

    // 超过系统的起拖阈值才算拖动，否则仍按"点击"处理，
    // 避免手抖把单击变成一次微小的跟手平移。
    if (!m_dragging) {
        const int threshold = QApplication::startDragDistance();
        if ((event->position().toPoint() - m_pressPos).manhattanLength() < threshold) {
            updateHoverState(event->position());
            event->accept();
            return;
        }
        m_dragging = true;
    }

    // 指针处始终对应视口中心，直接改滚动条（不做缓动），保证跟手。
    m_hoverScenePoint = scenePointAt(event->position());
    m_hoverValid = true;
    emit panRequested(m_hoverScenePoint, false);
    update();
    event->accept();
}

void MindMapMiniMap::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_pressed) {
        event->ignore();
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const QPointF releasePoint = scenePointAt(event->position());
    const bool wasClick = !m_dragging;
    m_pressed = false;
    m_dragging = false;

    if (wasClick) {
        // 单击：传送到落点（平滑过渡，时长沿用 smoothViewportDurationMs）。
        emit panRequested(releasePoint, true);
        m_hoverScenePoint = releasePoint;
        m_hoverValid = true;
    }
    emit interactionFinished();

    if (m_displayMode == DisplayMode::OnMovement && !m_inside && m_hideDelayTimer) {
        m_hideDelayTimer->start();
    }
    update();
    event->accept();
}

void MindMapMiniMap::wheelEvent(QWheelEvent* event) {
    // 滚轮留给画布，缩放/滚动行为不变。
    event->ignore();
}

void MindMapMiniMap::paintHoverFeedback(QPainter& painter, const QRectF& targetRect) const {
    if (!m_hoverValid) return;
    const auto fitted = projection(targetRect);
    if (!fitted.isValid() || !m_view || !m_view->scene()) return;

    // 落点预览框：以落点为视口中心时，视野将覆盖的范围。
    const QRectF visibleSceneRect = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    if (!visibleSceneRect.isEmpty()) {
        QRectF preview = visibleSceneRect;
        preview.moveCenter(m_hoverScenePoint);
        QRectF mappedPreview = fitted.worldToMap(preview);
        mappedPreview = mappedPreview.intersected(targetRect);
        if (!mappedPreview.isEmpty()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 150),
                                1, Qt::DashLine));
            painter.drawRect(mappedPreview);
        }
    }

    // 落点十字：让用户明确看到"中心会去哪"。
    const QPointF mappedPoint = fitted.worldToMap(m_hoverScenePoint);
    const QPointF clampedPoint(qBound(targetRect.left(), mappedPoint.x(), targetRect.right()),
                               qBound(targetRect.top(), mappedPoint.y(), targetRect.bottom()));
    painter.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 230), 1));
    painter.drawLine(clampedPoint + QPointF(-4, 0), clampedPoint + QPointF(4, 0));
    painter.drawLine(clampedPoint + QPointF(0, -4), clampedPoint + QPointF(0, 4));
}

void MindMapMiniMap::paintOffMapIndicator(QPainter& painter, const QRectF& targetRect,
                                          const QRectF& clippedViewportRect) const {
    if (!clippedViewportRect.isEmpty()) return;
    if (!m_view || !m_view->scene()) return;

    // 视口跑到节点包围盒之外：视口框整块消失会让人以为小地图没反应，
    // 改成在面板边缘朝视口方向画一小段标记条。
    const QPointF visibleCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    const auto fitted = projection(targetRect);
    if (!fitted.isValid()) return;
    const QPointF mappedCenter = fitted.worldToMap(visibleCenter);

    const qreal margin = 6.0;
    QRectF marker;
    if (mappedCenter.x() < targetRect.left()) {
        marker = QRectF(targetRect.left() - margin, targetRect.center().y() - 12, 4, 24);
    } else if (mappedCenter.x() > targetRect.right()) {
        marker = QRectF(targetRect.right() + margin - 4, targetRect.center().y() - 12, 4, 24);
    } else if (mappedCenter.y() < targetRect.top()) {
        marker = QRectF(targetRect.center().x() - 12, targetRect.top() - margin, 24, 4);
    } else if (mappedCenter.y() > targetRect.bottom()) {
        marker = QRectF(targetRect.center().x() - 12, targetRect.bottom() + margin - 4, 24, 4);
    } else {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 160));
    painter.drawRoundedRect(marker.intersected(rect()), 2, 2);
}

void MindMapMiniMap::paintEvent(QPaintEvent* event) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::MiniMapPaint);
    Q_UNUSED(event);
    if (!m_view || !m_view->scene()) return;
    // Keep the previous raster during rapid scene changes. The deferred timer
    // rebuilds it once the event stream is idle; only the first display needs
    // a synchronous build.
    if (m_cacheDirty && m_mapImage.isNull()) rebuildCache();

    const QRectF panelRect = rect().adjusted(0, 0, -1, -1);
    const QRectF targetRect = contentRect();
    if (targetRect.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QPen(QColor(170, 176, 186, 180), 1));
    painter.setBrush(QColor(255, 255, 255, 218));
    painter.drawRoundedRect(panelRect, 8, 8);

    const QRectF worldRect = m_cachedWorldRect;
    const QRectF visibleSceneRect = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    if (worldRect.width() <= 0 || worldRect.height() <= 0) return;

    painter.drawImage(targetRect, m_mapImage);

    QRectF viewportRect = projection(targetRect).worldToMap(visibleSceneRect);
    const QRectF clippedViewportRect = viewportRect.intersected(targetRect.adjusted(-1, -1, 1, 1));
    if (!clippedViewportRect.isEmpty()) {
        // 拖动中把填充加深，给出"正在操控"的状态。
        painter.setBrush(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), m_dragging ? 90 : 42));
        painter.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 230), 2));
        painter.drawRect(clippedViewportRect);
    } else {
        paintOffMapIndicator(painter, targetRect, clippedViewportRect);
    }

    paintHoverFeedback(painter, targetRect);
}
