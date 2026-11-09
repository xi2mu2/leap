#include "../MindMapIO.h"
#include "../MindMapNode.h"
#include "../MindMapBlock.h"
#include "../MindMapImageItem.h"
#include <QApplication>
#include <QJsonArray>
#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QDebug>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed at" << __LINE__ << #condition; return 1; } } while (false)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    auto* child = new MindMapNode("child");
    root->addChild(child);
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::red);
    auto* ib = new ImageBlock(image, child);
    child->addBlock(ib);
    ib->imageItem()->setImageBlock(ib);
    auto first = MindMapIO::serialize(root.get());
    CHECK(root->hasSerializationCache() && child->hasSerializationCache());
    auto repeated = MindMapIO::serialize(root.get());
    for (const char* key : {"_timestamp", "_signature"}) {
        first.remove(QLatin1String(key)); repeated.remove(QLatin1String(key));
    }
    CHECK(first == repeated);
    auto* tb = static_cast<TextBlock*>(child->blocks().first());
    tb->setPlainText("edited");
    CHECK(!child->hasSerializationCache() && root->hasSerializationCache());
    auto edited = MindMapIO::serialize(root.get());
    CHECK(edited["children"].toArray()[0].toObject()["text"] == "edited");
    QTextCursor cursor(tb->textItem()->document());
    cursor.select(QTextCursor::Document);
    QTextCharFormat bold; bold.setFontWeight(QFont::Bold);
    cursor.mergeCharFormat(bold);
    CHECK(!child->hasSerializationCache());
    tb->textItem()->document()->undo();
    CHECK(!child->hasSerializationCache());
    MindMapIO::serialize(root.get());
    ib->imageItem()->setImageName("renamed");
    CHECK(!child->hasSerializationCache());
    auto renamed = MindMapIO::serialize(root.get());
    CHECK(renamed["children"].toArray()[0].toObject()["blocks"].toArray()[1].toObject()["name"] == "renamed");
    ib->imageItem()->setImageSize(QSizeF(64, 64));
    CHECK(!child->hasSerializationCache());
    auto serialized = MindMapIO::serialize(root.get());
    // Preserve a raw field to prove opening and first saving uses the source JSON.
    auto sourceBlocks = serialized["blocks"].toArray();
    auto sourceText = sourceBlocks[0].toObject();
    sourceText["cacheProbe"] = "original";
    sourceBlocks[0] = sourceText;
    serialized["blocks"] = sourceBlocks;
    std::unique_ptr<MindMapNode> loaded(MindMapIO::deserialize(serialized));
    CHECK(loaded->hasSerializationCache());
    CHECK(loaded->childNodes()[0]->hasSerializationCache());
    loaded->layoutTree(0, 0); loaded->adjustLayout(); loaded->updateAllEdges();
    CHECK(loaded->childNodes()[0]->hasSerializationCache());
    auto saved = MindMapIO::serialize(loaded.get());
    CHECK(saved["blocks"].toArray()[0].toObject()["cacheProbe"] == "original");
    CHECK(saved["children"] == serialized["children"]);
    auto* lc = loaded->childNodes()[0];
    lc->setExpanded(false);
    CHECK(!MindMapIO::serialize(lc)["expanded"].toBool());
    lc->setTextBlockWidth(300);
    CHECK(!lc->hasSerializationCache());
    MindMapIO::serialize(loaded.get());
    lc->moveBlock(0, 1);
    CHECK(!lc->hasSerializationCache());
    auto* extra = new MindMapNode("extra");
    loaded->insertChild(0, extra);
    CHECK(MindMapIO::serialize(loaded.get())["children"].toArray()[1].toObject()["id"] == "1.2");
    QTemporaryDir dir;
    CHECK(MindMapIO::saveToFile(loaded.get(), dir.filePath("test.leap")));
    std::unique_ptr<MindMapNode> roundtrip(MindMapIO::loadFromFile(dir.filePath("test.leap")));
    CHECK(roundtrip && roundtrip->childNodes().size() == 2);
    auto styles = static_cast<const MindMapNode*>(roundtrip.get())->levelStyles();
    styles.root.textColor = Qt::green;
    roundtrip->applyLevelStyles(styles);
    CHECK(!roundtrip->hasSerializationCache());
    CHECK(MindMapIO::serialize(roundtrip.get())["styles"] == styles.toJson());
    auto* formula = new FormulaBlock(QPixmap::fromImage(image), "x", roundtrip.get());
    roundtrip->addBlock(formula);
    MindMapIO::serialize(roundtrip.get());
    formula->setLatexCode("y");
    CHECK(!roundtrip->hasSerializationCache());
    CHECK(MindMapIO::serialize(roundtrip.get())["blocks"].toArray().last().toObject()["latex"] == "y");
    auto legacy = serialized; legacy["version"] = 0; legacy.remove("blocks");
    std::unique_ptr<MindMapNode> old(MindMapIO::deserialize(legacy));
    CHECK(!old->hasSerializationCache());
    CHECK(!MindMapIO::serialize(old.get())["blocks"].toArray().isEmpty());

    QJsonObject cancellable;
    cancellable["version"] = MindMapIO::CURRENT_VERSION;
    QJsonArray manyChildren;
    for (int i = 0; i < 64; ++i) {
        QJsonObject childNode;
        childNode["version"] = MindMapIO::CURRENT_VERSION;
        childNode["blocks"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::number(i)}}};
        manyChildren.append(childNode);
    }
    cancellable["children"] = manyChildren;
    QGraphicsScene scene;
    scene.setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    CHECK(scene.itemIndexMethod() == QGraphicsScene::BspTreeIndex);
    int reports = 0;
    auto cancelledLoad = MindMapIO::deserialize(cancellable, &scene,
        [&](const MindMapIO::LoadProgress&) { return ++reports < 4; });
    CHECK(cancelledLoad == nullptr);
    CHECK(scene.items().isEmpty());
    qInfo() << "Serialization cache checks passed";
    return 0;
}
