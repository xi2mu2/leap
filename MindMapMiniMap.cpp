#include "MindMapMiniMap.h"
#include "MindMapNode.h"
#include "PerformanceMetrics.h"

#include <QGraphicsOpacityEffect>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

MindMapMiniMap::MindMapMiniMap(QGraphicsView* view)
    : QWidget(view), m_view(view) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setFixedSize(180, 120);
    hide();

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(520);
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect && m_opacityEffect->opacity() <= 0.01) {
            hide();
        }
    });

    m_hideDelayTimer = new QTimer(this);
    m_hideDelayTimer->setSingleShot(true);
    m_hideDelayTimer->setInterval(850);
    connect(m_hideDelayTimer, &QTimer::timeout, this, [this]() {
        if (!m_fadeAnimation) return;
        m_fadeAnimation->stop();
        m_fadeAnimation->setStartValue(m_opacityEffect ? m_opacityEffect->opacity() : 1.0);
        m_fadeAnimation->setEndValue(0.0);
        m_fadeAnimation->start();
    });

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(80);
    connect(m_rebuildTimer, &QTimer::timeout, this, [this]() {
        if (!m_cacheDirty) return;
        rebuildCache();
        update();
    });
}

void MindMapMiniMap::showForMovement() {
    if (!m_view || !m_view->scene()) return;

    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
    }
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(1.0);
    }

    show();
    raise();
    update();

    if (m_hideDelayTimer) {
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
        const qreal scale = qMin(m_mapImage.width() / m_cachedWorldRect.width(),
                                 m_mapImage.height() / m_cachedWorldRect.height());
        const QSizeF mappedSize(m_cachedWorldRect.width() * scale,
                                m_cachedWorldRect.height() * scale);
        const QPointF offset((m_mapImage.width() - mappedSize.width()) / 2.0,
                             (m_mapImage.height() - mappedSize.height()) / 2.0);
        auto mapRect = [&](const QRectF& sceneRect) {
            QRectF mapped;
            mapped.setLeft(offset.x() + (sceneRect.left() - m_cachedWorldRect.left()) * scale);
            mapped.setTop(offset.y() + (sceneRect.top() - m_cachedWorldRect.top()) * scale);
            mapped.setWidth(qMax(2.0, sceneRect.width() * scale));
            mapped.setHeight(qMax(2.0, sceneRect.height() * scale));
            return mapped;
        };
        mapPainter.setPen(QPen(QColor(119, 134, 155, 115), 1));
        for (const NodeSnapshot& node : m_nodes) {
            if (!node.hasParent) continue;
            mapPainter.drawLine(mapRect(QRectF(node.parentCenter, QSizeF())).center(),
                                mapRect(QRectF(node.center, QSizeF())).center());
        }
        mapPainter.setPen(Qt::NoPen);
        for (const NodeSnapshot& node : m_nodes) {
            mapPainter.setBrush(node.selected ? QColor(26, 115, 232, 220)
                                              : QColor(74, 144, 226, 170));
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

QRectF MindMapMiniMap::mapSceneRectToMiniMap(const QRectF& sceneRect, const QRectF& targetRect) const {
    if (!m_view || !m_view->scene() || sceneRect.isEmpty() || targetRect.isEmpty()) return QRectF();

    const QRectF worldRect = m_cachedWorldRect;
    if (worldRect.isEmpty()) return QRectF();
    // 背景地图和视口框必须使用同一个固定世界范围，否则视口移动到
    // 缓存范围外时，缩放比例会变化，导致缓存图像与视口框错位。
    if (worldRect.width() <= 0 || worldRect.height() <= 0) return QRectF();

    const qreal scale = qMin(targetRect.width() / worldRect.width(),
                             targetRect.height() / worldRect.height());
    const QSizeF mappedWorldSize(worldRect.width() * scale, worldRect.height() * scale);
    const QPointF offset(targetRect.left() + (targetRect.width() - mappedWorldSize.width()) / 2.0,
                         targetRect.top() + (targetRect.height() - mappedWorldSize.height()) / 2.0);

    QRectF mapped;
    mapped.setLeft(offset.x() + (sceneRect.left() - worldRect.left()) * scale);
    mapped.setTop(offset.y() + (sceneRect.top() - worldRect.top()) * scale);
    mapped.setWidth(sceneRect.width() * scale);
    mapped.setHeight(sceneRect.height() * scale);
    return mapped;
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

    QRectF worldRect = m_cachedWorldRect;
    const QRectF visibleSceneRect = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    if (worldRect.width() <= 0 || worldRect.height() <= 0) return;

    const qreal scale = qMin(targetRect.width() / worldRect.width(),
                             targetRect.height() / worldRect.height());
    const QSizeF mappedWorldSize(worldRect.width() * scale, worldRect.height() * scale);
    const QPointF offset(targetRect.left() + (targetRect.width() - mappedWorldSize.width()) / 2.0,
                         targetRect.top() + (targetRect.height() - mappedWorldSize.height()) / 2.0);

    painter.drawImage(targetRect, m_mapImage);

    QRectF viewportRect = mapSceneRectToMiniMap(visibleSceneRect, targetRect);
    viewportRect = viewportRect.intersected(targetRect.adjusted(-1, -1, 1, 1));
    painter.setBrush(QColor(26, 115, 232, 42));
    painter.setPen(QPen(QColor(26, 115, 232, 230), 2));
    painter.drawRect(viewportRect);
}
