#include "MindMapMarkdown.h"

#include "MindMapBlock.h"
#include "MindMapImageItem.h"
#include "MindMapNode.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextDocumentFragment>
#include <QUrl>

#include <functional>
#include <memory>
#include <vector>

namespace {

// 空节点在 .md 里的占位文字。导入时唯一内容正好是它的节点会被还原成空节点。
const QString kEmptyNodePlaceholder = QStringLiteral("（空节点）");
// 任务列表降级后的前缀（单向，不写回 - [x]）
const QString kCheckedPrefix = QStringLiteral("☑ ");
const QString kUncheckedPrefix = QStringLiteral("☐ ");

// ============================================================
// 导出端小工具
// ============================================================

QString fallbackImageName(ImageBlock* block) {
    const QString name = block ? block->imageName() : QString();
    return name.isEmpty() ? QStringLiteral("未命名图片") : name;
}

QString imageReferenceText(const MarkdownImageAsset& asset) {
    const qreal width = asset.displaySize.width();
    if (width > 0) {
        return QStringLiteral("![%1](%2)").arg(QString::number(qRound(width)), asset.fileName);
    }
    return QStringLiteral("![](%1)").arg(asset.fileName);
}

// 文本块里有没有 Markdown 表达不了的格式（颜色、背景、对齐）。
// 粗体 / 斜体 / 删除线 / 链接都能被 Markdown 保留，不算损耗。
//
// 注意：applyStyle() 会给整篇文档 mergeCharFormat 写入一个显式前景色，
// 所以“有前景色”本身不代表用户改过颜色——只有和节点样式色不一致才算损耗。
bool blockHasUnrepresentableFormat(TextBlock* block) {
    QTextDocument* doc = (block && block->textItem()) ? block->textItem()->document() : nullptr;
    if (!doc) return false;
    const MindMapNode* node = block->parentNode();
    const QColor styleColor = node ? node->styleForCurrentLevel().textColor : QColor();
    for (QTextBlock paragraph = doc->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
        const QTextBlockFormat blockFormat = paragraph.blockFormat();
        if (blockFormat.alignment() != Qt::AlignLeft && blockFormat.alignment() != Qt::AlignLeading) {
            return true;
        }
        for (auto it = paragraph.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat charFormat = it.fragment().charFormat();
            if (charFormat.foreground().style() != Qt::NoBrush && styleColor.isValid() &&
                charFormat.foreground().color() != styleColor) {
                return true;
            }
            if (charFormat.background().style() != Qt::NoBrush &&
                charFormat.background().color().alpha() > 0) {
                return true;
            }
        }
    }
    return false;
}

// 行内文字里需要转义的字符：这些一旦原样出现，读者那边就会被解析成格式。
QString escapeInlineText(const QString& text) {
    QString escaped;
    escaped.reserve(text.size() + 8);
    static const QString kSpecial = QStringLiteral("\\`*_{}[]<>|~$");
    for (QChar ch : text) {
        if (kSpecial.contains(ch)) escaped += QLatin1Char(0x5C);
        escaped += ch;
    }
    return escaped;
}

// 把一段富文本转成 Markdown。
//
// 为什么不用 QTextDocument::toMarkdown()：Qt 6.5 的实现只保留删除线与链接，
// 粗体 / 斜体一律丢掉（实测 "<b>x</b>" 导出成纯 "x"），而我们至少要保住这两样。
// 这里改成逐 fragment 手动加标记：**粗体**、*斜体*、~~删除线~~、[文字](链接)。
QString inlineMarkdownFromDocument(QTextDocument* document) {
    if (!document) return QString();
    QStringList paragraphs;
    for (QTextBlock paragraph = document->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
        QString text;
        for (auto it = paragraph.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            QString piece = escapeInlineText(fragment.text());
            if (piece.isEmpty()) continue;
            if (format.fontStrikeOut()) piece = QStringLiteral("~~") + piece + QStringLiteral("~~");
            if (format.fontItalic()) piece = QLatin1Char(42) + piece + QLatin1Char(42);
            if (format.fontWeight() >= QFont::Bold) {
                piece = QStringLiteral("**") + piece + QStringLiteral("**");
            }
            if (format.isAnchor() && !format.anchorHref().isEmpty()) {
                piece = QLatin1Char(91) + piece + QStringLiteral("](") + format.anchorHref() +
                        QLatin1Char(41);
            }
            text += piece;
        }
        paragraphs.append(text);
    }
    // Qt 文档末尾总带一个空段落，别把它写成空行
    while (!paragraphs.isEmpty() && paragraphs.last().isEmpty()) paragraphs.removeLast();
    return paragraphs.join(QLatin1Char(10));
}

// 列表项首行如果本身就是 Markdown 块级语法（- * + > # 1. $$），
// 读者那边会把它解析成结构而不是文字，这里统一加反斜杠转义。
QString escapeLeadingMarkers(const QString& line) {
    if (line.isEmpty()) return line;
    const QChar first = line.at(0);
    // 已经是一段转义过的文字（或正文本身以反斜杠开头）就不要再动它
    if (first == QLatin1Char('\\')) return line;

    const auto followedBySpace = [&line](int index) {
        return index >= line.size() || line.at(index) == QLatin1Char(' ') ||
               line.at(index) == QLatin1Char('\t');
    };

    if (first == QLatin1Char('-') || first == QLatin1Char('*') || first == QLatin1Char('+') ||
        first == QLatin1Char('>') || first == QLatin1Char('#')) {
        if (followedBySpace(1)) return QLatin1Char('\\') + line;
    }
    if (first == QLatin1Char('$') && line.startsWith(QStringLiteral("$$"))) {
        return QLatin1Char('\\') + line;
    }
    int digits = 0;
    while (digits < line.size() && line.at(digits).isDigit()) ++digits;
    if (digits > 0 && digits < line.size() &&
        (line.at(digits) == QLatin1Char('.') || line.at(digits) == QLatin1Char(')')) &&
        followedBySpace(digits + 1)) {
        return QLatin1Char('\\') + line;
    }
    return line;
}

QString imageToBase64Png(const QImage& image) {
    if (image.isNull()) return QString();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

// ============================================================
// 导入端：一行一行的扫描
// ============================================================

struct MdItem {
    int indent = 0;          // 列表标记所在的列
    int lineNumber = 0;
    QStringList raw;         // 首行内容 + 续行（已去掉行首缩进）
    QVector<MdItem*> children;
    MdItem* parent = nullptr;
};

// 行首缩进折成列数（tab 记 4 列），同时给出第一个非空白字符的下标。
int leadingIndent(const QString& line, int* contentStart) {
    int column = 0;
    int index = 0;
    while (index < line.size()) {
        const QChar ch = line.at(index);
        if (ch == QLatin1Char(' ')) {
            ++column;
            ++index;
        } else if (ch == QLatin1Char('\t')) {
            column += 4;
            ++index;
        } else {
            break;
        }
    }
    if (contentStart) *contentStart = index;
    return column;
}

// 判断这一行是不是列表项；是就顺手拆出括号里的任务勾选状态。
bool parseListMarker(const QString& line, int* indent, QString* content,
                     bool* isTask, bool* taskDone) {
    int start = 0;
    const int column = leadingIndent(line, &start);
    if (start >= line.size()) return false;
    const QChar marker = line.at(start);
    if (marker != QLatin1Char('-') && marker != QLatin1Char('*') && marker != QLatin1Char('+')) {
        return false;
    }
    if (start + 1 < line.size() && line.at(start + 1) != QLatin1Char(' ') &&
        line.at(start + 1) != QLatin1Char('\t')) {
        return false;
    }
    QString rest = (start + 1 < line.size()) ? line.mid(start + 1) : QString();
    while (rest.startsWith(QLatin1Char(' ')) || rest.startsWith(QLatin1Char('\t'))) {
        rest.remove(0, 1);
    }

    *isTask = false;
    *taskDone = false;
    if (rest.startsWith(QLatin1Char('['))) {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close >= 2 && close <= 3) {
            const QString box = rest.mid(1, close - 1);
            const bool afterOk = close + 1 >= rest.size() ||
                                 rest.at(close + 1) == QLatin1Char(' ') ||
                                 rest.at(close + 1) == QLatin1Char('\t');
            // 括号里只允许一个字符：空格 = 未完成，其它（x/X/任意） = 已完成。
            if (afterOk && box.size() == 1) {
                *isTask = true;
                // 括号里只要有非空字符就算完成（Obsidian 语义），否则是未完成
                *taskDone = !box.trimmed().isEmpty();
                rest = rest.mid(close + 1);
                while (rest.startsWith(QLatin1Char(' ')) || rest.startsWith(QLatin1Char('\t'))) {
                    rest.remove(0, 1);
                }
            }
        }
    }

    *indent = column;
    *content = rest;
    return true;
}

// 从 ![宽](路径) / ![[路径|宽]] 里取出路径与宽度
bool parseImageTarget(const QString& raw, QString* path, int* width) {
    QString target = raw.trimmed();
    if (target.isEmpty()) return false;
    if (target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>'))) {
        target = target.mid(1, target.size() - 2).trimmed();
    }
    // 去掉 Markdown 的 "title"
    if (target.endsWith(QLatin1Char('"'))) {
        const int space = target.lastIndexOf(QStringLiteral(" \""));
        if (space > 0) target = target.left(space).trimmed();
    }
    // Obsidian 的 |宽度 或 |宽x高
    const int bar = target.indexOf(QLatin1Char('|'));
    if (bar >= 0) {
        const QString tail = target.mid(bar + 1);
        const QStringList dimensions = tail.split(QLatin1Char('x'));
        bool ok = false;
        const int parsed = dimensions.first().trimmed().toInt(&ok);
        if (ok && parsed > 0) *width = parsed;
        target = target.left(bar);
    }
    *path = target.trimmed();
    return !path->isEmpty();
}

bool matchImageReference(const QString& line, QString* path, int* width) {
    static const QRegularExpression markdownImage(
        QStringLiteral("^!\\[([^\\]]*)\\]\\((.+)\\)$"));
    static const QRegularExpression obsidianEmbed(QStringLiteral("^!\\[\\[(.+)\\]\\]$"));

    const QRegularExpressionMatch mdMatch = markdownImage.match(line);
    if (mdMatch.hasMatch()) {
        const QString alt = mdMatch.captured(1).trimmed();
        int digits = 0;
        while (digits < alt.size() && alt.at(digits).isDigit()) ++digits;
        if (digits > 0) {
            bool ok = false;
            const int parsed = alt.left(digits).toInt(&ok);
            if (ok && parsed > 0) *width = parsed;
        }
        return parseImageTarget(mdMatch.captured(2), path, width);
    }
    const QRegularExpressionMatch obsidianMatch = obsidianEmbed.match(line);
    if (obsidianMatch.hasMatch()) {
        return parseImageTarget(obsidianMatch.captured(1), path, width);
    }
    return false;
}

QString resolveImagePath(const QString& baseDir, const QString& rawPath) {
    QString path = rawPath.trimmed();
    path = QUrl::fromPercentEncoding(path.toUtf8());
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (QFileInfo(path).isAbsolute()) return path;
    if (baseDir.isEmpty()) return path;
    return QDir(baseDir).absoluteFilePath(path);
}

// Markdown -> 富文本 HTML。走 Qt 自己的解析器，粗体/斜体/删除线/链接都能保留；
// 用 fragment 而不是 QTextDocument 是为了拿到的 HTML 里不带整篇的颜色和字体，
// 免得盖掉节点自己的样式。
QString markdownToHtml(const QString& markdown) {
    const QTextDocumentFragment fragment =
        QTextDocumentFragment::fromMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    return fragment.toHtml();
}

// 一个节点的内容行 -> blocks 数组
QJsonArray buildBlocks(const QStringList& raw, const QString& baseDir, bool includeImages,
                       int* imageTotal, int* imageMissing) {
    QJsonArray blocks;
    QStringList textLines;

    auto flushText = [&]() {
        if (textLines.isEmpty()) return;
        while (!textLines.isEmpty() && textLines.first().trimmed().isEmpty()) textLines.removeFirst();
        while (!textLines.isEmpty() && textLines.last().trimmed().isEmpty()) textLines.removeLast();
        if (!textLines.isEmpty()) {
            const QString markdown = textLines.join(QLatin1Char('\n'));
            const QString html = markdownToHtml(markdown);
            if (!html.trimmed().isEmpty()) {
                QJsonObject block;
                block[QStringLiteral("type")] = QStringLiteral("text");
                block[QStringLiteral("html")] = html;
                blocks.append(block);
            }
        }
        textLines.clear();
    };

    // 行与行之间补一个空行，交给 Markdown 当段落分隔；
    // 否则 Markdown 会把相邻两行并成一段，节点里的换行就丢了。
    auto appendLine = [&](const QString& line) {
        if (!textLines.isEmpty() && !textLines.last().trimmed().isEmpty() &&
            !line.trimmed().isEmpty()) {
            textLines.append(QString());
        }
        textLines.append(line);
    };

    for (int i = 0; i < raw.size(); ++i) {
        const QString line = raw.at(i);
        const QString trimmed = line.trimmed();

        if (trimmed == QStringLiteral("$$")) {
            int close = -1;
            for (int j = i + 1; j < raw.size(); ++j) {
                if (raw.at(j).trimmed() == QStringLiteral("$$")) {
                    close = j;
                    break;
                }
            }
            if (close < 0) {
                // 不配对的 $$ 就是普通文字
                appendLine(line);
                continue;
            }
            QStringList latexLines;
            for (int j = i + 1; j < close; ++j) latexLines.append(raw.at(j));
            while (!latexLines.isEmpty() && latexLines.first().trimmed().isEmpty()) {
                latexLines.removeFirst();
            }
            while (!latexLines.isEmpty() && latexLines.last().trimmed().isEmpty()) {
                latexLines.removeLast();
            }
            const QString latex = latexLines.join(QLatin1Char('\n'));
            if (!latex.trimmed().isEmpty()) {
                flushText();
                QJsonObject block;
                block[QStringLiteral("type")] = QStringLiteral("formula");
                block[QStringLiteral("latex")] = latex;
                blocks.append(block);
            }
            i = close;
            continue;
        }

        QString imagePath;
        int imageWidth = 0;
        if (matchImageReference(trimmed, &imagePath, &imageWidth)) {
            ++(*imageTotal);
            QString resolved;
            QImage image;
            if (includeImages) {
                resolved = resolveImagePath(baseDir, imagePath);
                QFile file(resolved);
                if (file.open(QIODevice::ReadOnly)) {
                    image.loadFromData(file.readAll());
                    file.close();
                }
            }
            if (includeImages && !image.isNull()) {
                flushText();
                QJsonObject block;
                block[QStringLiteral("type")] = QStringLiteral("image");
                const QString name = QFileInfo(resolved).fileName();
                block[QStringLiteral("name")] = name;
                if (imageWidth > 0 && image.width() > 0) {
                    const qreal width = imageWidth;
                    const qreal height = width * image.height() / image.width();
                    block[QStringLiteral("width")] = width;
                    block[QStringLiteral("height")] = height;
                }
                block[QStringLiteral("data")] = imageToBase64Png(image);
                blocks.append(block);
            } else {
                if (includeImages) ++(*imageMissing);
                const QString shown = QFileInfo(imagePath).fileName();
                appendLine(QStringLiteral("[图片: %1]").arg(shown.isEmpty() ? imagePath : shown));
            }
            continue;
        }

        appendLine(line);
    }
    flushText();
    return blocks;
}

} // namespace

// ============================================================
// 导出
// ============================================================

MarkdownExportResult MindMapMarkdownExporter::toMarkdown(MindMapNode* root, bool includeImages) {
    MarkdownExportResult result;
    if (!root) return result;

    int collapsedCount = 0;
    int richCount = 0;
    int emptyCount = 0;
    int imageIndex = 0;
    QStringList lines;

    std::function<void(MindMapNode*, int)> emitNode = [&](MindMapNode* node, int depth) {
        QStringList content;
        for (MindMapBlock* block : node->blocks()) {
            if (!block) continue;

            if (block->type() == BlockType::Text) {
                auto* textBlock = static_cast<TextBlock*>(block);
                if (blockHasUnrepresentableFormat(textBlock)) ++richCount;
                QString markdown;
                if (textBlock->textItem()) {
                    markdown = inlineMarkdownFromDocument(textBlock->textItem()->document());
                }
                while (markdown.endsWith(QLatin1Char('\n'))) markdown.chop(1);
                const QStringList rawLines = markdown.split(QLatin1Char('\n'));
                for (const QString& rawLine : rawLines) {
                    content.append(escapeLeadingMarkers(rawLine));
                }
            } else if (block->type() == BlockType::Formula) {
                auto* formulaBlock = static_cast<FormulaBlock*>(block);
                const QString latex = formulaBlock->latexCode();
                if (latex.trimmed().isEmpty()) continue;
                content.append(QStringLiteral("$$"));
                const QStringList latexLines = latex.split(QLatin1Char('\n'));
                for (const QString& latexLine : latexLines) content.append(latexLine);
                content.append(QStringLiteral("$$"));
            } else {
                auto* imageBlock = static_cast<ImageBlock*>(block);
                ++result.imageCount;
                const QImage image = imageBlock->originalImage();
                if (includeImages && !image.isNull()) {
                    MarkdownImageAsset asset;
                    asset.fileName =
                        QStringLiteral("image_%1.png").arg(++imageIndex, 3, 10, QLatin1Char('0'));
                    asset.image = image;
                    asset.displaySize =
                        imageBlock->imageItem() ? imageBlock->imageItem()->imageSize() : QSizeF();
                    result.images.append(asset);
                    content.append(imageReferenceText(asset));
                } else {
                    content.append(QStringLiteral("[图片: %1]").arg(fallbackImageName(imageBlock)));
                }
            }
        }

        while (!content.isEmpty() && content.first().trimmed().isEmpty()) content.removeFirst();
        while (!content.isEmpty() && content.last().trimmed().isEmpty()) content.removeLast();
        if (content.isEmpty()) {
            ++emptyCount;
            content.append(kEmptyNodePlaceholder);
        }

        const QString indent(depth * 4, QLatin1Char(' '));
        // Obsidian 只认行首（可以缩进）的 $$，所以首行是公式时先写一个空列表项，
        // 让 $$ 从续行开始；写成 "- $$" 会被当成普通文字。
        if (content.first().trimmed() == QStringLiteral("$$")) {
            lines.append(indent + QStringLiteral("-"));
            for (const QString& line : content) {
                lines.append(indent + QStringLiteral("  ") + line);
            }
        } else {
            lines.append(indent + QStringLiteral("- ") + content.first());
            for (int i = 1; i < content.size(); ++i) {
                lines.append(indent + QStringLiteral("  ") + content.at(i));
            }
        }

        if (!node->isExpanded()) ++collapsedCount;
        for (MindMapNode* child : node->childNodes()) emitNode(child, depth + 1);
    };
    emitNode(root, 0);

    result.text = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');

    if (result.imageCount > 0 && !includeImages) {
        result.warnings.append(
            QStringLiteral("有 %1 张图片没有导出（正文里只留了“[图片: 文件名]”占位）。")
                .arg(result.imageCount));
    }
    if (emptyCount > 0) {
        result.warnings.append(QStringLiteral("有 %1 个空节点，已用占位文字“%2”导出。")
                                   .arg(emptyCount)
                                   .arg(kEmptyNodePlaceholder));
    }
    if (richCount > 0) {
        result.warnings.append(
            QStringLiteral("有 %1 个文本块带颜色、背景或对齐格式，导出后只保留粗体、斜体、删除线和链接。")
                .arg(richCount));
    }
    if (collapsedCount > 0) {
        result.warnings.append(QStringLiteral("有 %1 个折叠节点会按展开状态导出（Markdown 没有折叠概念）。")
                                   .arg(collapsedCount));
    }
    return result;
}

// ============================================================
// 导入
// ============================================================

MarkdownParseResult MindMapMarkdownImporter::parse(
    const QString& text, const QString& baseDir, bool includeImages,
    const std::function<bool(int, int)>& progress) {
    MarkdownParseResult result;

    if (text.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("文件是空的。");
        return result;
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        line.replace(QChar(0x00A0), QLatin1Char(' '));
    }

    // 跳过 YAML front matter
    int first = 0;
    while (first < lines.size() && lines.at(first).trimmed().isEmpty()) ++first;
    if (first < lines.size() && lines.at(first).trimmed() == QStringLiteral("---")) {
        for (int i = first + 1; i < lines.size(); ++i) {
            if (lines.at(i).trimmed() == QStringLiteral("---")) {
                first = i + 1;
                break;
            }
        }
    }

    std::vector<std::unique_ptr<MdItem>> owned;
    QVector<MdItem*> items;
    MdItem* current = nullptr;
    QStringList preambleLines;
    int headingCount = 0;
    int taskCount = 0;
    bool inFence = false;

    for (int i = first; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QStringLiteral("```"))) {
            inFence = !inFence;
            if (current) current->raw.append(line);
            continue;
        }
        if (inFence) {
            if (current) current->raw.append(line);
            continue;
        }
        if (trimmed.isEmpty()) {
            if (current) current->raw.append(QString());
            continue;
        }

        int indent = 0;
        QString content;
        bool isTask = false;
        bool taskDone = false;
        if (parseListMarker(line, &indent, &content, &isTask, &taskDone)) {
            auto item = std::make_unique<MdItem>();
            item->indent = indent;
            item->lineNumber = i + 1;
            if (isTask) {
                ++taskCount;
                item->raw.append((taskDone ? kCheckedPrefix : kUncheckedPrefix) + content);
            } else {
                item->raw.append(content);
            }
            current = item.get();
            items.append(current);
            owned.push_back(std::move(item));
            continue;
        }

        if (trimmed.startsWith(QLatin1Char('#'))) {
            ++headingCount;
            // 标题本身不建节点，但保留在样例行里：只有标题没有列表时，
            // 报错信息才能把文件开头的内容原样摊给用户看。
            if (!current) preambleLines.append(trimmed);
            continue;
        }

        if (current) {
            int contentStart = 0;
            leadingIndent(line, &contentStart);
            current->raw.append(line.mid(contentStart));
        } else {
            preambleLines.append(trimmed);
        }
    }

    if (items.isEmpty()) {
        result.errorMessage =
            QStringLiteral("文件里没有找到 Markdown 列表（每行以“- ”开头的层级大纲）。");
        QStringList sample;
        for (const QString& line : preambleLines) {
            sample.append(line);
            if (sample.size() >= 5) break;
        }
        if (!sample.isEmpty()) {
            result.errorMessage += QStringLiteral("\n\n文件开头的内容：\n") + sample.join(QLatin1Char('\n'));
        }
        return result;
    }

    // 缩进栈建树：缩进变大就是子节点，相等是兄弟，变小就回退到最近的更浅祖先
    QVector<MdItem*> stack;
    for (MdItem* item : items) {
        while (!stack.isEmpty() && stack.last()->indent >= item->indent) stack.removeLast();
        if (!stack.isEmpty()) {
            item->parent = stack.last();
            stack.last()->children.append(item);
        }
        stack.append(item);
    }

    QVector<MdItem*> topLevel;
    for (MdItem* item : items) {
        if (!item->parent) topLevel.append(item);
    }

    int imageTotal = 0;
    int imageMissing = 0;
    bool cancelled = false;
    int processed = 0;
    const int total = items.size();

    std::function<QJsonObject(MdItem*)> buildNode = [&](MdItem* item) -> QJsonObject {
        QJsonArray blocks =
            buildBlocks(item->raw, baseDir, includeImages, &imageTotal, &imageMissing);
        // 唯一内容就是占位文字的节点还原成真正的空节点
        if (blocks.size() == 1) {
            const QJsonObject only = blocks.at(0).toObject();
            if (only.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                QTextDocument doc;
                doc.setHtml(only.value(QStringLiteral("html")).toString());
                if (doc.toPlainText().trimmed() == kEmptyNodePlaceholder) blocks = QJsonArray();
            }
        }

        QJsonObject node;
        node[QStringLiteral("blocks")] = blocks;
        node[QStringLiteral("expanded")] = true;

        ++processed;
        if (progress && processed % 16 == 0 && !progress(processed, total)) {
            cancelled = true;
            return QJsonObject();
        }

        QJsonArray children;
        for (MdItem* child : item->children) {
            if (cancelled) break;
            children.append(buildNode(child));
        }
        node[QStringLiteral("children")] = children;
        return node;
    };

    QJsonObject rootJson;
    if (topLevel.size() == 1) {
        rootJson = buildNode(topLevel.first());
    } else {
        QJsonArray children;
        for (MdItem* item : topLevel) {
            if (cancelled) break;
            children.append(buildNode(item));
        }
        rootJson[QStringLiteral("blocks")] = QJsonArray();
        rootJson[QStringLiteral("expanded")] = true;
        rootJson[QStringLiteral("children")] = children;
        result.warnings.append(
            QStringLiteral("文件里有 %1 个顶级条目，已用一个空根节点把它们收在一起。")
                .arg(topLevel.size()));
    }

    if (cancelled) {
        result.errorMessage = QStringLiteral("已取消导入。");
        return result;
    }

    result.rootJson = rootJson;
    result.ok = true;
    result.imageTotal = imageTotal;
    result.imageMissing = imageMissing;

    if (taskCount > 0) {
        result.warnings.append(
            QStringLiteral("有 %1 个任务列表项按“☑ / ☐”文字导入（勾选状态无法在思维导图里保留）。")
                .arg(taskCount));
    }
    if (headingCount > 0) {
        result.warnings.append(
            QStringLiteral("已忽略 %1 行 Markdown 标题（目前只支持列表式大纲）。").arg(headingCount));
    }
    if (!preambleLines.isEmpty()) {
        result.warnings.append(
            QStringLiteral("已忽略列表前面 %1 行非列表文字。").arg(preambleLines.size()));
    }
    if (imageTotal > 0 && !includeImages) {
        result.warnings.append(
            QStringLiteral("按“仅保留文字”导入，%1 张图片没有嵌入，只留了文字占位。").arg(imageTotal));
    }
    if (imageMissing > 0) {
        result.warnings.append(
            QStringLiteral("有 %1 张图片找不到对应文件（相对 .md 所在目录查找），已降级成文字占位。")
                .arg(imageMissing));
    }
    return result;
}

bool MindMapMarkdownImporter::isMarkdownFilePath(const QString& filePath) {
    return filePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
           filePath.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive);
}

QString MindMapMarkdownImporter::fileFilter() {
    return QStringLiteral("Markdown 大纲 (*.md *.markdown);;所有文件 (*)");
}

QString MindMapMarkdownImporter::openFileFilter() {
    return QStringLiteral(
        "思维导图文件 (*.leap *.mmd *.mermaid *.md *.markdown);;"
        "leap 文件 (*.leap);;"
        "Mermaid 文件 (*.mmd *.mermaid);;"
        "Markdown 大纲 (*.md *.markdown);;"
        "所有文件 (*)");
}

bool MindMapMarkdownImporter::containsImageReference(const QString& text) {
    return countImageReferences(text) > 0;
}

int MindMapMarkdownImporter::countImageReferences(const QString& text) {
    static const QRegularExpression reference(
        QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)|!\\[\\[[^\\]]+\\]\\]"));
    int count = 0;
    auto it = reference.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}
