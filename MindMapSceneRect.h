#pragma once

#include <QRectF>

namespace MindMapSceneRect {

inline QRectF expandedToContain(const QRectF& currentRect,
                               const QRectF& contentRect,
                               qreal safeMargin = 200.0,
                               qreal expansionDistance = 600.0) {
    if (!currentRect.isValid() || !contentRect.isValid() ||
        safeMargin < 0.0 || expansionDistance <= 0.0) {
        return currentRect;
    }

    QRectF requiredRect = contentRect.adjusted(-safeMargin, -safeMargin,
                                               safeMargin, safeMargin);
    qreal left = currentRect.left();
    qreal top = currentRect.top();
    qreal right = currentRect.right();
    qreal bottom = currentRect.bottom();

    if (requiredRect.left() < left) {
        left = qMin(requiredRect.left(), left - expansionDistance);
    }
    if (requiredRect.top() < top) {
        top = qMin(requiredRect.top(), top - expansionDistance);
    }
    if (requiredRect.right() > right) {
        right = qMax(requiredRect.right(), right + expansionDistance);
    }
    if (requiredRect.bottom() > bottom) {
        bottom = qMax(requiredRect.bottom(), bottom + expansionDistance);
    }

    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace MindMapSceneRect
