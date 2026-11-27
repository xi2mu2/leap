#pragma once

#include "MindMapMiniMapGeometry.h"

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
    // 指针落在小地图上的行为：
    //   Always     始终显示，随时可点击/拖拽传送；
    //   OnMovement 仅在视野移动后短暂浮现（旧行为）；
    //   Off        完全不显示，也不接收交互。
    enum class DisplayMode { Always, OnMovement, Off };

    explicit MindMapMiniMap(QGraphicsView* view);

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

    // 尺寸百分比，100 = 基准 180x120。宿主会夹到 [kMinSizePercent,
    // kMaxSizePercent]。绘制全部基于 contentRect()，因此改尺寸不需要别的适配。
    static constexpr int kMinSizePercent = 50;
    static constexpr int kMaxSizePercent = 200;
    static constexpr int kBaseWidth = 180;
    static constexpr int kBaseHeight = 120;

    void setSizePercent(int percent);
    int sizePercent() const { return m_sizePercent; }

    void showForMovement();
    void invalidateCache();

signals:
    // 请求把视口中心移到 sceneCenter。animated 为真时走平滑过渡，
    // 为假时直接改滚动条（拖拽跟踪必须跟手，不能缓动）。
    void panRequested(const QPointF& sceneCenter, bool animated);
    // 用户开始操作小地图：让视图先停掉正在跑的视口动画，
    // 否则缓动值会覆盖手动设置的滚动条位置，表现为拖动一顿一顿。
    void interactionStarted();
    void interactionFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRectF contentRect() const;
    MindMapMiniMapGeometry::Projection projection(const QRectF& targetRect) const;
    void rebuildCache();

    void setOpacityInstant(qreal value);
    QPointF scenePointAt(const QPointF& widgetPos) const;
    void updateHoverState(const QPointF& widgetPos);
    void clearHoverState();
    void paintHoverFeedback(QPainter& painter, const QRectF& targetRect) const;
    void paintOffMapIndicator(QPainter& painter, const QRectF& targetRect,
                              const QRectF& clippedViewportRect) const;

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

    DisplayMode m_displayMode = DisplayMode::OnMovement;
    int m_sizePercent = 100;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_inside = false;          // 指针是否在小地图上
    QPoint m_pressPos;
    QPointF m_pressScenePoint;
    QPointF m_hoverScenePoint;      // 指针当前对应的场景坐标
    bool m_hoverValid = false;
};
