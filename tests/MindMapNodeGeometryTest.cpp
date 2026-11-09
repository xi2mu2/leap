#include "../MindMapNodeGeometry.h"

#include <QDebug>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

int main() {
    const QRectF nodeRect(0, 0, 100, 50);
    const QRectF bounds = MindMapNodeGeometry::boundingRect(
        nodeRect, false, 6.0, 3.0, 20.0, 15.0);

    const QRectF hoverFrame = nodeRect.adjusted(-7.0, -7.0, 7.0, 7.0);
    const QRectF selectedFrame = nodeRect.adjusted(-7.5, -7.5, 7.5, 7.5);
    CHECK(bounds.contains(hoverFrame));
    CHECK(bounds.contains(selectedFrame));

    const QRectF boundsWithChildren = MindMapNodeGeometry::boundingRect(
        nodeRect, true, 6.0, 3.0, 20.0, 15.0);
    const QPointF indicatorCenter(nodeRect.width() + 20.0, nodeRect.center().y());
    const QRectF indicatorHitArea(indicatorCenter.x() - 15.0,
                                  indicatorCenter.y() - 15.0,
                                  30.0, 30.0);
    CHECK(boundsWithChildren.contains(selectedFrame));
    CHECK(boundsWithChildren.contains(indicatorHitArea));
    return 0;
}
