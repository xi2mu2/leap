// Temporary verification probe (not part of the product build).
// Checks that text formatting survives every path that rewrites text blocks:
//   MindMapNode::splitTextBlocksForMediaDrag()
//   MindMapNode::mergeAdjacentTextBlocksForMediaDrag()
//   TextBlock::mergeWith()            (Backspace at the start of a block)
//   TextBlock::splitAtCursorPreservingFormats() / SplitTextBlockCommand
//   MindMapIO save -> load round trip
#include "../MindMapNode.h"
#include "../MindMapBlock.h"
#include "../MindMapImageItem.h"
#include "../MindMapCommands.h"
#include "../MindMapIO.h"
#include <QApplication>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QImage>
#include <QUndoStack>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const QString& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    qDebug().noquote() << (ok ? "  PASS  " : "  FAIL  ") << what;
}

// Canonical fingerprint of everything that must survive the drag:
// per paragraph: block format + text; per fragment: char format + text.
static QString fingerprint(QTextDocument* doc) {
    const QFont docFont = doc->defaultFont();
    QString out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        out += QString("B|align=%1|lineH=%2|lineHT=%3|top=%4|bot=%5|left=%6|right=%7|indent=%8|ti=%9|text=%10\n")
                   .arg(int(bf.alignment()))
                   .arg(bf.lineHeight(), 0, 'f', 2)
                   .arg(int(bf.lineHeightType()))
                   .arg(bf.topMargin(), 0, 'f', 2)
                   .arg(bf.bottomMargin(), 0, 'f', 2)
                   .arg(bf.leftMargin(), 0, 'f', 2)
                   .arg(bf.rightMargin(), 0, 'f', 2)
                   .arg(bf.indent())
                   .arg(bf.textIndent(), 0, 'f', 2)
                   .arg(b.text());
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QTextCharFormat cf = f.charFormat();
            // 只比较“生效后”的字体属性：setHtml 往返会把文档默认字体写成显式
            // span，视觉效果与语义完全一样，不能算格式丢失。
            const QString fam = cf.hasProperty(QTextFormat::FontFamilies)
                ? cf.fontFamilies().toStringList().join(",") : docFont.family();
            const qreal sz = cf.hasProperty(QTextFormat::FontPointSize)
                ? cf.fontPointSize() : docFont.pointSizeF();
            out += QString("  F|%1|fam=%2|sz=%3|w=%4|col=%5|ul=%6\n")
                       .arg(f.text())
                       .arg(fam)
                       .arg(sz, 0, 'f', 2)
                       .arg(cf.fontWeight())
                       .arg(cf.foreground().color().name())
                       .arg(int(cf.underlineStyle()));
        }
    }
    return out;
}

static void showDiff(const QString& expected, const QString& actual) {
    qDebug().noquote() << "---- expected ----\n" << expected << "---- actual ----\n" << actual;
}

static int paragraphCount(MindMapNode* n) {
    auto* tb = static_cast<TextBlock*>(n->blocks().first());
    return tb->textItem()->document()->blockCount();
}

static QString defaultFontOf(MindMapNode* n, int blockIndex = 0) {
    auto* tb = static_cast<TextBlock*>(n->blocks()[blockIndex]);
    const QFont f = tb->textItem()->document()->defaultFont();
    return QString("%1/%2pt").arg(f.family()).arg(f.pointSizeF());
}

// Three paragraphs: p1 normal, p2 bold+red, p3 18pt+blue and centered 160%.
static void fillThreeParagraphs(TextBlock* tb) {
    QTextCursor c(tb->textItem()->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bold.setForeground(QColor(200, 0, 0));
    c.insertBlock();
    c.setCharFormat(bold);
    c.insertText(QString::fromUtf8("para2 bold red"));
    QTextCharFormat big;
    big.setFontPointSize(18);
    big.setForeground(QColor(0, 0, 200));
    c.insertBlock();
    c.setCharFormat(big);
    c.insertText(QString::fromUtf8("para3 18pt blue"));
    QTextBlock b = tb->textItem()->document()->begin();
    for (int i = 0; i < 2 && b.isValid(); ++i) b = b.next();
    QTextCursor bc(b);
    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignHCenter);
    bf.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
    bc.setBlockFormat(bf);
}

static void markBlock(QTextDocument* doc, int paraIndex, Qt::Alignment align, int lineHeight) {
    QTextBlock b = doc->begin();
    for (int i = 0; i < paraIndex && b.isValid(); ++i) b = b.next();
    QTextCursor c(b);
    QTextBlockFormat bf;
    bf.setAlignment(align);
    bf.setLineHeight(lineHeight, QTextBlockFormat::ProportionalHeight);
    c.setBlockFormat(bf);
}

// GUI 验证用的地图：一个 3 段文本、无图片的节点，和一个 3 段文本 + 顶部图片的节点。
// 先 addChild 再填段落格式 —— addChild 会 applyStyle(updateText=true) 重置段落对齐/行距。
static void fillGuiText(TextBlock* tb) {
    QTextCursor c(tb->textItem()->document());
    c.movePosition(QTextCursor::End);
    c.insertText(QString::fromUtf8("第一段：普通文本，左对齐。"));
    c.insertBlock();
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bold.setForeground(QColor(200, 0, 0));
    c.setCharFormat(bold);
    c.insertText(QString::fromUtf8("第二段：这一段居中，行距 160%，判断格式有没有丢就看它。"));
    c.insertBlock();
    QTextCharFormat big;
    big.setFontPointSize(18);
    big.setForeground(QColor(0, 0, 200));
    c.setCharFormat(big);
    c.insertText(QString::fromUtf8("第三段：18pt 蓝色。"));
    markBlock(tb->textItem()->document(), 1, Qt::AlignHCenter, 160);
}

static int makeGuiTestMap(const QString& mapPath, const QString& imagePath) {
    QImage img(260, 90, QImage::Format_ARGB32);
    img.fill(QColor(255, 205, 120));
    {
        QPainter p(&img);
        QFont f = p.font();
        f.setFamily(QStringLiteral("Microsoft YaHei"));
        f.setPointSizeF(16);
        p.setFont(f);
        p.setPen(QColor(90, 50, 0));
        p.drawText(img.rect(), Qt::AlignCenter, QString::fromUtf8("示例图片"));
        p.setPen(QPen(QColor(200, 120, 0), 3));
        p.drawRect(img.rect().adjusted(2, 2, -4, -4));
    }
    if (!img.save(imagePath)) {
        qDebug().noquote() << "failed to save image" << imagePath;
        return 1;
    }

    auto* root = new MindMapNode(QString::fromUtf8("根节点"), nullptr);

    auto* a = new MindMapNode(QString::fromUtf8("无图片节点"), nullptr);
    root->addChild(a);
    fillGuiText(static_cast<TextBlock*>(a->blocks().first()));

    auto* b = new MindMapNode(QString::fromUtf8("有图片节点"), nullptr);
    root->addChild(b);
    fillGuiText(static_cast<TextBlock*>(b->blocks().first()));
    auto* ib = new ImageBlock(img, b);
    b->addBlock(ib, 0);
    ib->imageItem()->setImageBlock(ib);
    b->calcNodeSize();

    if (!MindMapIO::saveToFile(root, mapPath)) {
        qDebug().noquote() << "failed to save map" << mapPath;
        return 1;
    }
    qDebug().noquote() << "saved map" << mapPath << "image" << imagePath;

    // 回读一遍，确认文件里确实带上了居中/160% 的段落格式。
    MindMapNode* loaded = MindMapIO::loadFromFile(mapPath, nullptr);
    if (!loaded) {
        qDebug().noquote() << "failed to reload" << mapPath;
        return 1;
    }
    for (MindMapNode* child : loaded->childNodes()) {
        const auto* tb = dynamic_cast<TextBlock*>(child->blocks().first());
        qDebug().noquote() << "node" << child->nodeText() << "blocks" << child->blocks().size()
                           << "paras" << (tb ? tb->textItem()->document()->blockCount() : -1);
        if (tb) qDebug().noquote() << fingerprint(tb->textItem()->document());
    }
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc > 1 && QString::fromLatin1(argv[1]) == QLatin1String("makemap")) {
        return makeGuiTestMap(QStringLiteral("D:/projects/leap_current/.tmp_probe/gui_test.leap"),
                              QStringLiteral("D:/projects/leap_current/.tmp_probe/gui_test_image.png"));
    }

    qDebug().noquote() << "app default font:" << QApplication::font().family()
                       << QApplication::font().pointSizeF() << "pt";

    // A: node WITHOUT image - single text block holding 3 paragraphs.
    {
        qDebug().noquote() << "\n=== A: multi-paragraph block (node without image) ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("para1 normal"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        fillThreeParagraphs(tb);
        const QString before = fingerprint(tb->textItem()->document());
        const QString fontBefore = defaultFontOf(n);

        n->splitTextBlocksForMediaDrag();
        check(n->blocks().size() == 3, QString("split -> 3 blocks (got %1)").arg(n->blocks().size()));
        const QString part0 = fingerprint(static_cast<TextBlock*>(n->blocks()[0])->textItem()->document());
        check(part0 == before.section("\n", 0, 1) + "\n", "split keeps paragraph #1 block format");
        check(defaultFontOf(n) == fontBefore, QString("split keeps default font (%1)").arg(fontBefore));

        // The centered 160% paragraph must survive the split as block #3.
        const QString part2 = fingerprint(static_cast<TextBlock*>(n->blocks()[2])->textItem()->document());
        check(part2.contains("align=4") && part2.contains("lineH=160.00"),
              "split keeps the centered 160% paragraph");

        n->mergeAdjacentTextBlocksForMediaDrag();
        check(n->blocks().size() == 1, "merge -> 1 block");
        check(paragraphCount(n) == 3, QString("merge keeps 3 paragraphs (got %1)").arg(paragraphCount(n)));
        const QString after = fingerprint(static_cast<TextBlock*>(n->blocks().first())->textItem()->document());
        check(after == before, "merge restores the exact original formatting");
        check(defaultFontOf(n) == fontBefore, "merge keeps default font");
        const QString html = static_cast<TextBlock*>(n->blocks().first())->toHtml();
        check(html.contains("align=\"center\"") && html.contains("line-height:160%"),
              "merged html still carries align=center + line-height:160%");
        if (after != before) {
            qDebug().noquote() << "---- expected ----\n" << before << "---- actual ----\n" << after;
        }
    }

    // C: merge alone - two single-paragraph blocks, the 2nd one centered.
    {
        qDebug().noquote() << "\n=== C: merge two single-paragraph blocks ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("first block"), nullptr);
        auto* tb2 = new TextBlock(n);
        tb2->setPlainText(QString::fromUtf8("second block"));
        n->addBlock(tb2, -1);
        markBlock(tb2->textItem()->document(), 0, Qt::AlignHCenter, 160);
        const QString b0 = fingerprint(static_cast<TextBlock*>(n->blocks()[0])->textItem()->document());
        const QString b1 = fingerprint(static_cast<TextBlock*>(n->blocks()[1])->textItem()->document());

        n->mergeAdjacentTextBlocksForMediaDrag();
        check(n->blocks().size() == 1, "merge -> 1 block");
        check(paragraphCount(n) == 2, QString("merge keeps 2 paragraphs (got %1)").arg(paragraphCount(n)));
        const QString merged = fingerprint(static_cast<TextBlock*>(n->blocks().first())->textItem()->document());
        check(merged == b0 + b1, "merge keeps both paragraphs' formatting");
    }

    // D: node WITH image - split/merge must leave the text untouched.
    {
        qDebug().noquote() << "\n=== D: node with image (text | image | text) ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("text above"), nullptr);
        auto* tb2 = new TextBlock(n);
        tb2->setPlainText(QString::fromUtf8("text below"));
        n->addBlock(tb2, -1);
        markBlock(tb2->textItem()->document(), 0, Qt::AlignHCenter, 160);
        QImage img(32, 32, QImage::Format_ARGB32);
        img.fill(Qt::red);
        auto* ib = new ImageBlock(img, n);
        n->addBlock(ib, 1);
        ib->imageItem()->setImageBlock(ib);
        const QString htmlBefore = static_cast<TextBlock*>(n->blocks().first())->toHtml()
                                 + static_cast<TextBlock*>(n->blocks().last())->toHtml();

        n->splitTextBlocksForMediaDrag();
        n->mergeAdjacentTextBlocksForMediaDrag();
        const QString htmlAfter = static_cast<TextBlock*>(n->blocks().first())->toHtml()
                                + static_cast<TextBlock*>(n->blocks().last())->toHtml();
        check(htmlBefore == htmlAfter, "text next to an image is not rewritten");

        // Drag the image to the bottom: the two text blocks become adjacent and merge.
        qDebug().noquote() << "  block count before move:" << n->blocks().size();
        n->moveBlock(1, 3);  // 索引是“移动后的目标位置”，3 = 末尾
        QString types;
        for (auto* b : n->blocks()) types += b->type() == BlockType::Text ? "T" : "I";
        qDebug().noquote() << "  after moveBlock(1,3): blocks=" << n->blocks().size() << "types=" << types;
        n->mergeAdjacentTextBlocksForMediaDrag();
        types.clear();
        for (auto* b : n->blocks()) types += b->type() == BlockType::Text ? "T" : "I";
        qDebug().noquote() << "  after merge: blocks=" << n->blocks().size() << "types=" << types;
        check(n->blocks().size() == 2, "image moved to bottom -> image + 1 merged text block");
        if (n->blocks().size() >= 1 && n->blocks()[0]->type() == BlockType::Text) {
            auto* firstText = static_cast<TextBlock*>(n->blocks()[0]);
            const QString merged = fingerprint(firstText->textItem()->document());
            check(merged.contains("align=4") && merged.contains("lineH=160.00"),
                  "merged text keeps the centered 160% paragraph");
        } else {
            check(false, "merged text block is the first block");
        }
    }

    // E: node that received applyStyle(updateText=true) - explicit font/colour on every char.
    {
        qDebug().noquote() << "\n=== E: after applyStyle(updateText=true) ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("para1 normal"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        fillThreeParagraphs(tb);
        n->applyStyle(n->styleForCurrentLevel(), true);
        const QString before = fingerprint(tb->textItem()->document());
        const QString fontBefore = defaultFontOf(n);

        n->splitTextBlocksForMediaDrag();
        check(defaultFontOf(n) == fontBefore, QString("split keeps style font (%1), not app default").arg(fontBefore));
        check(defaultFontOf(n, 2) == fontBefore, "every split part keeps the style font");
        n->mergeAdjacentTextBlocksForMediaDrag();
        check(defaultFontOf(n) == fontBefore, "merge keeps style font");
        const QString after = fingerprint(static_cast<TextBlock*>(n->blocks().first())->textItem()->document());
        check(after == before, "split+merge round-trips the applied style exactly");
        if (after != before) {
            qDebug().noquote() << "---- expected ----\n" << before << "---- actual ----\n" << after;
        }
    }

    // F: blank paragraph in the middle (Shift+Enter twice) must not be lost or duplicated.
    {
        qDebug().noquote() << "\n=== F: blank paragraph in the middle ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("a"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        QTextCursor c(tb->textItem()->document());
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        c.insertBlock();
        c.insertText(QString::fromUtf8("b"));
        const QString before = fingerprint(tb->textItem()->document());
        n->splitTextBlocksForMediaDrag();
        check(n->blocks().size() == 3, QString("split -> 3 blocks incl. blank (got %1)").arg(n->blocks().size()));
        n->mergeAdjacentTextBlocksForMediaDrag();
        check(paragraphCount(n) == 3, QString("merge keeps the blank paragraph (got %1)").arg(paragraphCount(n)));
        const QString after = fingerprint(static_cast<TextBlock*>(n->blocks().first())->textItem()->document());
        check(after == before, "blank line survives split+merge");
    }

    // G: TextBlock::mergeWith (Backspace at block start) must keep paragraph formats.
    {
        qDebug().noquote() << "\n=== G: TextBlock::mergeWith ===";
        MindMapNode* n1 = new MindMapNode(QString::fromUtf8("first"), nullptr);
        auto* t1 = static_cast<TextBlock*>(n1->blocks().first());
        MindMapNode* n2 = new MindMapNode(QString::fromUtf8("second"), nullptr);
        auto* t2 = static_cast<TextBlock*>(n2->blocks().first());
        QTextCursor c(t2->textItem()->document());
        c.movePosition(QTextCursor::End);
        QTextCharFormat big;
        big.setFontPointSize(18);
        big.setForeground(QColor(0, 0, 200));
        c.insertBlock();
        c.setCharFormat(big);
        c.insertText(QString::fromUtf8("third"));
        markBlock(t2->textItem()->document(), 0, Qt::AlignHCenter, 160);

        t1->mergeWith(t2);
        QTextDocument* doc = t1->textItem()->document();
        check(doc->blockCount() == 2, QString("mergeWith -> 2 paragraphs (got %1)").arg(doc->blockCount()));
        check(doc->begin().text() == QString::fromUtf8("firstsecond"),
              QString("first paragraph joins the last line (got '%1')").arg(doc->begin().text()));
        const QString fp = fingerprint(doc);
        check(fp.contains("sz=18.00") && fp.contains("col=#0000c8"),
              "mergeWith keeps the 18pt blue paragraph");
        check(doc->begin().next().text() == QString::fromUtf8("third"), "second paragraph text intact");
    }

    // H: splitting inside a styled paragraph keeps the format on both halves.
    {
        qDebug().noquote() << "\n=== H: splitAtCursorPreservingFormats ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("AAAA BBBB"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        QTextCursor c(tb->textItem()->document());
        c.movePosition(QTextCursor::End);
        QTextCharFormat big;
        big.setFontPointSize(18);
        big.setForeground(QColor(0, 0, 200));
        c.insertBlock();
        c.setCharFormat(big);
        c.insertText(QString::fromUtf8("CCCC"));
        markBlock(tb->textItem()->document(), 0, Qt::AlignHCenter, 160);
        tb->setCursorPosition(5);  // right after "AAAA "

        TextBlock* tail = tb->splitAtCursorPreservingFormats();
        check(tail != nullptr, "split returns the new block");
        if (!tail) {
            qDebug().noquote() << "  (split failed, skipping the rest of H)";
        } else {
            QTextDocument* headDoc = tb->textItem()->document();
            QTextDocument* tailDoc = tail->textItem()->document();
            check(headDoc->blockCount() == 1, QString("head keeps 1 paragraph (got %1)").arg(headDoc->blockCount()));
            check(headDoc->begin().text() == QString::fromUtf8("AAAA "),
                  QString("head text = 'AAAA ' (got '%1')").arg(headDoc->begin().text()));
            check(fingerprint(headDoc).contains("align=4") && fingerprint(headDoc).contains("lineH=160.00"),
                  "head keeps the centered 160% paragraph");
            check(tailDoc->blockCount() == 2, QString("tail keeps 2 paragraphs (got %1)").arg(tailDoc->blockCount()));
            const QString tfp = fingerprint(tailDoc);
            check(tailDoc->begin().text() == QString::fromUtf8("BBBB"),
                  QString("tail text = 'BBBB' (got '%1')").arg(tailDoc->begin().text()));
            check(tfp.contains("align=4") && tfp.contains("lineH=160.00"),
                  "tail keeps the centered 160% paragraph");
            check(tfp.contains("sz=18.00") && tfp.contains("col=#0000c8"),
                  "tail keeps the 18pt blue paragraph");
            QString joined = tb->toPlainText() + tail->toPlainText();
            joined.remove(QLatin1Char('\n'));
            check(joined == QString::fromUtf8("AAAA BBBBCCCC"),
                  "both halves together still hold the original text");
        }
    }

    // I: SplitTextBlockCommand undo/redo.
    {
        qDebug().noquote() << "\n=== I: SplitTextBlockCommand undo/redo ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("para1 normal"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        fillThreeParagraphs(tb);
        // 真实场景下拆分发生在编辑状态，此时文本项已经是层级样式字体；
        // 先进入编辑，避免“拆分后才被设字体”影响 html 里的 body 字体。
        n->startEditBlock(tb);
        const QString before = fingerprint(tb->textItem()->document());
        const int pos = tb->textItem()->document()->findBlockByNumber(1).position() + 4;
        QUndoStack stack;
        stack.push(new SplitTextBlockCommand(n, tb, 0, pos, nullptr));
        check(n->blocks().size() == 2, QString("command splits into 2 blocks (got %1)").arg(n->blocks().size()));
        const QString headAfter = fingerprint(static_cast<TextBlock*>(n->blocks()[0])->textItem()->document());
        const QString tailAfter = fingerprint(static_cast<TextBlock*>(n->blocks()[1])->textItem()->document());

        stack.undo();
        check(n->blocks().size() == 1, QString("undo -> 1 block (got %1)").arg(n->blocks().size()));
        const QString undone = fingerprint(static_cast<TextBlock*>(n->blocks()[0])->textItem()->document());
        check(undone == before, "undo restores the original formatting exactly");
        if (undone != before) showDiff(before, undone);

        stack.redo();
        check(n->blocks().size() == 2, QString("redo -> 2 blocks (got %1)").arg(n->blocks().size()));
        const QString redoneHead = fingerprint(static_cast<TextBlock*>(n->blocks()[0])->textItem()->document());
        check(redoneHead == headAfter, "redo reproduces the first half exactly");
        if (redoneHead != headAfter) showDiff(headAfter, redoneHead);
        const QString redoneTail = fingerprint(static_cast<TextBlock*>(n->blocks()[1])->textItem()->document());
        check(redoneTail == tailAfter, "redo reproduces the second half exactly");
        if (redoneTail != tailAfter) showDiff(tailAfter, redoneTail);
    }

    // J: save -> load round trip must keep explicit paragraph and character formats.
    {
        qDebug().noquote() << "\n=== J: MindMapIO save/load round trip ===";
        MindMapNode* n = new MindMapNode(QString::fromUtf8("para1 normal"), nullptr);
        auto* tb = static_cast<TextBlock*>(n->blocks().first());
        fillThreeParagraphs(tb);
        n->applyStyle(n->styleForCurrentLevel(), true);
        const QString before = fingerprint(tb->textItem()->document());
        // 写到工作区内：%TEMP% 对本进程拒绝访问（QTemporaryDir 也是因此失败）。
        const QString path = QStringLiteral("D:/projects/leap_current/.tmp_probe/roundtrip.mindmap");
        const bool saved = MindMapIO::saveToFile(n, path);
        check(saved, "saved to " + path);
        MindMapNode* loaded = saved ? MindMapIO::loadFromFile(path, nullptr) : nullptr;
        check(loaded != nullptr, "loaded back");
        if (loaded && !loaded->blocks().isEmpty()
            && loaded->blocks().first()->type() == BlockType::Text) {
            auto* lt = static_cast<TextBlock*>(loaded->blocks().first());
            const QString after = fingerprint(lt->textItem()->document());
            check(after == before, "load keeps the exact formatting that was saved");
            if (after != before) {
                qDebug().noquote() << "---- expected ----\n" << before << "---- actual ----\n" << after;
            }
        } else {
            check(false, "loaded node has a text block");
        }
        QFile::remove(path);
    }

    // K: 真实拖拽路径（QGraphicsScene 事件）：无图片节点插入图片后，把图片从顶部拖到底部。
    {
        qDebug().noquote() << "\n=== K: real drag of an inserted image (scene events) ===";
        QGraphicsScene scene;
        auto* root = new MindMapNode(QString::fromUtf8("root"), nullptr);
        scene.addItem(root);
        auto* node = new MindMapNode(QString::fromUtf8("para1 normal"), nullptr);
        root->addChild(node);
        scene.addItem(node); // addChild 只建父子关系，场景归属要单独建立（产品里由命令/加载路径完成）
        auto* tb = static_cast<TextBlock*>(node->blocks().first());
        fillThreeParagraphs(tb);
        node->calcNodeSize();
        node->updateBlockLayout();

        // 等同工具栏“图片”/Ctrl+V：InsertBlockCommand 把图片块插到文本之前
        QImage img(120, 40, QImage::Format_ARGB32);
        img.fill(QColor(255, 205, 120));
        auto* ib = new ImageBlock(img, node);

        QUndoStack stack;
        MindMapNode::setUndoStack(&stack);
        stack.push(new InsertBlockCommand(node, ib, 0, &scene));
        node->calcNodeSize();
        node->updateBlockLayout();

        const QString before = fingerprint(tb->textItem()->document());
        check(node->blocks().size() == 2 && node->blocks().first()->type() == BlockType::Image,
              "image block sits above the text block before the drag");

        QGraphicsItem* item = ib->imageItem();
        const QPointF start = item->boundingRect().center();

        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setPos(start);
        press.setScenePos(item->mapToScene(start));
        press.setScreenPos(item->mapToScene(start).toPoint());
        press.setButton(Qt::LeftButton);
        press.setButtons(Qt::LeftButton);
        press.setModifiers(Qt::NoModifier);
        scene.sendEvent(item, &press);

        const QPointF mid = start + QPointF(0, QApplication::startDragDistance() + 20);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setPos(mid);
        move.setScenePos(item->mapToScene(mid));
        move.setScreenPos(item->mapToScene(mid).toPoint());
        move.setLastPos(start);
        move.setLastScenePos(item->mapToScene(start));
        move.setButton(Qt::NoButton);
        move.setButtons(Qt::LeftButton);
        move.setModifiers(Qt::NoModifier);
        scene.sendEvent(item, &move);

        check(node->blocks().size() == 4,
              QString("drag splits the 3 paragraphs into 3 blocks (got %1)").arg(node->blocks().size()));

        const QPointF endNode(node->rect().width() / 2, node->rect().height() + 40);
        const QPointF end = item->mapFromParent(endNode);
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setPos(end);
        release.setScenePos(item->mapToScene(end));
        release.setScreenPos(item->mapToScene(end).toPoint());
        release.setLastPos(mid);
        release.setLastScenePos(item->mapToScene(mid));
        release.setButton(Qt::LeftButton);
        release.setButtons(Qt::NoButton);
        release.setModifiers(Qt::NoModifier);
        scene.sendEvent(item, &release);

        check(node->blocks().size() == 2,
              QString("after release the text blocks merge back, image kept (got %1)").arg(node->blocks().size()));
        check(!node->blocks().isEmpty() && node->blocks().last()->type() == BlockType::Image,
              "image ends up below the text");
        const QString after = (node->blocks().isEmpty() || node->blocks().first()->type() != BlockType::Text)
            ? QString()
            : fingerprint(static_cast<TextBlock*>(node->blocks().first())->textItem()->document());
        check(after == before, "dragging keeps the paragraph formats of the merged text");
        if (after != before) showDiff(before, after);

        check(stack.count() == 2,
              QString("MoveMediaBlockCommand pushed on top of InsertBlockCommand (got %1)").arg(stack.count()));
        stack.undo();
        check(node->blocks().size() == 2 && !node->blocks().isEmpty()
                  && node->blocks().first()->type() == BlockType::Image,
              "undo puts the image back on top");
        const QString undone = fingerprint(tb->textItem()->document());
        check(undone == before, "undo restores the text formatting");
        if (undone != before) showDiff(before, undone);
        stack.redo();
        check(!node->blocks().isEmpty() && node->blocks().last()->type() == BlockType::Image,
              "redo moves the image below again");

        MindMapNode::setUndoStack(nullptr);
    }

    // L: 新建文档的根节点从未经过 addChild 的样式刷新（applyStyle(updateText=true)）。
    {
        qDebug().noquote() << "\n=== L: freshly created root vs child text style ===";
        QGraphicsScene scene;
        auto* root = new MindMapNode(QString::fromUtf8("root"));
        scene.addItem(root);
        auto* child = new MindMapNode(QString::fromUtf8("child"));
        root->addChild(child);

        auto docFont = [](MindMapNode* n) {
            auto* tb = static_cast<TextBlock*>(n->blocks().first());
            return tb->textItem()->document()->defaultFont();
        };
        auto firstChar = [](MindMapNode* n) {
            auto* tb = static_cast<TextBlock*>(n->blocks().first());
            QTextCursor c(tb->textItem()->document()->begin());
            c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            return c.charFormat();
        };
        const QFont styleFont = root->styleForCurrentLevel().font();
        qDebug().noquote() << "style font:" << styleFont.family() << styleFont.pointSizeF()
                           << "app font:" << QApplication::font().family() << QApplication::font().pointSizeF();
        qDebug().noquote() << "root  doc font:" << docFont(root).family() << docFont(root).pointSizeF()
                           << "explicitCharFont:" << firstChar(root).hasProperty(QTextFormat::FontPointSize);
        qDebug().noquote() << "child doc font:" << docFont(child).family() << docFont(child).pointSizeF()
                           << "explicitCharFont:" << firstChar(child).hasProperty(QTextFormat::FontPointSize);

        check(docFont(child).family() == styleFont.family(), "child uses the level style font");
        check(firstChar(child).hasProperty(QTextFormat::FontPointSize), "child has explicit char font");
        check(docFont(root).family() != styleFont.family(), "root falls back to the application font");
        check(!firstChar(root).hasProperty(QTextFormat::FontPointSize), "root has no explicit char font");

        // 新建文档现在会在 setupDefaultTree() 末尾做一次整树刷新（MainWindow.cpp）。
        root->applyLevelStyles(root->levelStyles());
        qDebug().noquote() << "after whole-tree refresh root doc font:" << docFont(root).family()
                           << docFont(root).pointSizeF()
                           << "explicitCharFont:" << firstChar(root).hasProperty(QTextFormat::FontPointSize);
        check(docFont(root).family() == styleFont.family(),
              "whole-tree refresh gives the root the level style font");
        check(firstChar(root).hasProperty(QTextFormat::FontPointSize),
              "whole-tree refresh writes explicit char formats on the root");
        check(MindMapIO::isLoading() == false, "refresh runs outside the loading scope");

        // 进入编辑时 onDocumentChanged() 会 setFont(styleForCurrentLevel().font())。
        root->startEdit();
        qDebug().noquote() << "after startEdit root doc font:" << docFont(root).family()
                           << docFont(root).pointSizeF();
        check(docFont(root).family() == styleFont.family(), "editing the root keeps the style font");
    }

    qDebug().noquote() << QString("\n=== %1 checks, %2 failed ===").arg(g_checks).arg(g_failures);
    return g_failures == 0 ? 0 : 1;
}
