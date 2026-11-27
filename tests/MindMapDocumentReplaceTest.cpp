#include "../MindMapCommands.h"
#include "../MindMapIO.h"
#include "../MindMapNode.h"

#include <QApplication>
#include <QDebug>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonObject>
#include <QUndoStack>
#include <cmath>

// ReplaceDocumentCommand 的往返测试。
//
// 这条命令是插件批量改写文档（LeapDocumentWriter::replaceDocument）的实现底座，
// 最容易出问题的地方是「撤销之后旧文档能不能原样回来」——它保存的是 JSON 快照
// 而不是节点对象，所以每次 undo/redo 都要重新反序列化一次。这里把这条往返路径
// 连同节点数量、文本内容、以及「反复撤销重做不漂移」一起钉住。

#define CHECK(condition) do { \
    if (!(condition)) { \
        qCritical() << "Failed at" << __LINE__ << #condition; \
        return 1; \
    } \
} while (false)

namespace {

MindMapNode* findRoot(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    for (auto* item : scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) return node;
        }
    }
    return nullptr;
}

int countNodes(MindMapNode* node) {
    if (!node) return 0;
    int total = 1;
    for (auto* child : node->childNodes()) total += countNodes(child);
    return total;
}

QJsonObject makeTextNode(const QString& text, const QJsonArray& children) {
    QJsonObject block;
    block["type"] = "text";
    block["text"] = text;

    QJsonArray blocks;
    blocks.append(block);

    QJsonObject node;
    node["blocks"] = blocks;
    node["text"] = text;
    node["expanded"] = true;
    node["children"] = children;
    return node;
}

// 造一份「新文档」：root -> a, b；a -> a1
QJsonObject makeReplacementDoc() {
    QJsonArray aChildren;
    aChildren.append(makeTextNode("a1", QJsonArray()));

    QJsonArray rootChildren;
    rootChildren.append(makeTextNode("a", aChildren));
    rootChildren.append(makeTextNode("b", QJsonArray()));

    return makeTextNode("root", rootChildren);
}

// 场景里的初始文档：old -> x
//
// 注意不要用 unique_ptr 持有：addItem() 之后场景接管了节点的所有权，
// 再用智能指针管一份就是双重释放（这条测试第一次写就是这么崩的）。
MindMapNode* makeInitialTree(QGraphicsScene* scene) {
    auto* root = new MindMapNode("old");
    scene->addItem(root);
    auto* x = new MindMapNode("x");
    scene->addItem(x);
    root->addChild(x);
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
    return root;
}

int commandRoundTrip() {
    QGraphicsScene scene;
    scene.setSceneRect(-2000, -2000, 4000, 4000);
    MindMapNode* initial = makeInitialTree(&scene);

    CHECK(countNodes(findRoot(&scene)) == 2);
    CHECK(findRoot(&scene)->allText().contains("old"));

    const QJsonObject oldDoc = MindMapIO::serialize(initial);
    const QJsonObject newDoc = makeReplacementDoc();

    QUndoStack stack;
    stack.push(new ReplaceDocumentCommand(oldDoc, newDoc, &scene, "测试替换"));

    // push() 立刻触发一次 redo()：场景里应该是新文档。
    MindMapNode* afterReplace = findRoot(&scene);
    CHECK(afterReplace != nullptr);
    CHECK(countNodes(afterReplace) == 4);
    CHECK(afterReplace->allText().contains("root"));
    CHECK(!afterReplace->allText().contains("old"));

    // 撤销：旧文档要原样回来。
    stack.undo();
    MindMapNode* afterUndo = findRoot(&scene);
    CHECK(afterUndo != nullptr);
    CHECK(countNodes(afterUndo) == 2);
    CHECK(afterUndo->allText().contains("old"));
    CHECK(!afterUndo->allText().contains("root"));

    // 重做：又是新文档。
    stack.redo();
    MindMapNode* afterRedo = findRoot(&scene);
    CHECK(afterRedo != nullptr);
    CHECK(countNodes(afterRedo) == 4);

    // 反复往返不能漂移（每次都重新反序列化，最容易在这里泄漏或丢节点）。
    for (int i = 0; i < 5; ++i) {
        stack.undo();
        CHECK(countNodes(findRoot(&scene)) == 2);
        stack.redo();
        CHECK(countNodes(findRoot(&scene)) == 4);
    }

    // 场景里任何时刻都只能有一棵树。
    int rootCount = 0;
    for (auto* item : scene.items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) ++rootCount;
        }
    }
    CHECK(rootCount == 1);

    return 0;
}

// 空文档 / 坏文档不能让命令把场景清空后什么都不装。
int emptyDocumentIsIgnored() {
    QGraphicsScene scene;
    scene.setSceneRect(-2000, -2000, 4000, 4000);
    makeInitialTree(&scene);

    QUndoStack stack;
    stack.push(new ReplaceDocumentCommand(QJsonObject(), QJsonObject(), &scene, "空替换"));

    MindMapNode* root = findRoot(&scene);
    CHECK(root != nullptr);
    CHECK(countNodes(root) == 2);
    CHECK(root->allText().contains("old"));

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    if (commandRoundTrip() != 0) return 1;
    if (emptyDocumentIsIgnored() != 0) return 1;

    qInfo() << "MindMapDocumentReplaceTest passed";
    return 0;
}
