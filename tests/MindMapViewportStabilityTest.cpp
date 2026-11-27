#include "../MindMapNode.h"
#include "../MindMapView.h"
#include <QApplication>
#include <QGraphicsScene>
#include <QScrollBar>
#include <QDebug>
#include <QPointF>
#include <cmath>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed at" << __LINE__ << #condition; return 1; } } while (false)

static bool samePoint(const QPointF& a, const QPointF& b, qreal tolerance = 1.5) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

// 造一棵有层级的树，便于产生「重排后整棵树明显位移」的效果。
static MindMapNode* buildTree() {
    auto* root = new MindMapNode("root");
    auto* a = new MindMapNode("a");
    auto* b = new MindMapNode("b");
    auto* c = new MindMapNode("c");
    root->addChild(a);
    root->addChild(b);
    root->addChild(c);
    a->addChild(new MindMapNode("a1"));
    a->addChild(new MindMapNode("a2"));
    a->addChild(new MindMapNode("a3"));
    b->addChild(new MindMapNode("b1"));
    c->addChild(new MindMapNode("c1"));
    return root;
}

// 这个节点的中心在屏幕上的位置，就是我们要钉住的锚点。
static QPointF anchorOf(MindMapView* view, MindMapNode* node) {
    return view->mapFromScene(node->mapToScene(node->rect().center()));
}

// 复现真实故障链：设置面板反复用「节点 y 当子树顶部」重排，把整张图累积
// 推下去；随后退出编辑/关闭设置走规范原点重排，画面本该整体跳回。
// relayoutKeepingViewport() 必须在重排后把内容挪回屏幕原位。
static int relayoutKeepsViewportAnchor() {
    QGraphicsScene scene;
    scene.setSceneRect(-4000, -4000, 8000, 8000);
    std::unique_ptr<MindMapNode> root(buildTree());
    scene.addItem(root.get());
    MindMapNode::setUndoStack(nullptr);

    // 规范布局，作为「设置被改过之前」的基线。
    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
    const QPointF canonicalRootPos = root->pos();

    MindMapView view(&scene);
    view.resize(900, 700);
    view.show();
    QApplication::processEvents();

    // 累积漂移：模拟旧代码 MainWindow::onSettings() 里
    // layoutTree(root->pos().x(), root->pos().y()) 的行为。
    for (int i = 0; i < 3; ++i) {
        const QPointF p = root->pos();
        root->clearEdges();
        root->layoutTree(p.x(), p.y());
        root->adjustLayout();
        root->updateAllEdges();
    }
    CHECK(root->pos().y() > canonicalRootPos.y());
    QApplication::processEvents();

    // 让漂移后的根节点位于视野中间，模拟用户正在看这张图。
    view.centerOn(root->mapToScene(root->rect().center()));
    QApplication::processEvents();
    const QPointF anchorBefore = anchorOf(&view, root.get());

    // 退出编辑/关闭设置的路径：回到规范原点重排。
    view.relayoutKeepingViewport(root.get());
    QApplication::processEvents();
    const QPointF anchorAfter = anchorOf(&view, root.get());

    // 坐标确实归位了……
    CHECK(samePoint(root->pos(), canonicalRootPos, 0.01));
    // ……但屏幕上的位置不能变。
    CHECK(samePoint(anchorBefore, anchorAfter));
    return 0;
}

// 即使没有漂移（重排是幂等的），锚点也必须保持——这条防止补偿逻辑本身
// 在「不需要补偿」的情况下把视野推走。
static int relayoutWithoutDriftKeepsAnchor() {
    QGraphicsScene scene;
    scene.setSceneRect(-4000, -4000, 8000, 8000);
    std::unique_ptr<MindMapNode> root(buildTree());
    scene.addItem(root.get());
    MindMapNode::setUndoStack(nullptr);

    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();

    MindMapView view(&scene);
    view.resize(900, 700);
    view.show();
    QApplication::processEvents();
    view.centerOn(root->mapToScene(root->rect().center()));
    QApplication::processEvents();

    const QPointF anchorBefore = anchorOf(&view, root.get());
    const int horizontal = view.horizontalScrollBar()->value();
    const int vertical = view.verticalScrollBar()->value();

    view.relayoutKeepingViewport(root.get());
    QApplication::processEvents();

    CHECK(samePoint(anchorBefore, anchorOf(&view, root.get())));
    CHECK(view.horizontalScrollBar()->value() == horizontal);
    CHECK(view.verticalScrollBar()->value() == vertical);
    return 0;
}

// 重排导致场景矩形变大（expandedToContain 只扩不缩）时，滚动条范围会变，
// 补偿必须仍然把内容钉在原位，而不是被新的边界吃掉。
static int relayoutWithSceneRectGrowthKeepsAnchor() {
    QGraphicsScene scene;
    // 场景矩形比内容还小：重排时的 ensureSceneRectContains() 必然把它撑大
    // （expandedToContain 只扩不缩），滚动条范围也跟着变。
    scene.setSceneRect(-20, -20, 40, 40);
    std::unique_ptr<MindMapNode> root(buildTree());
    scene.addItem(root.get());
    MindMapNode::setUndoStack(nullptr);

    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();

    MindMapView view(&scene);
    view.resize(900, 700);
    view.show();
    QApplication::processEvents();

    const QRectF sceneRectBefore = scene.sceneRect();
    const QPointF anchorBefore = anchorOf(&view, root.get());

    view.relayoutKeepingViewport(root.get());
    QApplication::processEvents();

    // 画布确实被撑大了（这会改变滚动条范围，是补偿最容易失效的地方），
    // 而内容仍然停在屏幕原位。
    CHECK(scene.sceneRect() != sceneRectBefore);
    CHECK(samePoint(anchorBefore, anchorOf(&view, root.get())));
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    if (relayoutKeepsViewportAnchor() != 0) return 1;
    if (relayoutWithoutDriftKeepsAnchor() != 0) return 1;
    if (relayoutWithSceneRectGrowthKeepsAnchor() != 0) return 1;

    qInfo() << "MindMapViewportStabilityTest passed";
    return 0;
}
