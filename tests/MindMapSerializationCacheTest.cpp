#include "../MindMapIO.h"
#include "../MindMapNode.h"
#include "../MindMapBlock.h"
#include "../MindMapImageItem.h"
#include "LeapVersion.h"
#include <QApplication>
#include <QJsonArray>
#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QDebug>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed at" << __LINE__ << #condition; return 1; } } while (false)

// 彩虹分支在真实节点树上的行为：分支序号取「连到根的那一层」的位置，
// 增删分支后兄弟的颜色必须跟着前移，不能停留在旧序号上。
static int rainbowNodeChecks() {
    std::unique_ptr<MindMapNode> root(new MindMapNode("root"));
    auto* a = new MindMapNode("a");
    auto* b = new MindMapNode("b");
    auto* c = new MindMapNode("c");
    root->addChild(a);
    root->addChild(b);
    root->addChild(c);
    auto* a1 = new MindMapNode("a1");
    auto* a2 = new MindMapNode("a2");
    a->addChild(a1);
    a->addChild(a2);
    auto* a1x = new MindMapNode("a1x");
    a1->addChild(a1x);

    CHECK(root->styleBranchIndex() == -1);
    CHECK(a->styleBranchIndex() == 0);
    CHECK(b->styleBranchIndex() == 1);
    CHECK(c->styleBranchIndex() == 2);
    CHECK(a2->styleBranchIndex() == 0);
    CHECK(a1x->styleBranchIndex() == 0);

    // 关闭彩虹时，颜色就是普通层级样式。
    const MindMapLevelStyles off = root->levelStyles();
    CHECK(!off.rainbow.enabled);
    CHECK(a->styleForCurrentLevel().fillColor == off.firstChild.fillColor);
    CHECK(a1x->styleForCurrentLevel().fillColor == off.deeper.fillColor);

    MindMapLevelStyles on = off;
    on.rainbow.enabled = true;
    root->applyLevelStyles(on);
    const QVector<QColor> palette = MindMapRainbowStyle::defaultColors();

    // 根节点不受彩虹影响。
    CHECK(root->styleForCurrentLevel().fillColor == off.root.fillColor);
    // 1 级：饱和分支色 + 白字。
    CHECK(a->styleForCurrentLevel().fillColor == palette.at(0));
    CHECK(b->styleForCurrentLevel().fillColor == palette.at(1));
    CHECK(c->styleForCurrentLevel().fillColor == palette.at(2));
    CHECK(a->styleForCurrentLevel().textColor == QColor(Qt::white));
    // 2 级及更深：同一分支统一浅色，不再逐级变浅。
    CHECK(a1->styleForCurrentLevel().fillColor ==
          MindMapRainbowStyle::mix(palette.at(0), QColor(Qt::white), 0.85));
    CHECK(a1->styleForCurrentLevel().fillColor == a2->styleForCurrentLevel().fillColor);
    CHECK(a1->styleForCurrentLevel().fillColor == a1x->styleForCurrentLevel().fillColor);
    CHECK(a1->styleForCurrentLevel().textColor == a1x->styleForCurrentLevel().textColor);
    // 节点实际绘制的颜色也一起换掉了，而不只是 styleForCurrentLevel() 的返回值。
    CHECK(a->m_fillColor == palette.at(0));
    CHECK(a1->m_fillColor == a1->styleForCurrentLevel().fillColor);
    // 字号沿用层级样式，开启彩虹不改布局。
    CHECK(a->styleForCurrentLevel().fontPointSize == off.firstChild.fontPointSize);
    CHECK(a1x->styleForCurrentLevel().fontPointSize == off.deeper.fontPointSize);

    // 在最前面插入一个带后代的子树：原 a/b/c 序号后移，颜色必须重排。
    auto* z = new MindMapNode("z");
    auto* z1 = new MindMapNode("z1");
    z->addChild(z1);
    root->insertChild(0, z);
    CHECK(z->styleBranchIndex() == 0);
    CHECK(a->styleBranchIndex() == 1);
    CHECK(b->styleBranchIndex() == 2);
    CHECK(c->styleBranchIndex() == 3);
    CHECK(z->m_fillColor == palette.at(0));
    CHECK(a->m_fillColor == palette.at(1));
    CHECK(b->m_fillColor == palette.at(2));
    CHECK(c->m_fillColor == palette.at(3));
    CHECK(z1->m_fillColor == MindMapRainbowStyle::mix(palette.at(0), QColor(Qt::white), 0.85));
    CHECK(a1x->m_fillColor == MindMapRainbowStyle::mix(palette.at(1), QColor(Qt::white), 0.85));

    // 删掉中间的 b：后面的 c 前移一位，颜色跟着变。
    root->removeChild(b);
    CHECK(c->styleBranchIndex() == 2);
    CHECK(c->m_fillColor == palette.at(2));
    CHECK(a->m_fillColor == palette.at(1));
    delete b;

    // 拖拽换父：把 c 挂到 a 下面，它不再是 1 级分支，改穿 a 的色系。
    root->removeChild(c);
    a->addChild(c);
    CHECK(c->styleBranchIndex() == 1);
    CHECK(c->m_fillColor == MindMapRainbowStyle::mix(palette.at(1), QColor(Qt::white), 0.85));

    // 关闭彩虹后立刻恢复普通层级样式，两套设置互不擦除。
    MindMapLevelStyles back = on;
    back.rainbow.enabled = false;
    root->applyLevelStyles(back);
    CHECK(a->m_fillColor == off.firstChild.fillColor);
    CHECK(a1x->m_fillColor == off.deeper.fillColor);
    CHECK(root->levelStyles().rainbow.colors.size() == kRainbowBranchPeriod);
    CHECK(root->levelStyles().rainbow.enabled == false);

    // 重新打开，配色还是原来那六色（此时根下是 z、a 两支，a 排第二）。
    root->applyLevelStyles(on);
    CHECK(a->styleBranchIndex() == 1);
    CHECK(a->m_fillColor == palette.at(1));

    // 分支覆盖：改第 1 个分支（z）的 1 级填充，节点立刻换色；
    // 同分支的 2 级、以及别的分支都不许跟着变。
    MindMapLevelStyles tweaked = on;
    MindMapRainbowBranchStyle myFill;
    myFill.hasFill = true;
    myFill.fillColor = QColor("#123456");
    tweaked.rainbow.setBranchStyle(0, 1, myFill);
    root->applyLevelStyles(tweaked);
    CHECK(z->m_fillColor == QColor("#123456"));
    CHECK(z1->m_fillColor == MindMapRainbowStyle::mix(palette.at(0), QColor(Qt::white), 0.85));
    CHECK(a->m_fillColor == palette.at(1));

    // 覆盖也得跟着存档走，否则重新打开文件微调过的颜色就丢了。
    {
        const QJsonObject tweakedSaved = MindMapIO::serialize(root.get());
        std::unique_ptr<MindMapNode> tweakedReloaded(MindMapIO::deserialize(tweakedSaved));
        CHECK(tweakedReloaded != nullptr);
        CHECK(tweakedReloaded->levelStyles().rainbow.branchStyles.size() == 1);
        CHECK(tweakedReloaded->childNodes().at(0)->m_fillColor == QColor("#123456"));
    }

    // 撤掉覆盖，后面的存档往返断言才有确定的期望值。
    root->applyLevelStyles(on);
    CHECK(z->m_fillColor == palette.at(0));

    // 分支级「跟随填充」：勾上后这一支的边框必须等于它的填充色，其它支不受影响。
    {
        MindMapLevelStyles following = on;
        MindMapRainbowBranchStyle follow;
        follow.hasBorderFollowsFill = true;
        follow.borderFollowsFill = true;
        following.rainbow.setBranchStyle(0, 1, follow);
        root->applyLevelStyles(following);
        CHECK(z->m_fillColor == z->m_borderColor);
        CHECK(a->m_borderColor != a->m_fillColor);
        root->applyLevelStyles(on);
        CHECK(z->m_borderColor != z->m_fillColor);
    }

    // 存档往返：彩虹开关与六色必须跟着文件走，读回来节点就已按分支序号上色。
    QJsonObject saved = MindMapIO::serialize(root.get());
    CHECK(saved["styles"].toObject().contains("rainbow"));
    CHECK(saved["styles"].toObject()["rainbow"].toObject()["enabled"].toBool());
    std::unique_ptr<MindMapNode> reloaded(MindMapIO::deserialize(saved));
    CHECK(reloaded != nullptr);
    CHECK(reloaded->levelStyles().rainbow.enabled);
    CHECK(reloaded->levelStyles().rainbow.colors.size() == kRainbowBranchPeriod);
    CHECK(reloaded->childNodes().size() == 2);
    auto* rz = reloaded->childNodes().at(0);
    auto* ra = reloaded->childNodes().at(1);
    CHECK(rz->styleBranchIndex() == 0);
    CHECK(ra->styleBranchIndex() == 1);
    CHECK(rz->m_fillColor == palette.at(0));
    CHECK(ra->m_fillColor == palette.at(1));
    CHECK(!ra->childNodes().isEmpty());
    CHECK(ra->childNodes().at(0)->m_fillColor ==
          MindMapRainbowStyle::mix(palette.at(1), QColor(Qt::white), 0.85));
    // 读回来的层级样式没被彩虹擦掉。
    CHECK(reloaded->levelStyles().firstChild.fillColor == off.firstChild.fillColor);
    return 0;
}

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
    // 文件头水印里的版本号必须来自 VERSION.txt（经 LeapVersion.h 注入），
    // 不能是任何地方硬编码的字面量。
    {
        auto storedWatermark = MindMapIO::serialize(loaded.get())["_copyright"].toString();
        CHECK(storedWatermark.contains(QStringLiteral(LEAP_VERSION_DISPLAY)));
        CHECK(!storedWatermark.isEmpty());
    }
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
    CHECK(rainbowNodeChecks() == 0);
    qInfo() << "Serialization cache checks passed";
    return 0;
}
