#pragma once

#include <QPointF>
#include <QRectF>

namespace MindMapHoverGeometry {

inline QRectF candidateSearchRect(const QPointF& scenePosition,
                                 qreal reachToLeftItems,
                                 qreal reachToRightItems,
                                 qreal verticalReach) {
    return QRectF(scenePosition.x() - reachToLeftItems,
                  scenePosition.y() - verticalReach,
                  reachToLeftItems + reachToRightItems,
                  verticalReach * 2.0);
}

} // namespace MindMapHoverGeometry
