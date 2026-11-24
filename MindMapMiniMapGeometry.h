#pragma once

// 小地图的"世界坐标 <-> 控件坐标"投影，收敛成一处纯函数。
//
// 之前这段公式在 MindMapMiniMap.cpp 里抄了三份（rebuildCache 内联、
// mapSceneRectToMiniMap、paintEvent），其中 paintEvent 那份还是算完没人用的
// 死代码。三份公式只要有一份漂移，"栅格图像画在哪"和"点下去传送到哪"就对不上。
// 因此正变换、反变换都从这里出，控件里不再出现第二份 scale/offset 计算。

#include <QPointF>
#include <QRectF>
#include <QtGlobal>

namespace MindMapMiniMapGeometry {

struct Projection {
    qreal scale = 0.0;      // 世界单位 -> 小地图像素
    QPointF offset;         // 世界左上角在小地图目标矩形里的像素位置
    QRectF worldRect;       // 参与投影的世界范围（固定，只随结构变化）
    QRectF targetRect;      // 小地图上可绘制区域（内容区，非整个控件）

    bool isValid() const { return scale > 0.0; }

    QPointF worldToMap(const QPointF& worldPoint) const {
        return QPointF(offset.x() + (worldPoint.x() - worldRect.left()) * scale,
                       offset.y() + (worldPoint.y() - worldRect.top()) * scale);
    }

    QRectF worldToMap(const QRectF& sceneRect) const {
        if (!isValid()) return QRectF();
        QRectF mapped;
        mapped.setLeft(offset.x() + (sceneRect.left() - worldRect.left()) * scale);
        mapped.setTop(offset.y() + (sceneRect.top() - worldRect.top()) * scale);
        mapped.setWidth(sceneRect.width() * scale);
        mapped.setHeight(sceneRect.height() * scale);
        return mapped;
    }

    QPointF mapToWorld(const QPointF& mapPoint) const {
        if (!isValid()) return QPointF();
        return QPointF(worldRect.left() + (mapPoint.x() - offset.x()) / scale,
                       worldRect.top() + (mapPoint.y() - offset.y()) / scale);
    }

    // 世界范围真正覆盖到的像素区域。等比缩放时目标矩形的宽高比与世界不一致，
    // 上下或左右会留下"信箱带"（letterbox），这块空白底下没有对应的世界坐标。
    QRectF fittedWorldRect() const { return worldToMap(worldRect); }
};

inline Projection fit(const QRectF& worldRect, const QRectF& targetRect) {
    Projection projection;
    projection.worldRect = worldRect;
    projection.targetRect = targetRect;
    if (worldRect.width() <= 0.0 || worldRect.height() <= 0.0) return projection;
    if (targetRect.width() <= 0.0 || targetRect.height() <= 0.0) return projection;

    const qreal scale = qMin(targetRect.width() / worldRect.width(),
                             targetRect.height() / worldRect.height());
    if (!(scale > 0.0)) return projection;

    projection.scale = scale;
    projection.offset = QPointF(
        targetRect.left() + (targetRect.width() - worldRect.width() * scale) / 2.0,
        targetRect.top() + (targetRect.height() - worldRect.height() * scale) / 2.0);
    return projection;
}

// 把手里的控件坐标夹进"有内容的区域"再反算世界坐标。
// 指针落进信箱带或划出控件时仍然返回边界上的合法点，既不产生非法值，
// 也不会让拖动跟踪中断或跳变。
inline QPointF clampedMapToWorld(const QPointF& mapPoint, const Projection& projection) {
    if (!projection.isValid()) return QPointF();
    const QRectF fitted = projection.fittedWorldRect();
    const qreal x = qBound(fitted.left(), mapPoint.x(), fitted.right());
    const qreal y = qBound(fitted.top(), mapPoint.y(), fitted.bottom());
    return projection.mapToWorld(QPointF(x, y));
}

} // namespace MindMapMiniMapGeometry
