#include "../MindMapMarkdown.h"
#include "../MindMapIO.h"
#include "../MindMapNode.h"
#include "../MindMapBlock.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QDebug>

#define CHECK(condition) do { \
    if (!(condition)) { \
        qCritical() << "Failed at" << __LINE__ << #condition; \
        return 1; \
    } \
} while (false)

namespace {

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
        } else {
            if (!text.isEmpty()) text += QLatin1Char('|');
            text += QStringLiteral("[img]");
        }
    }
    return text;
}

int countNodes(MindMapNode* node) {
    int total = 1;
    for (MindMapNode* child : node->childNodes()) total += countNodes(child);
    return total;
}

MindMapNode* buildFromJson(const QJsonObject& json, QGraphicsScene* scene) {
    return MindMapIO::deserialize(json, scene);
}

MindMapNode* makeNode(MindMapNode* parent, const QString& text) {
    auto* node = new MindMapNode(text, parent);
    if (!parent) return node;
    return node;
}

QPixmap formulaPixmap() {
    QPixmap pixmap(8, 8);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

// ------------------------------------------------------------
// 导出：层级、缩进、首行标记转义
// ------------------------------------------------------------
int exportStructureChecks() {
    BuiltTree tree;
    auto* root = makeNode(nullptr, QStringLiteral("根"));
    tree.scene.addItem(root);
    auto* a = makeNode(root, QStringLiteral("分支 A"));
    auto* b = makeNode(root, QStringLiteral("分支 B"));
    makeNode(a, QStringLiteral("A-1"));
    makeNode(a, QStringLiteral("A-2"));
    tree.root = root;

    const MarkdownExportResult result =
        MindMapMarkdownExporter::toMarkdown(tree.root, true);
    const QStringList lines = result.text.split(QLatin1Char('\n'));

    CHECK(lines.first() == QStringLiteral("- 根"));
    CHECK(result.text.contains(QStringLiteral("\n    - 分支 A\n")));
    CHECK(result.text.contains(QStringLiteral("\n    - 分支 B\n")));
    CHECK(result.text.contains(QStringLiteral("\n        - A-1\n")));
    CHECK(result.text.contains(QStringLiteral("\n        - A-2\n")));
    // 导出成功且没有降级内容时不应有警告
    CHECK(result.warnings.isEmpty());
    CHECK(result.imageCount == 0);
    // 树里没有图片，即使允许导出图片也不该产出资源
    CHECK(result.images.isEmpty());

    // 以列表符号开头的正文必须转义，否则读者那边会被当成结构
    BuiltTree escaped;
    auto* escapedRoot = makeNode(nullptr, QStringLiteral("- 以减号开头"));
    escaped.scene.addItem(escapedRoot);
    escaped.root = escapedRoot;
    const MarkdownExportResult escapedResult =
        MindMapMarkdownExporter::toMarkdown(escaped.root, true);
    CHECK(escapedResult.text.startsWith(QStringLiteral("- \\- 以减号开头")));

    // 空节点用占位文字导出并给出说明
    BuiltTree emptyTree;
    auto* emptyRoot = makeNode(nullptr, QStringLiteral("根"));
    emptyTree.scene.addItem(emptyRoot);
    emptyRoot->clearBlocks();
    emptyTree.root = emptyRoot;
    const MarkdownExportResult emptyResult =
        MindMapMarkdownExporter::toMarkdown(emptyTree.root, true);
    CHECK(emptyResult.text.contains(QStringLiteral("（空节点）")));
    CHECK(emptyResult.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("空节点")));
    return 0;
}

// ------------------------------------------------------------
// 导出 -> 导入 往返：结构与文字必须一致
// ------------------------------------------------------------
int roundTripChecks() {
    BuiltTree tree;
    auto* root = makeNode(nullptr, QStringLiteral("根节点"));
    tree.scene.addItem(root);
    auto* first = makeNode(root, QStringLiteral("第一分支"));
    auto* second = makeNode(root, QStringLiteral("第二分支"));
    makeNode(first, QStringLiteral("子项 1"));
    makeNode(first, QStringLiteral("子项 2"));
    makeNode(second, QStringLiteral("子项 3"));
    tree.root = root;

    const MarkdownExportResult exported =
        MindMapMarkdownExporter::toMarkdown(tree.root, true);
    const MarkdownParseResult parsed =
        MindMapMarkdownImporter::parse(exported.text, QString(), true);
    CHECK(parsed.ok);
    CHECK(parsed.errorMessage.isEmpty());

    BuiltTree loaded;
    loaded.root = buildFromJson(parsed.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    CHECK(countNodes(loaded.root) == countNodes(tree.root));
    CHECK(plainText(loaded.root) == QStringLiteral("根节点"));
    CHECK(loaded.root->childNodes().size() == 2);
    CHECK(plainText(loaded.root->childNodes().at(0)) == QStringLiteral("第一分支"));
    CHECK(plainText(loaded.root->childNodes().at(1)) == QStringLiteral("第二分支"));
    CHECK(loaded.root->childNodes().at(0)->childNodes().size() == 2);
    CHECK(plainText(loaded.root->childNodes().at(0)->childNodes().at(1)) ==
          QStringLiteral("子项 2"));
    return 0;
}

// ------------------------------------------------------------
// 公式：首行是 $$ 时导出成空列表项，导入回到 FormulaBlock
// ------------------------------------------------------------
int formulaChecks() {
    BuiltTree tree;
    auto* root = makeNode(nullptr, QStringLiteral("根"));
    tree.scene.addItem(root);
    auto* formulaOnly = makeNode(root, QStringLiteral("公式节点"));
    formulaOnly->clearBlocks();
    formulaOnly->addBlock(new FormulaBlock(formulaPixmap(), QStringLiteral("a^2 + b^2"), formulaOnly));
    tree.root = root;

    const MarkdownExportResult exported =
        MindMapMarkdownExporter::toMarkdown(tree.root, true);
    // 关键：不能出现 "- $$"，否则 Obsidian 会把它当普通文字
    CHECK(!exported.text.contains(QStringLiteral("- $$")));
    // 首行是空列表项 "-"，$$ 从续行开始（缩进 4 空格 + 续行 2 空格 = 6 空格）
    CHECK(exported.text.contains(QStringLiteral("\n    -\n      $$")));

    const MarkdownParseResult parsed =
        MindMapMarkdownImporter::parse(exported.text, QString(), true);
    CHECK(parsed.ok);
    BuiltTree loaded;
    loaded.root = buildFromJson(parsed.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    CHECK(loaded.root->childNodes().size() == 1);
    MindMapNode* loadedFormula = loaded.root->childNodes().at(0);
    CHECK(loadedFormula->blocks().size() == 1);
    CHECK(loadedFormula->blocks().at(0)->type() == BlockType::Formula);
    CHECK(static_cast<FormulaBlock*>(loadedFormula->blocks().at(0))->latexCode() ==
          QStringLiteral("a^2 + b^2"));
    return 0;
}

// ------------------------------------------------------------
// 任务列表与标题：单向降级 + 提示
// ------------------------------------------------------------
int taskListChecks() {
    const QString source = QStringLiteral(
        "# 这是标题\n"
        "\n"
        "- 待办清单\n"
        "    - [x] 已经做完\n"
        "    - [ ] 还没做\n"
        "    - [X] 大写也算完成\n");
    const MarkdownParseResult parsed =
        MindMapMarkdownImporter::parse(source, QString(), true);
    CHECK(parsed.ok);

    BuiltTree loaded;
    loaded.root = buildFromJson(parsed.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    CHECK(plainText(loaded.root) == QStringLiteral("待办清单"));
    CHECK(loaded.root->childNodes().size() == 3);
    CHECK(plainText(loaded.root->childNodes().at(0)) == QStringLiteral("☑ 已经做完"));
    CHECK(plainText(loaded.root->childNodes().at(1)) == QStringLiteral("☐ 还没做"));
    CHECK(plainText(loaded.root->childNodes().at(2)) == QStringLiteral("☑ 大写也算完成"));

    const QString warningText = parsed.warnings.join(QLatin1Char('\n'));
    CHECK(warningText.contains(QStringLiteral("任务列表")));
    CHECK(warningText.contains(QStringLiteral("标题")));

    // 导出端绝不能把 ☑ 反解成 - [x]
    const MarkdownExportResult exported =
        MindMapMarkdownExporter::toMarkdown(loaded.root, true);
    CHECK(!exported.text.contains(QStringLiteral("[x]")));
    CHECK(exported.text.contains(QStringLiteral("☑")));
    return 0;
}

// ------------------------------------------------------------
// 行内格式：粗体 / 斜体往返
// ------------------------------------------------------------
int inlineFormatChecks() {
    BuiltTree tree;
    auto* root = makeNode(nullptr, QStringLiteral("格式"));
    tree.scene.addItem(root);
    TextBlock* block = static_cast<TextBlock*>(root->blocks().first());
    block->setHtml(QStringLiteral("<b>粗体</b> 和 <i>斜体</i>"));
    tree.root = root;

    const MarkdownExportResult exported =
        MindMapMarkdownExporter::toMarkdown(tree.root, true);
    CHECK(exported.text.contains(QStringLiteral("**粗体**")));
    CHECK(exported.text.contains(QStringLiteral("*斜体*")));

    const MarkdownParseResult parsed =
        MindMapMarkdownImporter::parse(exported.text, QString(), true);
    CHECK(parsed.ok);
    BuiltTree loaded;
    loaded.root = buildFromJson(parsed.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    const QString html = loaded.root->nodeHtml();
    CHECK(html.contains(QStringLiteral("font-weight")));
    CHECK(html.contains(QStringLiteral("font-style")));
    CHECK(plainText(loaded.root) == QStringLiteral("粗体 和 斜体"));
    return 0;
}

// ------------------------------------------------------------
// 图片：导出成 image_00N.png，导入按 .md 所在目录找回
// ------------------------------------------------------------
int imageChecks() {
    QTemporaryDir dir;
    CHECK(dir.isValid());

    BuiltTree tree;
    auto* root = makeNode(nullptr, QStringLiteral("带图片"));
    tree.scene.addItem(root);
    auto* withImage = makeNode(root, QStringLiteral("图片节点"));
    withImage->clearBlocks();
    QImage pixels(16, 8, QImage::Format_ARGB32);
    pixels.fill(Qt::blue);
    withImage->addBlock(new ImageBlock(pixels, withImage));
    tree.root = root;

    const MarkdownExportResult exported =
        MindMapMarkdownExporter::toMarkdown(tree.root, true);
    CHECK(exported.imageCount == 1);
    CHECK(exported.images.size() == 1);
    CHECK(exported.images.first().fileName == QStringLiteral("image_001.png"));
    // 图片块有显示尺寸时导出成 ![宽](image_001.png)，没有尺寸才是 ![](…)
    CHECK(exported.text.contains(QStringLiteral("](image_001.png)")));

    // 模拟调用方把 .md 与图片一起写到磁盘
    const QString mdPath = dir.filePath(QStringLiteral("doc.md"));
    QFile mdFile(mdPath);
    CHECK(mdFile.open(QIODevice::WriteOnly));
    mdFile.write(exported.text.toUtf8());
    mdFile.close();
    CHECK(exported.images.first().image.save(dir.filePath(exported.images.first().fileName), "PNG"));

    QFile readBack(mdPath);
    CHECK(readBack.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(readBack.readAll());
    readBack.close();

    CHECK(MindMapMarkdownImporter::containsImageReference(text));

    const MarkdownParseResult parsed =
        MindMapMarkdownImporter::parse(text, dir.path(), true);
    CHECK(parsed.ok);
    CHECK(parsed.imageTotal == 1);
    CHECK(parsed.imageMissing == 0);

    BuiltTree loaded;
    loaded.root = buildFromJson(parsed.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    MindMapNode* loadedImage = loaded.root->childNodes().at(0);
    CHECK(loadedImage->blocks().size() == 1);
    CHECK(loadedImage->blocks().at(0)->type() == BlockType::Image);
    auto* imageBlock = static_cast<ImageBlock*>(loadedImage->blocks().at(0));
    CHECK(imageBlock->imageName() == QStringLiteral("image_001.png"));
    CHECK(!imageBlock->originalImage().isNull());

    // 仅文字模式：图片变成占位文字，并且提示丢了几张
    const MarkdownExportResult textOnly =
        MindMapMarkdownExporter::toMarkdown(tree.root, false);
    CHECK(textOnly.images.isEmpty());
    CHECK(textOnly.text.contains(QStringLiteral("[图片: ")));
    CHECK(textOnly.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("图片")));

    const MarkdownParseResult textOnlyParsed =
        MindMapMarkdownImporter::parse(textOnly.text, dir.path(), false);
    CHECK(textOnlyParsed.ok);
    CHECK(textOnlyParsed.imageTotal == 0);

    // 图片文件不存在时降级成文字占位并计数
    const MarkdownParseResult missing =
        MindMapMarkdownImporter::parse(QStringLiteral("- 根\n    ![](not_here.png)\n"),
                                       dir.path(), true);
    CHECK(missing.ok);
    CHECK(missing.imageTotal == 1);
    CHECK(missing.imageMissing == 1);
    CHECK(missing.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("找不到")));
    return 0;
}

// ------------------------------------------------------------
// 多顶级条目、前言文字、空文档
// ------------------------------------------------------------
int structureEdgeCaseChecks() {
    const MarkdownParseResult multi = MindMapMarkdownImporter::parse(
        QStringLiteral("- 甲\n- 乙\n"), QString(), true);
    CHECK(multi.ok);
    CHECK(multi.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("顶级条目")));
    BuiltTree loaded;
    loaded.root = buildFromJson(multi.rootJson, &loaded.scene);
    CHECK(loaded.root != nullptr);
    // 虚拟根没有文字，两个顶级条目变成它的孩子
    CHECK(plainText(loaded.root).isEmpty());
    CHECK(loaded.root->childNodes().size() == 2);

    const MarkdownParseResult preamble = MindMapMarkdownImporter::parse(
        QStringLiteral("开头一段废话\n\n- 根\n"), QString(), true);
    CHECK(preamble.ok);
    CHECK(preamble.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("非列表")));

    const MarkdownParseResult empty = MindMapMarkdownImporter::parse(QString(), QString(), true);
    CHECK(!empty.ok);
    CHECK(!empty.errorMessage.isEmpty());

    const MarkdownParseResult noList = MindMapMarkdownImporter::parse(
        QStringLiteral("# 只有标题\n\n一段正文\n"), QString(), true);
    CHECK(!noList.ok);
    CHECK(noList.errorMessage.contains(QStringLiteral("列表")));
    CHECK(noList.errorMessage.contains(QStringLiteral("只有标题")));

    // 空节点占位往返：仍然回到空节点
    const MarkdownParseResult placeholder = MindMapMarkdownImporter::parse(
        QStringLiteral("- 根\n    - （空节点）\n"), QString(), true);
    CHECK(placeholder.ok);
    BuiltTree emptyChild;
    emptyChild.root = buildFromJson(placeholder.rootJson, &emptyChild.scene);
    CHECK(emptyChild.root != nullptr);
    CHECK(emptyChild.root->childNodes().size() == 1);
    // MindMapIO::deserialize 遇到空 blocks 会保留默认的空 TextBlock，所以这里
    // 只能断言「文字为空」，不能断言 blocks 为空。
    CHECK(plainText(emptyChild.root->childNodes().at(0)).isEmpty());
    return 0;
}

// ------------------------------------------------------------
// 路径与过滤器
// ------------------------------------------------------------
int filePathChecks() {
    CHECK(MindMapMarkdownImporter::isMarkdownFilePath(QStringLiteral("a.md")));
    CHECK(MindMapMarkdownImporter::isMarkdownFilePath(QStringLiteral("A.MD")));
    CHECK(MindMapMarkdownImporter::isMarkdownFilePath(QStringLiteral("a.markdown")));
    CHECK(!MindMapMarkdownImporter::isMarkdownFilePath(QStringLiteral("a.mmd")));
    CHECK(!MindMapMarkdownImporter::isMarkdownFilePath(QStringLiteral("a.leap")));

    const QString filter = MindMapMarkdownImporter::fileFilter();
    CHECK(filter.contains(QStringLiteral("*.md")));
    const QString openFilter = MindMapMarkdownImporter::openFileFilter();
    CHECK(openFilter.contains(QStringLiteral("*.leap")));
    CHECK(openFilter.contains(QStringLiteral("*.mmd")));
    CHECK(openFilter.contains(QStringLiteral("*.md")));

    CHECK(MindMapMarkdownImporter::countImageReferences(
              QStringLiteral("![](a.png) 和 ![[b.png]] 和纯文字")) == 2);
    CHECK(!MindMapMarkdownImporter::containsImageReference(QStringLiteral("只有文字")));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (int rc = exportStructureChecks()) return rc;
    if (int rc = roundTripChecks()) return rc;
    if (int rc = formulaChecks()) return rc;
    if (int rc = taskListChecks()) return rc;
    if (int rc = inlineFormatChecks()) return rc;
    if (int rc = imageChecks()) return rc;
    if (int rc = structureEdgeCaseChecks()) return rc;
    if (int rc = filePathChecks()) return rc;

    qInfo() << "All Markdown checks passed.";
    return 0;
}
