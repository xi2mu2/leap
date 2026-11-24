#include "MindMapMermaid.h"

#include "MindMapBlock.h"
#include "MindMapNode.h"

#include <QJsonArray>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QVector>
#include <functional>
#include <memory>
#include <vector>

namespace {

// 公式块要在主线程上逐条调用 JKQTMathText 渲染，导入别人给的 .mmd 时
// 公式过多会把界面卡住，所以超过这个数量就退回成纯文本。
constexpr int kMaxImportedFormulas = 200;

const QChar kBacktick(0x0060);

// 空标签的占位。Mermaid 的 mindmap 词法要求 [ ] 之间至少有一个
// NODE_DESCR，而 [""] 这种零长度字符串会让词法分析器在遇到 ] 时
// 直接报 “Expecting 'NODE_DESCR', got 'NODE_DEND'”，整个文件都解析
// 失败。所以空节点导出成 ["#8203;"]：这是 Mermaid 支持的十进制实体码，
// 渲染出来是 U+200B 零宽空格，视觉上仍是空的，但词法上非空。
// 注意不要对它调用 escapeQuotedLabel()，那会把 # 再转义成 #35; 而失效。
const QString kEmptyLabelEntity = QStringLiteral("#8203;");

// 零宽空格：导入时用来判断“标签其实什么都没有”
const QChar kZeroWidthSpace(0x200B);

struct TempNode {
    QString rawLabel;      // 未解码的标签（可能含 $$ 与 <br/>）
    bool markdown = false; // 标签是被反引号包裹的 markdown 字符串
    bool hasShape = false;
    QString shapeName;
    bool hasIcon = false;
    bool hasClass = false;
    std::vector<TempNode*> children;
};

struct ShapeDelim {
    const char* open;
    const char* close;
    const char* name;
};

// 取“最早出现的开括号”作为形状起点，因此只需要覆盖全部组合。
// 方框（[）不参与降级统计 —— 它对应本项目的默认矩形。
const ShapeDelim kShapeDelims[] = {
    {"((", "))", "圆形"},
    {"{{", "}}", "六边形"},
    {"))", "((", "Bang 爆炸形"},
    {"[", "]", "方框"},
    {"(", ")", "圆角"},
    {")", "(", "云"},
};

int leadingIndentWidth(const QString& line) {
    int width = 0;
    for (const QChar ch : line) {
        if (ch == QLatin1Char(' ')) {
            ++width;
        } else if (ch == QLatin1Char('\t')) {
            width += 4;   // 制表符按 4 空格计，Mermaid 编辑器也这么显示
        } else {
            break;
        }
    }
    return width;
}

bool isOtherDiagramKeyword(const QString& line) {
    static const char* kKeywords[] = {
        "flowchart", "graph", "sequenceDiagram", "classDiagram", "classDiagram-v2",
        "stateDiagram", "stateDiagram-v2", "erDiagram", "journey", "gantt", "pie",
        "gitGraph", "quadrantChart", "requirementDiagram", "timeline", "sankey-beta",
        "xychart-beta", "block-beta", "kanban", "radar-beta", "treemap-beta",
        "C4Context", "C4Container", "C4Component", "packet-beta", "architecture-beta",
    };
    const QString head = line.section(QLatin1Char(' '), 0, 0).trimmed();
    for (const char* keyword : kKeywords) {
        if (head == QLatin1String(keyword)) return true;
    }
    return false;
}

// 有些写法把 ::icon(...) / :::类名 单独写成一行，语义上属于上一行的节点。
// 返回 true 表示这行只是装饰，已被吸收到 target 上。
bool applyStandaloneDecoration(const QString& line, TempNode* target) {
    if (!target) return false;
    QString rest = line.trimmed();
    bool touched = false;
    for (;;) {
        if (rest.startsWith(QStringLiteral("::icon("))) {
            const int close = rest.indexOf(QLatin1Char(')'));
            if (close < 0) return false;
            target->hasIcon = true;
            rest = rest.mid(close + 1).trimmed();
            touched = true;
            continue;
        }
        if (rest.startsWith(QStringLiteral(":::"))) {
            const QString body = rest.mid(3);
            const int space = body.indexOf(QLatin1Char(' '));
            target->hasClass = true;
            rest = (space >= 0 ? body.mid(space + 1) : QString()).trimmed();
            touched = true;
            continue;
        }
        break;
    }
    return touched && rest.isEmpty();
}

// 解析一行节点：拆出缩进、形状、标签，并去掉 ::icon(...) / :::类名。
bool parseNodeLine(const QString& rawLine, TempNode* node, int* indentOut) {
    *indentOut = leadingIndentWidth(rawLine);
    QString line = rawLine.trimmed();
    if (line.isEmpty()) return false;

    // 1) 去掉尾部的装饰语法（本项目没有对应物，只记录以便提示）
    for (;;) {
        const int iconPos = line.indexOf(QStringLiteral("::icon("));
        const int classPos = line.indexOf(QStringLiteral(":::"));
        int cut = -1;
        if (iconPos >= 0) {
            node->hasIcon = true;
            cut = iconPos;
        }
        if (classPos >= 0 && (cut < 0 || classPos < cut)) {
            node->hasClass = true;
            cut = classPos;
        }
        if (cut < 0) break;
        line = line.left(cut).trimmed();
        if (line.isEmpty()) break;
    }
    if (line.isEmpty()) return false;

    // 2) 形状：标签取到与开括号配对的最后一个闭括号
    int bestPos = -1;
    const ShapeDelim* best = nullptr;
    for (const ShapeDelim& delim : kShapeDelims) {
        const int pos = line.indexOf(QLatin1String(delim.open));
        if (pos >= 0 && (bestPos < 0 || pos < bestPos)) {
            bestPos = pos;
            best = &delim;
        }
    }

    QString label;
    if (best) {
        const QString after = line.mid(bestPos + QLatin1String(best->open).size());
        const int closePos = after.lastIndexOf(QLatin1String(best->close));
        if (closePos >= 0) {
            label = after.left(closePos);
            node->hasShape = true;
            node->shapeName = QString::fromUtf8(best->name);
        } else {
            label = line;   // 括号不配对：整行当标签，尽量不丢内容
        }
    } else {
        label = line;
    }

    // 3) 引号 / 反引号包裹的标签
    label = label.trimmed();
    if (label.size() >= 2) {
        const QChar first = label.at(0);
        const QChar last = label.at(label.size() - 1);
        if (first == QLatin1Char('"') && last == QLatin1Char('"')) {
            label = label.mid(1, label.size() - 2);
        } else if (first == QLatin1Char('\'') && last == QLatin1Char('\'')) {
            label = label.mid(1, label.size() - 2);
        } else if (first == kBacktick && last == kBacktick) {
            label = label.mid(1, label.size() - 2);
            node->markdown = true;
        }
    }

    node->rawLabel = label;
    return true;
}

// <br/> 与常见 HTML 实体还原成普通文本
QString decodeEntities(const QString& input) {
    QString text = input;
    text.replace(QStringLiteral("<br/>"), QStringLiteral("\n"));
    text.replace(QStringLiteral("<br />"), QStringLiteral("\n"));
    text.replace(QStringLiteral("<br>"), QStringLiteral("\n"));
    text.replace(QStringLiteral("#34;"), QStringLiteral("\""));
    text.replace(QStringLiteral("#35;"), QStringLiteral("#"));
    text.replace(QStringLiteral("#8203;"), QString(kZeroWidthSpace));
    text.replace(QStringLiteral("&#8203;"), QString(kZeroWidthSpace));
    text.replace(QStringLiteral("#38;"), QStringLiteral("&"));
    text.replace(QStringLiteral("#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&nbsp;"), QString(QChar(0x00A0)));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

// 反引号 markdown 串 -> 富文本 HTML（只支持粗体、斜体、行内代码与换行）
QString markdownToHtml(const QString& text) {
    QString html;
    int i = 0;
    const int size = text.size();
    while (i < size) {
        if (text.mid(i, 2) == QLatin1String("**")) {
            const int end = text.indexOf(QStringLiteral("**"), i + 2);
            if (end > i + 2) {
                html += QStringLiteral("<b>") + text.mid(i + 2, end - i - 2).toHtmlEscaped() +
                        QStringLiteral("</b>");
                i = end + 2;
                continue;
            }
        }
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('*')) {
            const int end = text.indexOf(QLatin1Char('*'), i + 1);
            if (end > i + 1) {
                html += QStringLiteral("<i>") + text.mid(i + 1, end - i - 1).toHtmlEscaped() +
                        QStringLiteral("</i>");
                i = end + 1;
                continue;
            }
        }
        if (ch == kBacktick) {
            const int end = text.indexOf(kBacktick, i + 1);
            if (end > i + 1) {
                html += QStringLiteral("<code>") + text.mid(i + 1, end - i - 1).toHtmlEscaped() +
                        QStringLiteral("</code>");
                i = end + 1;
                continue;
            }
        }
        if (ch == QLatin1Char('\n')) {
            html += QStringLiteral("<br/>");
        } else {
            html += QString(ch).toHtmlEscaped();
        }
        ++i;
    }
    return html;
}

bool textBlockHasRichFormat(TextBlock* block) {
    QTextDocument* doc = block->textItem() ? block->textItem()->document() : nullptr;
    if (!doc) return false;
    for (QTextBlock paragraph = doc->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
        if (paragraph.blockFormat() != QTextBlockFormat()) return true;
        for (auto it = paragraph.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat() != QTextCharFormat()) return true;
        }
    }
    return false;
}

// 节点的全部内容压成一个 Mermaid 标签：文本块按顺序拼接，公式包成 $$...$$，
// 图片没有对应语法，只计数交给调用方提示。
QString nodeLabel(MindMapNode* node, int* imageCount, int* richCount, int* formulaCount) {
    QStringList pieces;
    for (MindMapBlock* block : node->blocks()) {
        switch (block->type()) {
        case BlockType::Text: {
            auto* textBlock = static_cast<TextBlock*>(block);
            if (textBlockHasRichFormat(textBlock)) ++(*richCount);
            QString blockText = textBlock->toPlainText();
            blockText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            blockText.replace(QChar(0x2028), QLatin1Char('\n'));
            blockText.replace(QChar(0x2029), QLatin1Char('\n'));
            if (blockText.trimmed().isEmpty()) break;
            pieces.append(blockText);
            break;
        }
        case BlockType::Formula: {
            const QString latex = static_cast<FormulaBlock*>(block)->latexCode();
            if (latex.trimmed().isEmpty()) break;
            pieces.append(QStringLiteral("$$") + latex + QStringLiteral("$$"));
            ++(*formulaCount);
            break;
        }
        case BlockType::Image:
            ++(*imageCount);
            break;
        }
    }
    return pieces.join(QLatin1Char('\n'));
}

bool labelNeedsQuoting(const QString& label) {
    if (label.isEmpty()) return true;
    if (label.startsWith(QLatin1Char(' ')) || label.endsWith(QLatin1Char(' '))) return true;
    if (label.contains(QLatin1Char('\n'))) return true;
    if (label.contains(QStringLiteral("[](){}\"")) || label.contains(kBacktick)) return true;
    if (label.contains(QStringLiteral("::"))) return true;
    return false;
}

QString escapeQuotedLabel(const QString& label) {
    QString text = label;
    text.replace(QLatin1Char('#'), QStringLiteral("#35;"));
    text.replace(QLatin1Char('"'), QStringLiteral("#34;"));
    text.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    return text;
}

} // namespace

MermaidParseResult MindMapMermaidImporter::parse(const QString& text) {
    MermaidParseResult result;
    QStringList warnings;

    QString source = text;
    if (source.startsWith(QChar(0xFEFF))) source.remove(0, 1);
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    std::vector<std::unique_ptr<TempNode>> pool;
    auto makeNode = [&pool]() -> TempNode* {
        pool.emplace_back(new TempNode());
        return pool.back().get();
    };

    struct Entry {
        int indent = 0;
        TempNode* node = nullptr;
    };
    QVector<Entry> stack;
    TempNode* root = nullptr;

    int shapeCount = 0;
    int iconCount = 0;
    int classCount = 0;
    int extraRootCount = 0;
    int badLineCount = 0;
    int firstBadLine = 0;
    int formulaCount = 0;
    int droppedFormulas = 0;

    TempNode* lastNode = nullptr;   // 上一行解析出的节点，用于吸收独立的装饰行
    bool sawKeyword = false;
    bool warnedMissingKeyword = false;
    bool skippingBlock = false;   // --- 前置配置块 / config 块

    const QStringList lines = source.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const QString& rawLine = lines.at(index);
        const int lineNo = index + 1;
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.startsWith(QStringLiteral("%%"))) continue;   // 注释与 %%{init}%%
        if (trimmed == QLatin1String("---")) {
            skippingBlock = !skippingBlock;
            continue;
        }
        if (skippingBlock) continue;

        if (!sawKeyword) {
            if (trimmed == QLatin1String("mindmap") || trimmed.startsWith(QStringLiteral("mindmap "))) {
                sawKeyword = true;
                continue;
            }
            if (isOtherDiagramKeyword(trimmed)) {
                result.ok = false;
                result.errorMessage =
                    QStringLiteral("这不是 Mermaid 的 mindmap 图（检测到 %1），目前只支持 mindmap 语法。")
                        .arg(trimmed.section(QLatin1Char(' '), 0, 0));
                return result;
            }
            // 宽松处理：没有 mindmap 关键字也按正文解析，但提示一下
            sawKeyword = true;
            if (!warnedMissingKeyword) {
                warnings.append(QStringLiteral("文件里没有找到 mindmap 关键字，已按思维导图正文解析。"));
                warnedMissingKeyword = true;
            }
        }

        // 独立成行的 ::icon(...) / :::类名 属于上一行的节点，不单独成节点
        if (lastNode && applyStandaloneDecoration(trimmed, lastNode)) continue;

        TempNode* node = makeNode();
        int indent = 0;
        if (!parseNodeLine(rawLine, node, &indent)) {
            if (badLineCount == 0) firstBadLine = lineNo;
            ++badLineCount;
            continue;
        }
        lastNode = node;

        if (!root) {
            root = node;
            stack.clear();
            stack.append(Entry{indent, node});
            continue;
        }

        // 缩进语义是“与上一行比较”，不规整缩进也要兼容：
        // 弹出所有不比当前行更浅的祖先，剩下的栈顶就是父节点。
        while (stack.size() > 1 && indent <= stack.last().indent) stack.removeLast();
        if (indent <= stack.first().indent) {
            // 与根节点同级或更浅：Mermaid 只允许一个根节点，挂到根下面
            root->children.push_back(node);
            ++extraRootCount;
            stack.resize(1);
            stack.append(Entry{stack.first().indent + 1, node});
        } else {
            stack.last().node->children.push_back(node);
            stack.append(Entry{indent, node});
        }
    }

    if (!root) {
        if (!warnings.isEmpty()) result.errorMessage = warnings.join(QLatin1Char('\n')) + QLatin1Char('\n');
        result.errorMessage += QStringLiteral("文件里没有可导入的思维导图节点。");
        return result;
    }

    std::function<QJsonObject(TempNode*)> buildNode = [&](TempNode* node) -> QJsonObject {
        QJsonObject object;
        QJsonArray blocks;

        const QStringList parts = node->rawLabel.split(QStringLiteral("$$"));
        const bool hasFormula = parts.size() > 1;
        const bool balanced = parts.size() % 2 == 1;
        if (hasFormula && !balanced) {
            warnings.append(QStringLiteral("有节点的标签里 $$ 不配对，已按普通文本处理。"));
        }

        auto appendText = [&](const QString& raw) {
            QString decoded = decodeEntities(raw);
            // 导出空节点时写的是零宽空格占位，这里再还原成“真正空”的节点
            decoded.remove(kZeroWidthSpace);
            if (decoded.trimmed().isEmpty()) return;
            QJsonObject block;
            block["type"] = QStringLiteral("text");
            if (node->markdown) {
                block["html"] = markdownToHtml(decoded);
            } else {
                block["text"] = decoded;
            }
            blocks.append(block);
        };

        if (hasFormula && !balanced) {
            appendText(node->rawLabel);
        } else {
            for (int i = 0; i < parts.size(); ++i) {
                const bool isFormula = hasFormula && (i % 2 == 1);
                if (!isFormula) {
                    appendText(parts.at(i));
                    continue;
                }
                const QString latex = parts.at(i).trimmed();
                if (latex.isEmpty()) continue;
                if (formulaCount < kMaxImportedFormulas) {
                    QJsonObject block;
                    block["type"] = QStringLiteral("formula");
                    block["latex"] = latex;
                    blocks.append(block);
                    ++formulaCount;
                } else {
                    QJsonObject block;
                    block["type"] = QStringLiteral("text");
                    block["text"] = QStringLiteral("$$") + latex + QStringLiteral("$$");
                    blocks.append(block);
                    ++droppedFormulas;
                }
            }
        }

        object["blocks"] = blocks;
        object["expanded"] = true;
        QJsonArray children;
        for (TempNode* child : node->children) children.append(buildNode(child));
        object["children"] = children;
        return object;
    };

    // 统计被降级的装饰语法（形状/图标/类名），统一给出提示
    std::function<void(TempNode*)> tally = [&](TempNode* node) {
        if (node->hasShape && node->shapeName != QStringLiteral("方框")) ++shapeCount;
        if (node->hasIcon) ++iconCount;
        if (node->hasClass) ++classCount;
        for (TempNode* child : node->children) tally(child);
    };
    tally(root);

    result.rootJson = buildNode(root);
    result.ok = true;

    if (iconCount > 0) {
        warnings.append(QStringLiteral("已忽略 %1 个节点的图标 ::icon(...)。").arg(iconCount));
    }
    if (classCount > 0) {
        warnings.append(QStringLiteral("已忽略 %1 个节点的样式类名 :::（逐节点配色无法保存）。").arg(classCount));
    }
    if (shapeCount > 0) {
        warnings.append(QStringLiteral("已忽略 %1 个节点的形状（圆角/圆形/六边形/Bang/云），只保留文字。")
                            .arg(shapeCount));
    }
    if (extraRootCount > 0) {
        warnings.append(
            QStringLiteral("检测到 %1 个与根节点同级的条目，已挂到根节点下（Mermaid 思维导图只允许一个根节点）。")
                .arg(extraRootCount));
    }
    if (droppedFormulas > 0) {
        warnings.append(QStringLiteral("公式数量超过 %1 个，多出的 %2 条已按普通文本导入。")
                            .arg(kMaxImportedFormulas).arg(droppedFormulas));
    }
    if (badLineCount > 0) {
        warnings.append(QStringLiteral("有 %1 行无法解析，已跳过（从第 %2 行开始）。")
                            .arg(badLineCount).arg(firstBadLine));
    }

    result.warnings = warnings;
    return result;
}

bool MindMapMermaidImporter::isMermaidFilePath(const QString& filePath) {
    return filePath.endsWith(QStringLiteral(".mmd"), Qt::CaseInsensitive) ||
           filePath.endsWith(QStringLiteral(".mermaid"), Qt::CaseInsensitive);
}

QString MindMapMermaidImporter::fileFilter() {
    return QStringLiteral("Mermaid 思维导图 (*.mmd *.mermaid);;所有文件 (*)");
}

QString MindMapMermaidImporter::openFileFilter() {
    return QStringLiteral(
        "思维导图文件 (*.leap *.mmd *.mermaid);;"
        "leap 文件 (*.leap);;"
        "Mermaid 文件 (*.mmd *.mermaid);;"
        "所有文件 (*)");
}

QString MindMapMermaidExporter::toText(MindMapNode* root, QStringList* warnings) {
    if (!root) return QString();

    int imageCount = 0;
    int richCount = 0;
    int collapsedCount = 0;
    int formulaCount = 0;
    int emptyLabelCount = 0;

    QStringList lines;
    lines.append(QStringLiteral("mindmap"));

    int nextId = 1;
    std::function<void(MindMapNode*, int)> emitNode = [&](MindMapNode* node, int depth) {
        QString label = nodeLabel(node, &imageCount, &richCount, &formulaCount);
        const QString indent(depth * 2, QLatin1Char(' '));
        const QString id = QStringLiteral("n%1").arg(nextId++);
        if (label.trimmed().isEmpty()) {
            // 空节点（新建后没填字、只有图片块、只有空公式）Mermaid 解析不了，
            // 用零宽空格实体占位；不能走 escapeQuotedLabel()，否则 # 会被转义。
            ++emptyLabelCount;
            lines.append(indent + id + QLatin1Char('[') + kEmptyLabelEntity + QLatin1Char(']'));
        } else if (labelNeedsQuoting(label)) {
            lines.append(indent + id + QStringLiteral("[\"") + escapeQuotedLabel(label) +
                         QStringLiteral("\"]"));
        } else {
            lines.append(indent + id + QLatin1Char('[') + label + QLatin1Char(']'));
        }
        if (!node->isExpanded()) ++collapsedCount;
        for (MindMapNode* child : node->childNodes()) emitNode(child, depth + 1);
    };
    emitNode(root, 0);

    if (warnings) {
        if (imageCount > 0) {
            warnings->append(QStringLiteral("有 %1 个图片块无法写入 Mermaid，已跳过。").arg(imageCount));
        }
        if (formulaCount > 0) {
            warnings->append(QStringLiteral("有 %1 个公式按 $$...$$ 文本导出，Mermaid 本身不会渲染公式。")
                                 .arg(formulaCount));
        }
        if (richCount > 0) {
            warnings->append(QStringLiteral("有 %1 个文本块带粗体/斜体/颜色/对齐等格式，导出后只剩纯文本。")
                                 .arg(richCount));
        }
        if (emptyLabelCount > 0) {
            warnings->append(QStringLiteral("有 %1 个空节点，已用空白占位符导出（Mermaid 不接受空节点）。")
                                 .arg(emptyLabelCount));
        }
        if (collapsedCount > 0) {
            warnings->append(QStringLiteral("有 %1 个折叠节点会按展开状态导出（Mermaid 没有折叠概念）。")
                                 .arg(collapsedCount));
        }
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
