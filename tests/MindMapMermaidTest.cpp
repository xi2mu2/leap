#include "../MindMapMermaid.h"
#include "../MindMapIO.h"
#include "../MindMapNode.h"
#include "../MindMapBlock.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QDebug>
#include <QString>

#define CHECK(condition) do { \
    if (!(condition)) { \
        qCritical() << "Failed at" << __LINE__ << #condition; \
        return 1; \
    } \
} while (false)

namespace {

QChar bt() { return QChar(0x0060); }

struct BuiltTree {
    QGraphicsScene scene;
    MindMapNode* root = nullptr;
};

QString plainText(MindMapNode* node) {
    QString text;
    for (MindMapBlock* block : node->blocks()) {
        if (block->type() == BlockType::Text) {
            if (!text.isEmpty()) text += QLatin1Char('|');
            text += static_cast<TextBlock*>(block)->toPlainText();
        } else if (block->type() == BlockType::Formula) {
            if (!text.isEmpty()) text += QLatin1Char('|');
            text += QStringLiteral("$$") + static_cast<FormulaBlock*>(block)->latexCode() +
                    QStringLiteral("$$");
        }
    }
    return text;
}

int countNodes(MindMapNode* node) {
    int total = 1;
    for (MindMapNode* child : node->childNodes()) total += countNodes(child);
    return total;
}

void collectTexts(MindMapNode* node, QStringList* out) {
    out->append(plainText(node));
    for (MindMapNode* child : node->childNodes()) collectTexts(child, out);
}

MindMapNode* buildFromJson(const QJsonObject& json, QGraphicsScene* scene) {
    return MindMapIO::deserialize(json, scene);
}

int parseStructureChecks() {
    const QString source = QStringLiteral(
        "mindmap\n"
        "  root[项目计划]\n"
        "    plan[里程碑]\n"
        "      m1((第一阶段))\n"
        "      m2{{第二阶段}}\n"
        "    risk[风险]\n"
        "      r1\n");

    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);
    CHECK(result.errorMessage.isEmpty());
    CHECK(result.rootJson.contains(QStringLiteral("blocks")));
    CHECK(!result.rootJson.contains(QStringLiteral("version")));

    BuiltTree tree;
    tree.root = buildFromJson(result.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(countNodes(tree.root) == 6);
    CHECK(plainText(tree.root) == QStringLiteral("项目计划"));
    CHECK(tree.root->childNodes().size() == 2);

    QStringList texts;
    collectTexts(tree.root, &texts);
    CHECK(texts.size() == 6);
    CHECK(texts.at(1) == QStringLiteral("里程碑"));
    CHECK(texts.at(2) == QStringLiteral("第一阶段"));
    CHECK(texts.at(3) == QStringLiteral("第二阶段"));
    CHECK(texts.at(4) == QStringLiteral("风险"));
    CHECK(texts.at(5) == QStringLiteral("r1"));

    CHECK(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("形状")));
    return 0;
}

int irregularIndentChecks() {
    const QString source = QStringLiteral(
        "mindmap\n"
        "  a\n"
        "      b\n"
        "    c\n"
        "  d\n");

    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);

    BuiltTree tree;
    tree.root = buildFromJson(result.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(plainText(tree.root) == QStringLiteral("a"));
    // 缩进回退时按 Mermaid 语义走到最近一个缩进更浅的祖先：c(4) 比 b(6) 浅，
    // 因此回到 a(2) 之下，而不是继续挂在 b 下。
    CHECK(tree.root->childNodes().size() == 3);
    CHECK(plainText(tree.root->childNodes().at(0)) == QStringLiteral("b"));
    CHECK(tree.root->childNodes().at(0)->childNodes().isEmpty());
    CHECK(plainText(tree.root->childNodes().at(1)) == QStringLiteral("c"));
    CHECK(plainText(tree.root->childNodes().at(2)) == QStringLiteral("d"));
    return 0;
}

int multiRootChecks() {
    const QString source = QStringLiteral(
        "mindmap\n"
        "  root\n"
        "    child\n"
        "  orphan\n");

    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);
    CHECK(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("根节点")));

    BuiltTree tree;
    tree.root = buildFromJson(result.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(countNodes(tree.root) == 3);
    CHECK(plainText(tree.root) == QStringLiteral("root"));
    return 0;
}

int markdownAndEntityChecks() {
    QString source = QStringLiteral("mindmap\n  root[\"多行<br/>标签\"]\n");
    source += QStringLiteral("    md[") + bt() + QStringLiteral("**粗体** 与 *斜体*") + bt() +
              QStringLiteral("]\n");
    source += QStringLiteral("    amp[Tom &amp; Jerry]\n");

    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);

    BuiltTree tree;
    tree.root = buildFromJson(result.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(plainText(tree.root) == QStringLiteral("多行\n标签"));

    CHECK(tree.root->childNodes().size() == 2);
    MindMapNode* mdNode = tree.root->childNodes().at(0);
    // QTextDocument 会把 <b>/<i> 归一成 style 属性，所以按渲染后的形式检查
    const QString mdHtml = mdNode->nodeHtml();
    CHECK(mdHtml.contains(QStringLiteral("font-weight")));
    CHECK(mdHtml.contains(QStringLiteral("font-style")));
    CHECK(plainText(mdNode) == QStringLiteral("粗体 与 斜体"));

    CHECK(plainText(tree.root->childNodes().at(1)) == QStringLiteral("Tom & Jerry"));
    return 0;
}

int formulaRoundTripChecks() {
    BuiltTree source;
    auto* root = new MindMapNode(QStringLiteral("公式"), nullptr);
    source.scene.addItem(root);
    root->addBlock(new FormulaBlock(QPixmap(), QStringLiteral("E = mc^2"), root));
    auto* linear = new MindMapNode(QStringLiteral("二阶"), nullptr);
    root->addChild(linear);
    linear->addBlock(new FormulaBlock(QPixmap(), QStringLiteral("a^2 + b^2 = c^2"), linear));
    source.root = root;

    QStringList exportWarnings;
    const QString mermaid = MindMapMermaidExporter::toText(source.root, &exportWarnings);
    CHECK(mermaid.startsWith(QStringLiteral("mindmap\n")));
    CHECK(mermaid.contains(QStringLiteral("$$E = mc^2$$")));
    CHECK(exportWarnings.join(QLatin1Char('\n')).contains(QStringLiteral("公式")));

    const MermaidParseResult parsed = MindMapMermaidImporter::parse(mermaid);
    CHECK(parsed.ok);
    CHECK(!parsed.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("配对")));

    BuiltTree tree;
    tree.root = buildFromJson(parsed.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(countNodes(tree.root) == 2);
    // 节点自身的文字会跟着 $$ 公式一起导出成同一个标签，所以回来是「文本块 + 公式块」
    CHECK(tree.root->blocks().size() == 2);
    CHECK(tree.root->blocks().at(0)->type() == BlockType::Text);
    CHECK(tree.root->blocks().at(1)->type() == BlockType::Formula);
    CHECK(static_cast<FormulaBlock*>(tree.root->blocks().at(1))->latexCode() ==
          QStringLiteral("E = mc^2"));
    CHECK(tree.root->childNodes().size() == 1);
    CHECK(tree.root->childNodes().first()->blocks().size() == 2);
    CHECK(static_cast<FormulaBlock*>(tree.root->childNodes().first()->blocks().at(1))->latexCode() ==
          QStringLiteral("a^2 + b^2 = c^2"));
    return 0;
}

int escapingRoundTripChecks() {
    BuiltTree source;
    auto* root = new MindMapNode(QStringLiteral("根 (v2)"), nullptr);
    source.scene.addItem(root);
    auto* tricky = new MindMapNode(QStringLiteral("含\"引号\"与 # 号\n第二行"), nullptr);
    root->addChild(tricky);
    auto* plain = new MindMapNode(QStringLiteral("普通节点"), nullptr);
    root->addChild(plain);
    source.root = root;

    const QString mermaid = MindMapMermaidExporter::toText(source.root, nullptr);
    const MermaidParseResult parsed = MindMapMermaidImporter::parse(mermaid);
    CHECK(parsed.ok);

    BuiltTree tree;
    tree.root = buildFromJson(parsed.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(countNodes(tree.root) == 3);
    CHECK(plainText(tree.root) == QStringLiteral("根 (v2)"));
    CHECK(plainText(tree.root->childNodes().at(0)) == QStringLiteral("含\"引号\"与 # 号\n第二行"));
    CHECK(plainText(tree.root->childNodes().at(1)) == QStringLiteral("普通节点"));
    return 0;
}

int headerHandlingChecks() {
    const QString source = QStringLiteral(
        "---\n"
        "config:\n"
        "  theme: base\n"
        "---\n"
        "%% 这是注释\n"
        "mindmap\n"
        "  root\n"
        "    child\n");
    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);
    CHECK(result.warnings.isEmpty());
    CHECK(result.rootJson.value(QStringLiteral("children")).toArray().size() == 1);

    const MermaidParseResult wrong = MindMapMermaidImporter::parse(
        QStringLiteral("sequenceDiagram\n  A->>B: hi\n"));
    CHECK(!wrong.ok);
    CHECK(wrong.errorMessage.contains(QStringLiteral("mindmap")));

    const MermaidParseResult loose = MindMapMermaidImporter::parse(
        QStringLiteral("  root\n    child\n"));
    CHECK(loose.ok);
    CHECK(!loose.warnings.isEmpty());

    const MermaidParseResult empty = MindMapMermaidImporter::parse(QString());
    CHECK(!empty.ok);
    CHECK(!empty.errorMessage.isEmpty());
    return 0;
}

int emptyNodeLabelChecks() {
    // Mermaid 的词法要求 [ ] 之间至少有一个 NODE_DESCR，空标签会让整个
    // 文件解析失败（Expecting 'NODE_DESCR', got 'NODE_DEND'）。这里覆盖
    // 三种会产生空标签的节点：空文本、只有空白的文本、只有图片块。
    BuiltTree source;
    auto* root = new MindMapNode(QStringLiteral("根"), nullptr);
    source.scene.addItem(root);

    auto* emptyText = new MindMapNode(QString(), nullptr);
    root->addChild(emptyText);

    auto* blankText = new MindMapNode(QStringLiteral("   "), nullptr);
    root->addChild(blankText);

    auto* imageOnly = new MindMapNode(QString(), nullptr);
    imageOnly->clearBlocks();
    QImage pixels(8, 8, QImage::Format_ARGB32);
    pixels.fill(Qt::red);
    imageOnly->addBlock(new ImageBlock(pixels, imageOnly));
    root->addChild(imageOnly);

    auto* normal = new MindMapNode(QStringLiteral("正常节点"), nullptr);
    root->addChild(normal);
    source.root = root;

    QStringList exportWarnings;
    const QString mermaid = MindMapMermaidExporter::toText(source.root, &exportWarnings);

    // 关键断言：不再出现 Mermaid 无法解析的空标签
    CHECK(!mermaid.contains(QStringLiteral("[\"\"]")));
    CHECK(!mermaid.contains(QStringLiteral("[]")));
    CHECK(mermaid.contains(QStringLiteral("[#8203;]")));

    const QString warningText = exportWarnings.join(QLatin1Char('\n'));
    CHECK(warningText.contains(QStringLiteral("空节点")));

    const MermaidParseResult parsed = MindMapMermaidImporter::parse(mermaid);
    CHECK(parsed.ok);

    BuiltTree tree;
    tree.root = buildFromJson(parsed.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);

    // 结构完整：4 个孩子，数量与层级都不变
    CHECK(countNodes(tree.root) == 5);
    CHECK(tree.root->childNodes().size() == 4);
    CHECK(plainText(tree.root) == QStringLiteral("根"));
    CHECK(plainText(tree.root->childNodes().at(0)).isEmpty());
    CHECK(plainText(tree.root->childNodes().at(1)).isEmpty());
    CHECK(plainText(tree.root->childNodes().at(2)).isEmpty());
    CHECK(plainText(tree.root->childNodes().at(3)) == QStringLiteral("正常节点"));

    // 零宽空格不能残留到节点文本里
    for (MindMapNode* child : tree.root->childNodes()) {
        CHECK(!plainText(child).contains(QChar(0x200B)));
    }
    return 0;
}

int entityRoundTripChecks() {
    // escapeQuotedLabel() 把 # 转成 #35;、把 " 转成 #34;，两者必须能还原。
    // 注意这套转义不是单射的：用户文本若本身就写成 "#35;" 这样的实体形式，
    // 回读时会被当成实体解码（Mermaid 自身也有同样的歧义），这里不覆盖。
    BuiltTree source;
    auto* root = new MindMapNode(QStringLiteral("根"), nullptr);
    source.scene.addItem(root);
    auto* hashNode = new MindMapNode(QStringLiteral("#1 标题与 100% 进度"), nullptr);
    root->addChild(hashNode);
    auto* quoteNode = new MindMapNode(QStringLiteral("含\"双引号\"与 <尖括号>"), nullptr);
    root->addChild(quoteNode);
    source.root = root;

    const QString mermaid = MindMapMermaidExporter::toText(source.root, nullptr);
    const MermaidParseResult parsed = MindMapMermaidImporter::parse(mermaid);
    CHECK(parsed.ok);

    BuiltTree tree;
    tree.root = buildFromJson(parsed.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(countNodes(tree.root) == 3);
    CHECK(plainText(tree.root->childNodes().at(0)) == QStringLiteral("#1 标题与 100% 进度"));
    CHECK(plainText(tree.root->childNodes().at(1)) ==
          QStringLiteral("含\"双引号\"与 <尖括号>"));
    return 0;
}

int filePathChecks() {
    CHECK(MindMapMermaidImporter::isMermaidFilePath(QStringLiteral("a.mmd")));
    CHECK(MindMapMermaidImporter::isMermaidFilePath(QStringLiteral("B.MMD")));
    CHECK(MindMapMermaidImporter::isMermaidFilePath(QStringLiteral("c/d.mermaid")));
    CHECK(!MindMapMermaidImporter::isMermaidFilePath(QStringLiteral("e.leap")));
    CHECK(!MindMapMermaidImporter::isMermaidFilePath(QStringLiteral("f.md")));
    CHECK(MindMapMermaidImporter::fileFilter().contains(QStringLiteral("*.mmd")));
    return 0;
}

int realWorldSampleChecks() {
    // 取自 mermaid 官方文档风格的样例：图标、类名、多种形状、<> 标签
    const QString source = QStringLiteral(
        "mindmap\n"
        "  root((思维导图))\n"
        "    Origins\n"
        "      Long history\n"
        "      ::icon(fa fa-book)\n"
        "      Popularisation\n"
        "        British popular psychology author Tony Buzan\n"
        "    Research\n"
        "      On effectiveness<br/>and features\n"
        "      On Automatic creation\n"
        "        Uses\n"
        "            Creative techniques\n"
        "            Strategic thinking\n"
        "    Tools\n"
        "      Pen and paper\n"
        "      Mermaid:::urgent\n");

    const MermaidParseResult result = MindMapMermaidImporter::parse(source);
    CHECK(result.ok);

    const QString warningText = result.warnings.join(QLatin1Char('\n'));
    CHECK(warningText.contains(QStringLiteral("图标")));
    CHECK(warningText.contains(QStringLiteral("类名")));
    CHECK(warningText.contains(QStringLiteral("形状")));

    BuiltTree tree;
    tree.root = buildFromJson(result.rootJson, &tree.scene);
    CHECK(tree.root != nullptr);
    CHECK(plainText(tree.root) == QStringLiteral("思维导图"));
    CHECK(tree.root->childNodes().size() == 3);
    CHECK(plainText(tree.root->childNodes().at(0)) == QStringLiteral("Origins"));
    CHECK(tree.root->childNodes().at(0)->childNodes().size() == 2);
    CHECK(plainText(tree.root->childNodes().at(0)->childNodes().at(1)) ==
          QStringLiteral("Popularisation"));
    CHECK(plainText(tree.root->childNodes().at(1)->childNodes().at(0)) ==
          QStringLiteral("On effectiveness\nand features"));
    CHECK(plainText(tree.root->childNodes().at(2)->childNodes().at(1)) ==
          QStringLiteral("Mermaid"));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (int rc = parseStructureChecks()) return rc;
    if (int rc = irregularIndentChecks()) return rc;
    if (int rc = multiRootChecks()) return rc;
    if (int rc = markdownAndEntityChecks()) return rc;
    if (int rc = formulaRoundTripChecks()) return rc;
    if (int rc = escapingRoundTripChecks()) return rc;
    if (int rc = headerHandlingChecks()) return rc;
    if (int rc = realWorldSampleChecks()) return rc;
    if (int rc = emptyNodeLabelChecks()) return rc;
    if (int rc = entityRoundTripChecks()) return rc;
    if (int rc = filePathChecks()) return rc;

    qInfo() << "All Mermaid checks passed.";
    return 0;
}
