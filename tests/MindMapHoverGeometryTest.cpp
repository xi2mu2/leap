#include "../MindMapHoverGeometry.h"
#include "../MindMapNode.h"

#include <QDebug>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

int main() {
    const QRectF nodeRect(0, 0, 100, 50);
    const qreal leftItemReach = MindMapNode::H_GAP / 2 + 1.0;
    const qreal rightItemReach = MindMapNode::INDICATOR_HIT_RADIUS + 1.0;
    const qreal verticalReach = MindMapNode::INDICATOR_HIT_RADIUS + 1.0;

    const QPointF hoverPositions[] = {
        QPointF(50, 25),
        QPointF(-MindMapNode::INDICATOR_HIT_RADIUS, 25),
        QPointF(100 + MindMapNode::H_GAP / 2, 25),
        QPointF(50, -MindMapNode::INDICATOR_HIT_RADIUS),
        QPointF(50, 50 + MindMapNode::INDICATOR_HIT_RADIUS),
        QPointF(-MindMapNode::INDICATOR_HIT_RADIUS,
                -MindMapNode::INDICATOR_HIT_RADIUS),
        QPointF(100 + MindMapNode::H_GAP / 2,
                50 + MindMapNode::INDICATOR_HIT_RADIUS)
    };

    for (const QPointF& position : hoverPositions) {
        const QRectF searchRect = MindMapHoverGeometry::candidateSearchRect(
            position, leftItemReach, rightItemReach, verticalReach);
        CHECK(searchRect.intersects(nodeRect));
    }

    const QPointF unrelatedPosition(300, 300);
    const QRectF unrelatedSearch = MindMapHoverGeometry::candidateSearchRect(
        unrelatedPosition, leftItemReach, rightItemReach, verticalReach);
    CHECK(!unrelatedSearch.intersects(nodeRect));
    return 0;
}
