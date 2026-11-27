#include "../MindMapNode.h"
#include "../MindMapBlock.h"
#include <QApplication>
#include <QGraphicsScene>
#include <QDebug>
#include <QPointF>
#include <QVector>
#include <cmath>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed at" << __LINE__ << #condition; return 1; } } while (false)

static bool samePoint(const QPointF& a, const QPointF& b) {
    return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01;
}

// 收集整棵树的坐标，用于比较两次重排结果是否一致。
static void collectPositions(MindMapNode* node, QVector<QPointF>& out) {
    out.append(node->pos());
    for (auto* child : node->childNodes()) {
        collectPositions(child, out);
    }
}

static QVector<QPointF> collectPositions(MindMapNode* root) {
    QVector<QPointF> out;
    collectPositions(root, out);
    return out;
}

static void canonicalLayout(MindMapNode* root) {
    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
}

// 布局入口必须是幂等的：同样的树连续重排多次，坐标不能漂移。
// 历史上 layoutTree() 的 y 参数被当作「节点 y」传入 root->pos().y()，
// 而它实际是「子树顶部」，于是每次重排根节点都向下累积半个子树高度；
// 等到别处用 layoutTree(0, 0) 重建布局时，整张图就会突然弹回。
static int layoutIsIdempotent() {
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    auto* a = new MindMapNode("a");
    auto* b = new MindMapNode("b");
    auto* c = new MindMapNode("c");
    root->addChild(a);
    root->addChild(b);
    root->addChild(c);
    a->addChild(new MindMapNode("a1"));
    a->addChild(new MindMapNode("a2"));
    b->addChild(new MindMapNode("b1"));

    canonicalLayout(root.get());
    const QVector<QPointF> first = collectPositions(root.get());

    canonicalLayout(root.get());
    const QVector<QPointF> second = collectPositions(root.get());
    CHECK(first.size() == second.size());
    for (int i = 0; i < first.size(); ++i) {
        CHECK(samePoint(first[i], second[i]));
    }

    // 再多跑几轮，确认没有「每轮移动一点点」的累积误差。
    for (int round = 0; round < 5; ++round) {
        canonicalLayout(root.get());
        const QVector<QPointF> again = collectPositions(root.get());
        for (int i = 0; i < first.size(); ++i) {
            CHECK(samePoint(first[i], again[i]));
        }
    }
    return 0;
}

// 规范原点与树之前的位置无关：先把根节点随便挪开，再 layoutTree(0, 0)
// 必须回到同一组坐标。设置面板刷新、命令撤销/重做、收尾编辑都依赖这条，
// 否则不同入口会得到两套绝对坐标，画面就在两者之间来回跳。
static int canonicalOriginIgnoresPriorPosition() {
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    auto* child = new MindMapNode("child");
    root->addChild(child);
    child->addChild(new MindMapNode("grandchild"));

    canonicalLayout(root.get());
    const QVector<QPointF> baseline = collectPositions(root.get());

    const qreal dx = 137.0;
    const qreal dy = -412.0;
    root->setPos(root->pos() + QPointF(dx, dy));
    child->setPos(child->pos() + QPointF(dx / 2.0, dy / 2.0));

    canonicalLayout(root.get());
    const QVector<QPointF> restored = collectPositions(root.get());
    CHECK(baseline.size() == restored.size());
    for (int i = 0; i < baseline.size(); ++i) {
        CHECK(samePoint(baseline[i], restored[i]));
    }
    return 0;
}

// adjustLayout() 只能向下推开重叠，不能把整棵树往上抬；重复调用同样必须幂等。
static int adjustLayoutIsIdempotent() {
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    auto* a = new MindMapNode("a");
    auto* b = new MindMapNode("b");
    root->addChild(a);
    root->addChild(b);

    canonicalLayout(root.get());
    const QVector<QPointF> once = collectPositions(root.get());

    root->adjustLayout();
    root->adjustLayout();
    const QVector<QPointF> thrice = collectPositions(root.get());
    CHECK(once.size() == thrice.size());
    for (int i = 0; i < once.size(); ++i) {
        CHECK(samePoint(once[i], thrice[i]));
    }
    return 0;
}

// 把 layoutTree() 的 y 语义钉住：y 是「子树顶部」，不是节点自身的 y。
// 这正是当年设置面板传入 root->pos().y() 就导致每次刷新都向下累积的原因；
// 如果以后有人改了语义，这条断言会先失败，提醒同步所有调用点。
static int layoutTreeYIsSubtreeTop() {
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    root->addChild(new MindMapNode("a"));
    root->addChild(new MindMapNode("b"));
    root->addChild(new MindMapNode("c"));

    canonicalLayout(root.get());
    const qreal canonicalY = root->pos().y();

    // 有子节点时，子树高度大于节点自身高度，节点在垂直方向居中，
    // 所以「规范原点的 y」会被推到 0 以下（居中在 0..height 之间）。
    CHECK(root->calcRequiredHeight() > root->rect().height());
    // 节点在子树内垂直居中：以 0 为子树顶部时，节点 y 会被推正。
    CHECK(canonicalY > 0.0);

    // 用规范 y 再重排一次不应移动：说明传 0 才是固定点。
    root->layoutTree(0, 0);
    CHECK(samePoint(QPointF(root->pos().x(), root->pos().y()), QPointF(0.0, canonicalY)));

    // 反过来，把节点自身 pos().y() 当作子树顶部传进去，根节点会被再推下去
    // 半个子树高度——这就是历史 bug 的形态（这里固化它的方向，避免调用点回退）。
    const qreal before = root->pos().y();
    root->layoutTree(0, before);
    CHECK(root->pos().y() > before);
    return 0;
}

// 布局本身不受 scene 影响：挂在场景里（有 view 的路径会走
// relayoutKeepingViewport）也不能改变节点坐标的规范性。
static int layoutWorksInsideScene() {
    QGraphicsScene scene;
    auto* root = new MindMapNode("root");
    scene.addItem(root);
    root->addChild(new MindMapNode("c1"));
    root->addChild(new MindMapNode("c2"));

    canonicalLayout(root);
    const QVector<QPointF> baseline = collectPositions(root);
    const QPointF firstRoot = root->pos();

    canonicalLayout(root);
    const QVector<QPointF> again = collectPositions(root);
    CHECK(baseline.size() == again.size());
    for (int i = 0; i < baseline.size(); ++i) {
        CHECK(samePoint(baseline[i], again[i]));
    }
    CHECK(samePoint(root->pos(), firstRoot));
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    if (layoutIsIdempotent() != 0) return 1;
    if (canonicalOriginIgnoresPriorPosition() != 0) return 1;
    if (adjustLayoutIsIdempotent() != 0) return 1;
    if (layoutTreeYIsSubtreeTop() != 0) return 1;
    if (layoutWorksInsideScene() != 0) return 1;

    qInfo() << "MindMapLayoutStabilityTest passed";
    return 0;
}
