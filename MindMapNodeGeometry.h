#pragma once

#include <QRectF>

namespace MindMapNodeGeometry {

inline QRectF boundingRect(const QRectF& nodeRect, bool hasChildren,
                           qreal highlightMargin, qreal highlightPenWidth,
                           qreal indicatorHorizontalOffset, qreal indicatorHitRadius) {
    const qreal paintExtent = highlightMargin + highlightPenWidth / 2.0 + 1.0;
    QRectF bounds = nodeRect.adjusted(-paintExtent, -paintExtent,
                                      paintExtent, paintExtent);

    if (hasChildren) {
        const qreal centerY = nodeRect.center().y();
        const qreal indicatorRight = nodeRect.width() + indicatorHorizontalOffset
                                     + indicatorHitRadius;
        bounds.setRight(qMax(bounds.right(), indicatorRight));
        bounds.setTop(qMin(bounds.top(), centerY - indicatorHitRadius));
        bounds.setBottom(qMax(bounds.bottom(), centerY + indicatorHitRadius));
    }
    return bounds;
}

} // namespace MindMapNodeGeometry
