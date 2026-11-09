#pragma once

#include <QPointer>
#include <QRectF>
#include <QImage>
#include <QVector>
#include <QWidget>

class QGraphicsOpacityEffect;
class QGraphicsView;
class QPropertyAnimation;
class QTimer;

class MindMapMiniMap : public QWidget {
    Q_OBJECT

public:
    explicit MindMapMiniMap(QGraphicsView* view);

    void showForMovement();
    void invalidateCache();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF contentRect() const;
    QRectF mapSceneRectToMiniMap(const QRectF& sceneRect, const QRectF& targetRect) const;
    void rebuildCache();

    struct NodeSnapshot {
        QRectF rect;
        QPointF parentCenter;
        QPointF center;
        bool hasParent = false;
        bool selected = false;
    };

    QPointer<QGraphicsView> m_view;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
    QTimer* m_hideDelayTimer = nullptr;
    QTimer* m_rebuildTimer = nullptr;
    QVector<NodeSnapshot> m_nodes;
    QRectF m_cachedWorldRect;
    QImage m_mapImage;
    bool m_cacheDirty = true;
};
