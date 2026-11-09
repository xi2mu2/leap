#include "../MindMapSceneRect.h"

#include <QDebug>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            qCritical() << "Failed:" << #condition; \
            return 1; \
        } \
    } while (false)

int main() {
    const QRectF current(0, 0, 1000, 1000);

    const QRectF inside = MindMapSceneRect::expandedToContain(
        current, QRectF(250, 250, 500, 500));
    CHECK(inside == current);

    const QRectF below = MindMapSceneRect::expandedToContain(
        current, QRectF(450, 850, 100, 200));
    CHECK(qFuzzyCompare(below.top(), current.top()));
    CHECK(below.bottom() >= 1250.0);

    const QRectF leftAndTop = MindMapSceneRect::expandedToContain(
        current, QRectF(-700, -300, 100, 100));
    CHECK(leftAndTop.left() <= -800.0);
    CHECK(leftAndTop.top() <= -500.0);
    CHECK(leftAndTop.right() == current.right());
    CHECK(leftAndTop.bottom() == current.bottom());

    const QRectF farRight = MindMapSceneRect::expandedToContain(
        current, QRectF(5000, 400, 100, 100));
    CHECK(farRight.right() >= 5300.0);
    CHECK(farRight.left() == current.left());

    return 0;
}
