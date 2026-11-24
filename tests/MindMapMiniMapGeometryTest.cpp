#include "../MindMapMiniMapGeometry.h"

#include <QDebug>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            qCritical() << "Failed:" << #condition; \
            return false; \
        } \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance) \
    do { \
        if (qAbs((actual) - (expected)) > (tolerance)) { \
            qCritical() << "Failed:" << #actual << "expected" << (expected) \
                        << "actual" << (actual); \
            return false; \
        } \
    } while (false)

namespace {

constexpr qreal kEps = 1e-6;

// 世界原点为负、宽高比与目标矩形不同的常见情形。
bool checkLetterboxedWorld() {
    const QRectF world(-400, -250, 1600, 500);   // 宽扁
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);
    CHECK(projection.isValid());

    // 16:5 的世界塞进 16:10 的目标 -> 水平贴合，垂直留白。
    // scale = min(160/1600, 100/500) = 0.1，映射高度 50，上下各留 25。
    CHECK_NEAR(projection.scale, target.width() / world.width(), kEps);
    CHECK_NEAR(projection.offset.x(), target.left(), kEps);
    CHECK_NEAR(projection.offset.y(), 35.0, kEps);

    const QRectF fitted = projection.fittedWorldRect();
    CHECK_NEAR(fitted.left(), target.left(), kEps);
    CHECK_NEAR(fitted.width(), target.width(), kEps);
    CHECK_NEAR(fitted.top(), 35.0, kEps);
    CHECK_NEAR(fitted.height(), 50.0, kEps);
    return true;
}

// 高瘦的世界在水平方向留白。
bool checkLetterboxedWorldHorizontally() {
    const QRectF world(0, 0, 500, 1600);
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);
    CHECK(projection.isValid());

    // scale = min(160/500, 100/1600) = 0.0625，映射宽度 31.25，左右各留 64.375。
    CHECK_NEAR(projection.scale, target.height() / world.height(), kEps);
    CHECK_NEAR(projection.offset.y(), target.top(), kEps);
    CHECK_NEAR(projection.offset.x(), 74.375, kEps);
    return true;
}

// 正变换 -> 反变换必须回到原点，包括负坐标和偏移后的世界。
bool checkRoundTrip() {
    const QRectF world(-1234.5, -987.25, 4321.0, 1234.5);
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);
    CHECK(projection.isValid());

    const QPointF samples[] = {
        world.topLeft(), world.bottomRight(), world.center(),
        QPointF(world.left() + 17.0, world.top() + 3.0),
    };
    for (const QPointF& sample : samples) {
        const QPointF back = projection.mapToWorld(projection.worldToMap(sample));
        CHECK_NEAR(back.x(), sample.x(), 1e-6);
        CHECK_NEAR(back.y(), sample.y(), 1e-6);
    }
    return true;
}

// 矩形重载与逐点重载必须给出同一个位置，否则栅格图像会和视口框错位。
bool checkRectMatchesPoints() {
    const QRectF world(0, 0, 800, 600);
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);

    const QRectF sceneRect(-100, 50, 300, 200);
    const QRectF mappedRect = projection.worldToMap(sceneRect);
    const QPointF topLeft = projection.worldToMap(sceneRect.topLeft());
    CHECK_NEAR(mappedRect.left(), topLeft.x(), kEps);
    CHECK_NEAR(mappedRect.top(), topLeft.y(), kEps);
    CHECK_NEAR(mappedRect.width(), sceneRect.width() * projection.scale, kEps);
    CHECK_NEAR(mappedRect.height(), sceneRect.height() * projection.scale, kEps);
    return true;
}

// 信箱带内、控件外都要夹回边界，不能返回信箱带的坐标。
bool checkClamping() {
    const QRectF world(0, 0, 1600, 500);
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);
    CHECK(projection.isValid());

    // 目标矩形上方的信箱带：y = 10 在实心区域（35..85）之外。
    const QPointF above = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(50, 10), projection);
    CHECK_NEAR(above.y(), world.top(), 1e-6);
    CHECK(above.x() >= world.left() && above.x() <= world.right());

    const QPointF below = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(50, 200), projection);
    CHECK_NEAR(below.y(), world.bottom(), 1e-6);

    // 控件外（左侧、右侧）夹到水平边界。
    const QPointF left = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(-500, 80), projection);
    CHECK_NEAR(left.x(), world.left(), 1e-6);
    const QPointF right = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(9999, 80), projection);
    CHECK_NEAR(right.x(), world.right(), 1e-6);

    // 内部点不受影响。
    const QPointF inside = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(90, 85), projection);
    const QPointF direct = projection.mapToWorld(QPointF(90, 85));
    CHECK_NEAR(inside.x(), direct.x(), 1e-6);
    CHECK_NEAR(inside.y(), direct.y(), 1e-6);
    return true;
}

// 退化输入不能崩、不能除零。
bool checkDegenerateInput() {
    const QRectF target(10, 10, 160, 100);

    CHECK(!MindMapMiniMapGeometry::fit(QRectF(), target).isValid());
    CHECK(!MindMapMiniMapGeometry::fit(QRectF(0, 0, 0, 100), target).isValid());
    CHECK(!MindMapMiniMapGeometry::fit(QRectF(0, 0, 100, 0), target).isValid());
    CHECK(!MindMapMiniMapGeometry::fit(QRectF(0, 0, 800, 600), QRectF()).isValid());

    const auto invalid = MindMapMiniMapGeometry::fit(QRectF(), target);
    const QPointF clamped = MindMapMiniMapGeometry::clampedMapToWorld(QPointF(50, 50), invalid);
    CHECK(clamped == QPointF());
    return true;
}

// 世界恰好等于目标宽高比时不该有任何留白。
bool checkExactAspect() {
    const QRectF world(0, 0, 1600, 1000);
    const QRectF target(10, 10, 160, 100);
    const auto projection = MindMapMiniMapGeometry::fit(world, target);
    CHECK(projection.isValid());
    CHECK_NEAR(projection.offset.x(), target.left(), kEps);
    CHECK_NEAR(projection.offset.y(), target.top(), kEps);
    CHECK_NEAR(projection.scale, 0.1, kEps);
    return true;
}

} // namespace

int main() {
    if (!checkLetterboxedWorld()) return 1;
    if (!checkLetterboxedWorldHorizontally()) return 1;
    if (!checkRoundTrip()) return 1;
    if (!checkRectMatchesPoints()) return 1;
    if (!checkClamping()) return 1;
    if (!checkDegenerateInput()) return 1;
    if (!checkExactAspect()) return 1;
    return 0;
}
