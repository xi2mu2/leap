#include "MindMapNode.h"
#include "MindMapNodeGeometry.h"
#include "MindMapImageItem.h"
#include "MindMapIO.h"
#include "MindMapFormulaDialog.h"
#include "MindMapCommands.h"
#include "MindMapView.h"
#include "MindMapHoverTracker.h"
#include "PerformanceMetrics.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextBlock>
#include <QApplication>
#include <QGraphicsView>
#include <QLineF>
#include <QObject>
#include <QMenu>
#include <QTextOption>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QFontMetricsF>
#include <QSettings>
#include <algorithm>

// ==================== 静态成员 ====================
QUndoStack* MindMapNode::s_undoStack = nullptr;
QJsonObject MindMapNode::s_clipboardData;
bool MindMapNode::s_hasClipboard = false;
bool MindMapNode::s_darkMode = false;

// ==================== 全局辅助 ====================

MindMapNode* MindMapNode::findEditingNode(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    for (auto* item : scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isEditing()) return node;
        }
    }
    return nullptr;
}

// 【新增】辅助函数：将相邻的文本块合并为一个
// 策略：只合并连续的文本块，非文本块作为分隔
// 例如: {text1}{text2}{image}{text3} -> {text1+text2}{image}{text3}
QString MindMapNode::mergeHtmlBlocks(const QVector<TextBlock*>& blocks) {
    if (blocks.isEmpty()) return "";
    if (blocks.size() == 1) return blocks[0]->toHtml();

    // 用逐段落复制代替 HTML 往返，保证段落级格式（对齐/行距/缩进）不丢失
    QTextDocument mergedDoc;
    mergedDoc.setDefaultFont(blocks.first()->textItem()->document()->defaultFont());
    QTextCursor cursor(&mergedDoc);
    for (int i = 0; i < blocks.size(); ++i) {
        TextBlock::copyParagraphs(cursor, blocks[i]->textItem()->document(), i == 0);
    }
    return mergedDoc.toHtml();
}

// 【新增】进入编辑时：只合并相邻的文本块
// 例如: {text1}{text2}{image}{text3} -> {text1+text2}{image}{text3}
// 返回包含原始 block 所在组合并后的 TextBlock 指针
TextBlock* MindMapNode::mergeTextBlocksForEditing(TextBlock* originalBlock) {
    // 按相邻文本块分组，每组分别合并
    // 遍历 m_blocks，找到所有连续的文本块区间
    struct TextGroup {
        int startIndex;      // 在 m_blocks 中的起始索引
        int count;           // 连续文本块的数量
        QVector<TextBlock*> blocks;
    };
    QVector<TextGroup> groups;

    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i]->type() == BlockType::Text) {
            if (groups.isEmpty() || groups.last().startIndex + groups.last().count != i) {
                // 开始一个新的连续文本块组
                TextGroup group;
                group.startIndex = i;
                group.count = 1;
                group.blocks.append(static_cast<TextBlock*>(m_blocks[i]));
                groups.append(group);
            } else {
                // 延续上一个连续组
                groups.last().count++;
                groups.last().blocks.append(static_cast<TextBlock*>(m_blocks[i]));
            }
        }
    }

    TextBlock* resultBlock = nullptr;

    // 从后往前处理每组，避免索引变化问题
    for (int g = groups.size() - 1; g >= 0; --g) {
        const TextGroup& group = groups[g];
        if (group.blocks.size() <= 1) {
            // 单个文本块不需要合并，检查是否是原始块所在的组
            if (group.blocks.contains(originalBlock)) {
                resultBlock = group.blocks.first();
            }
            continue;
        }

        // 合并这组相邻文本块
        QString mergedHtml = mergeHtmlBlocks(group.blocks);
        TextBlock* mergedBlock = new TextBlock(this);
        mergedBlock->setHtml(mergedHtml);

        // 检查原始块是否在这组中
        if (group.blocks.contains(originalBlock)) {
            resultBlock = mergedBlock;
        }

        // 删除这组中的所有旧文本块
        for (auto* tb : group.blocks) {
            if (QGraphicsItem* gi = tb->graphicsItem()) {
                m_childOldZValues.remove(gi);
            }
            if (m_selectedBlock == tb) m_selectedBlock = nullptr;
            if (m_editingTextBlock == tb) m_editingTextBlock = nullptr;
            delete tb;
        }

        // 在组的起始位置插入合并后的块
        m_blocks.insert(group.startIndex, mergedBlock);
        // 删除原来组中剩余的块（已经 delete 了，这里只是从列表移除）
        for (int i = 0; i < group.count; ++i) {
            m_blocks.removeAt(group.startIndex + 1);
        }
    }

    calcNodeSize();
    updateBlockLayout();
    return resultBlock;
}

// 【新增】退出编辑时：按段落拆分每个合并后的文本块
// 注意：每个之前被合并的相邻文本块组现在是一个单独的文本块，按段落拆分它
void MindMapNode::splitTextBlocksAfterEditing() {
    // 从后往前处理，避免索引变化
    for (int i = m_blocks.size() - 1; i >= 0; --i) {
        if (m_blocks[i]->type() != BlockType::Text) continue;

        TextBlock* textBlock = static_cast<TextBlock*>(m_blocks[i]);
        QTextDocument* doc = textBlock->textItem()->document();
        int paragraphCount = doc->blockCount();
        if (paragraphCount <= 1) continue;  // 只有一个段落，不需要拆分

        // 收集光标位置信息
        int cursorPos = textBlock->cursorPosition();
        int targetBlockIndex = 0;
        int targetBlockPos = 0;

        // 按段落拆分
        QVector<TextBlock*> newBlocks;
        for (int p = 0; p < paragraphCount; ++p) {
            QTextBlock para = doc->findBlockByNumber(p);
            int blockStart = para.position();
            int blockEnd = blockStart + para.length() - 1;  // 去掉段落结尾的换行符

            // 提取该段落的 HTML
            QTextCursor extractCursor(doc);
            extractCursor.setPosition(blockStart);
            extractCursor.setPosition(blockEnd, QTextCursor::KeepAnchor);
            QString paraHtml = extractCursor.selection().toHtml();

            // 如果段落为空，创建空文本块
            if (paraHtml.isEmpty() || para.text().isEmpty()) {
                paraHtml = "<p></p>";
            }

            TextBlock* newBlock = new TextBlock(this);
            newBlock->setHtml(paraHtml);
            newBlocks.append(newBlock);

            // 计算光标应该落在哪个新块中
            if (cursorPos >= blockStart && cursorPos <= blockEnd) {
                targetBlockIndex = p;
                targetBlockPos = cursorPos - blockStart;
            }
        }

        // 从 z-value 备份中移除旧块的 item
        if (QGraphicsItem* gi = textBlock->graphicsItem()) {
            m_childOldZValues.remove(gi);
        }
        if (m_selectedBlock == textBlock) m_selectedBlock = nullptr;
        if (m_editingTextBlock == textBlock) m_editingTextBlock = nullptr;
        delete textBlock;

        // 在原位置插入拆分后的块
        m_blocks.removeAt(i);
        for (int j = newBlocks.size() - 1; j >= 0; --j) {
            m_blocks.insert(i, newBlocks[j]);
        }
    }

    calcNodeSize();
    updateBlockLayout();
}

QVector<MindMapBlock*> MindMapNode::splitTextBlocksForMediaDrag() {
    QVector<MindMapBlock*> original = m_blocks;
    m_mediaDragTransientBlocks.clear();
    for (int i = m_blocks.size() - 1; i >= 0; --i) {
        if (m_blocks[i]->type() != BlockType::Text) continue;
        auto* textBlock = static_cast<TextBlock*>(m_blocks[i]);
        QTextDocument* doc = textBlock->textItem()->document();
        if (doc->blockCount() <= 1) continue;
        QVector<TextBlock*> paragraphs;
        for (int p = 0; p < doc->blockCount(); ++p) {
            const QTextBlock para = doc->findBlockByNumber(p);
            auto* part = new TextBlock(this);
            QTextDocument* partDoc = part->textItem()->document();
            // 继承原文档的默认字体与默认字色：没有显式字符格式的文字
            // （编辑时新敲入的内容）否则会回落到应用默认字体
            partDoc->setDefaultFont(doc->defaultFont());
            part->textItem()->setDefaultTextColor(textBlock->textItem()->defaultTextColor());
            // 逐段落复制（含 QTextBlockFormat），不再用 selection().toHtml()
            QTextCursor dst(partDoc);
            dst.movePosition(QTextCursor::Start);
            TextBlock::copyParagraph(dst, para, doc, true);
            paragraphs.append(part);
            m_mediaDragTransientBlocks.append(part);
        }
        m_blocks.removeAt(i);
        textBlock->textItem()->setVisible(false);
        for (TextBlock* part : paragraphs) part->textItem()->setVisible(true);
        for (int p = paragraphs.size() - 1; p >= 0; --p) m_blocks.insert(i, paragraphs[p]);
    }
    invalidateSerializationCache();
    calcNodeSize();
    updateBlockLayout();
    return original;
}

QVector<MindMapBlock*> MindMapNode::takeMediaDragTransientBlocks() {
    QVector<MindMapBlock*> blocks = m_mediaDragTransientBlocks;
    m_mediaDragTransientBlocks.clear();
    return blocks;
}

void MindMapNode::mergeAdjacentTextBlocksForMediaDrag() {
    for (int i = m_blocks.size() - 1; i > 0; --i) {
        if (m_blocks[i]->type() != BlockType::Text || m_blocks[i - 1]->type() != BlockType::Text)
            continue;
        auto* previous = static_cast<TextBlock*>(m_blocks[i - 1]);
        auto* current = static_cast<TextBlock*>(m_blocks[i]);
        // 就地把 current 的段落追加到 previous 的文档里：
        // 保留 previous 的文档（默认字体/默认字色），并逐段复制段落格式，
        // 避免 mergeHtmlBlocks 走 HTML 时丢掉 align / line-height。
        QTextCursor cursor(previous->textItem()->document());
        cursor.movePosition(QTextCursor::End);
        TextBlock::copyParagraphs(cursor, current->textItem()->document(), false);
        if (m_selectedBlock == current) m_selectedBlock = previous;
        if (m_editingTextBlock == current) m_editingTextBlock = previous;
        m_childOldZValues.remove(current->graphicsItem());
        m_blocks.removeAt(i);
        current->textItem()->setVisible(false);
        // Keep detached blocks alive so the undo command can restore the exact objects.
    }
    invalidateSerializationCache();
    calcNodeSize();
    updateBlockLayout();
}

void MindMapNode::restoreBlockSequence(const QVector<MindMapBlock*>& blocks) {
    m_blocks = blocks;
    for (MindMapBlock* block : m_blocks) {
        block->setParentNode(this);
        if (QGraphicsItem* item = block->graphicsItem()) {
            if ((block->type() == BlockType::Image || block->type() == BlockType::Formula) &&
                item->parentItem() != this) {
                item->setParentItem(this);
            }
            item->setVisible(true);
        }
    }
    invalidateSerializationCache();
    calcNodeSize();
    updateBlockLayout();
}

static void notifyViewButtonState(MindMapNode* node) {
    if (!node || !node->scene()) return;
    if (!node->scene()->views().isEmpty()) {
        if (auto* view = dynamic_cast<MindMapView*>(node->scene()->views().first())) {
            view->updateInsertImageButtonState();
        }
    }
}

static void collectNodes(MindMapNode* node, QVector<MindMapNode*>& out) {
    if (!node) return;
    out.append(node);
    if (node->isExpanded()) {
        for (auto* child : node->childNodes()) {
            collectNodes(child, out);
        }
    }
}

static bool compareNodeX(MindMapNode* a, MindMapNode* b) {
    return a->x() < b->x();
}

// ==================== 构造 / 析构 ====================

MindMapNode::MindMapNode(const QString& text, MindMapNode* parent)
    : QGraphicsRectItem(nullptr), m_parent(parent), m_editingTextBlock(nullptr) {
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsFocusable);

    // 创建初始文本块
    TextBlock* textBlock = new TextBlock(this);
    textBlock->setPlainText(text);
    m_blocks.append(textBlock);

    calcNodeSize();

    if (m_parent) {
        m_parent->addChild(this);
    }
    applyStyle(styleForCurrentLevel(), false);
}

MindMapNode::~MindMapNode() {
    if (m_hoverOwner) {
        MindMapHoverTracker* hoverOwner = m_hoverOwner;
        m_hoverOwner = nullptr;
        hoverOwner->forgetNode(this);
    }

    if (m_parent) {
        m_parent->removeChild(this);
    }

    // 【修复】清理所有 Block 之前，先解除 Image/Formula item 的 Qt 父子关系
    // 避免 block 析构时和 MindMapNode 析构时的 double-free
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(block);
            if (ib->imageItem()) {
                ib->imageItem()->setParentItem(nullptr);
            }
        } else if (block->type() == BlockType::Formula) {
            auto* fb = static_cast<FormulaBlock*>(block);
            if (fb->pixmapItem()) {
                fb->pixmapItem()->setParentItem(nullptr);
            }
        }
    }

    // 清理所有 Block
    for (auto* block : m_blocks) {
        delete block;
    }
    m_blocks.clear();

    for (auto* edge : m_edgeItems) {
        if (edge && edge->scene()) edge->scene()->removeItem(edge);
        delete edge;
    }
    m_edgeItems.clear();

    QVector<MindMapNode*> childrenCopy = m_children;
    m_children.clear();
    for (auto* child : childrenCopy) {
        child->m_parent = nullptr;
        delete child;
    }
}

// ==================== boundingRect 扩展 ====================

QRectF MindMapNode::boundingRect() const {
    return MindMapNodeGeometry::boundingRect(rect(), !m_children.isEmpty(),
                                             HIGHLIGHT_MARGIN, 3.0,
                                             H_GAP / 2, INDICATOR_HIT_RADIUS);
}

QPainterPath MindMapNode::shape() const {
    QPainterPath path;
    if (m_shape == MindMapNodeShape::Rect) path.addRect(rect());
    else path.addRoundedRect(rect(), CORNER_RADIUS, CORNER_RADIUS);

    // QGraphicsScene::items(point) uses shape() for hit testing.  The
    // expand/collapse indicator is painted just outside the node rectangle,
    // so include its clickable area here as well as in boundingRect().
    if (!m_children.isEmpty()) {
        const QPointF center(rect().width() + H_GAP / 2, rect().height() / 2);
        path.addEllipse(center, INDICATOR_HIT_RADIUS, INDICATOR_HIT_RADIUS);
    }
    return path;
}

MindMapNode* MindMapNode::styleRoot() const {
    const MindMapNode* node = this;
    while (node->m_parent) node = node->m_parent;
    return const_cast<MindMapNode*>(node);
}

int MindMapNode::styleLevel() const {
    int depth = 0;
    for (const MindMapNode* node = m_parent; node; node = node->m_parent) ++depth;
    return depth == 0 ? 0 : (depth == 1 ? 1 : 2);
}

MindMapStyle MindMapNode::styleForCurrentLevel() const {
    const MindMapLevelStyles& styles = styleRoot()->m_levelStyles;
    return styleLevel() == 0 ? styles.root : (styleLevel() == 1 ? styles.firstChild : styles.deeper);
}

const MindMapLevelStyles& MindMapNode::levelStyles() const { return styleRoot()->m_levelStyles; }
MindMapLevelStyles& MindMapNode::levelStyles() {
    MindMapNode* root = styleRoot();
    root->invalidateSerializationCache();
    return root->m_levelStyles;
}
void MindMapNode::setLevelStyles(const MindMapLevelStyles& styles) {
    MindMapNode* root = styleRoot();
    root->m_levelStyles = styles;
    root->invalidateSerializationCache();
}

void MindMapNode::invalidateSerializationCache() {
    m_serializationCache = QJsonObject();
    m_hasSerializationCache = false;
}

void MindMapNode::setSerializationCache(const QJsonObject& cache) {
    m_serializationCache = cache;
    m_hasSerializationCache = true;
}

void MindMapNode::applyStyle(const MindMapStyle& style, bool updateText, bool respectExplicitTextFormats) {
    m_fillColor = style.fillColor;
    m_borderColor = style.effectiveBorderColor();
    m_borderWidth = style.borderWidth;
    m_textColor = style.textColor;
    m_shape = style.shape;
    if (updateText) {
        // 打开文件时不要用样式默认值覆盖文件里显式保存的段落/字符格式
        const bool preserveExplicit = respectExplicitTextFormats || MindMapIO::isLoading();
        for (auto* block : m_blocks) {
            if (block->type() != BlockType::Text) continue;
            auto* textBlock = static_cast<TextBlock*>(block);
            textBlock->textItem()->setFont(style.font());
            textBlock->textItem()->setDefaultTextColor(style.textColor);
            QTextDocument* document = textBlock->textItem()->document();
            if (!preserveExplicit) {
                // Character formats embedded in HTML override item defaults.
                QTextCharFormat charFormat;
                charFormat.setFontFamilies({style.font().family()});
                charFormat.setFontPointSize(style.font().pointSizeF());
                charFormat.setForeground(style.textColor);
                QTextCursor textCursor(document);
                textCursor.select(QTextCursor::Document);
                textCursor.mergeCharFormat(charFormat);
            }
            for (QTextBlock paragraph = document->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
                QTextBlockFormat format = paragraph.blockFormat();
                // 打开文件时保留文档里显式存下来的对齐/行距，别用主题默认值覆盖
                // （主题切换、层级变化等其它路径仍然强制套用样式）。
                if (preserveExplicit &&
                    (format.hasProperty(QTextFormat::BlockAlignment) ||
                     format.hasProperty(QTextFormat::LineHeight))) {
                    continue;
                }
                format.setAlignment(style.alignment);
                format.setLineHeight(style.lineSpacing * 100.0, QTextBlockFormat::ProportionalHeight);
                QTextCursor cursor(paragraph);
                cursor.setBlockFormat(format);
            }
        }
    }
    calcNodeSize();
    update();
}

void MindMapNode::applyLevelStyles(const MindMapLevelStyles& styles) {
    MindMapNode* root = styleRoot();
    root->m_levelStyles = styles;
    root->invalidateSerializationCache();
    std::function<void(MindMapNode*)> visit = [&](MindMapNode* node) {
        if (!node) return;
        node->applyStyle(node->styleForCurrentLevel());
        for (auto* child : node->m_children) visit(child);
    };
    visit(root);
}

// ==================== 基础操作 ====================

void MindMapNode::addChild(MindMapNode* child) {
    if (!m_children.contains(child)) {
        prepareGeometryChange();
        m_children.append(child);
        child->m_parent = this;
        // The child may already have descendants when it is attached (for
        // example during file loading). Refresh the whole subtree now that
        // every node has its real parent and level.
        std::function<void(MindMapNode*)> refreshStyles =
            [&](MindMapNode* node) {
                if (!node) return;
                node->applyStyle(node->styleForCurrentLevel());
                for (auto* descendant : node->m_children) {
                    refreshStyles(descendant);
                }
            };
        refreshStyles(child);
        // 【修复】如果父节点当前处于折叠状态，新子节点应同步隐藏
        if (!m_expanded) {
            child->setVisible(false);
            child->setSubtreeVisible(false);
        }
    }
}
void MindMapNode::insertChild(int index, MindMapNode* child) {
    if (!child || m_children.contains(child)) return;
    prepareGeometryChange();
    if (index < 0) index = 0;
    if (index > m_children.size()) index = m_children.size();
    m_children.insert(index, child);
    child->m_parent = this;
    child->applyStyle(child->styleForCurrentLevel());
}

void MindMapNode::removeChild(MindMapNode* child) {
    prepareGeometryChange();
    m_children.removeAll(child);
    if (child && child->m_parent == this) {
        child->m_parent = nullptr;
    }
}

// ==================== Block 管理 ====================

void MindMapNode::addBlock(MindMapBlock* block, int index) {
    if (!block) return;
    if (m_blocks.contains(block)) return;

    prepareGeometryChange();
    block->setParentNode(this);

    if (index < 0 || index > m_blocks.size()) {
        m_blocks.append(block);
    } else {
        m_blocks.insert(index, block);
    }
    invalidateSerializationCache();
    calcNodeSize();
}

void MindMapNode::removeBlock(MindMapBlock* block) {
    if (!block) return;
    int idx = m_blocks.indexOf(block);
    if (idx >= 0) {
        removeBlockAt(idx);
    }
}

void MindMapNode::clearBlockSelectionAndUpdate() {
    if (m_selectedBlock) {
        clearBlockSelection();
        if (scene()) scene()->update();
    }
}

void MindMapNode::clearBlocks() {
    invalidateSerializationCache();
    // 从后往前删除，避免索引变化
    for (int i = m_blocks.size() - 1; i >= 0; --i) {
        auto* block = m_blocks[i];
        // 【修复】清理 z-value 备份中的悬空指针
        if (QGraphicsItem* gi = block->graphicsItem()) {
            m_childOldZValues.remove(gi);
        }
        // 【修复】解除 Image/Formula item 的 Qt 父子关系，让 block 析构时安全 delete item
        if (block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(block);
            if (ib->imageItem()) {
                ib->imageItem()->setParentItem(nullptr);
            }
        } else if (block->type() == BlockType::Formula) {
            auto* fb = static_cast<FormulaBlock*>(block);
            if (fb->pixmapItem()) {
                fb->pixmapItem()->setParentItem(nullptr);
            }
        }
        m_blocks.removeAt(i);
        delete block;
    }
    m_selectedBlock = nullptr;
    m_editingTextBlock = nullptr;
    m_isEditing = false;
    calcNodeSize();
}

void MindMapNode::removeBlockAt(int index) {
    if (index < 0 || index >= m_blocks.size()) return;

    invalidateSerializationCache();

    prepareGeometryChange();
    auto* block = m_blocks[index];

    // 【修复】从 z-value 备份中移除该 block 的 graphicsItem，防止后续恢复时访问悬空指针
    if (QGraphicsItem* gi = block->graphicsItem()) {
        m_childOldZValues.remove(gi);
    }

    // 【修复】解除 Image/Formula item 的 Qt 父子关系，让 block 析构时安全 delete item
    if (block->type() == BlockType::Image) {
        auto* ib = static_cast<ImageBlock*>(block);
        if (ib->imageItem()) {
            ib->imageItem()->setParentItem(nullptr);
        }
    } else if (block->type() == BlockType::Formula) {
        auto* fb = static_cast<FormulaBlock*>(block);
        if (fb->pixmapItem()) {
            fb->pixmapItem()->setParentItem(nullptr);
        }
    }

    if (m_selectedBlock == block) {
        m_selectedBlock = nullptr;
    }
    if (block->type() == BlockType::Text) {
        auto* tb = static_cast<TextBlock*>(block);
        if (m_editingTextBlock == tb) {
            m_editingTextBlock = nullptr;
            m_isEditing = false;
        }
    }

    m_blocks.removeAt(index);
    delete block;

    // 合并相邻的空文本块
    mergeEmptyTextBlocks();
    calcNodeSize();
    MindMapNode* root = getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (scene()) scene()->update();
}

// 【新增】takeBlockAt：从节点中移除 block 但不删除，返回 block 指针
MindMapBlock* MindMapNode::takeBlockAt(int index) {
    if (index < 0 || index >= m_blocks.size()) return nullptr;

    invalidateSerializationCache();

    prepareGeometryChange();
    auto* block = m_blocks[index];

    // 从 z-value 备份中移除
    if (QGraphicsItem* gi = block->graphicsItem()) {
        m_childOldZValues.remove(gi);
    }

    if (m_selectedBlock == block) {
        m_selectedBlock = nullptr;
    }
    if (block->type() == BlockType::Text) {
        auto* tb = static_cast<TextBlock*>(block);
        if (m_editingTextBlock == tb) {
            m_editingTextBlock = nullptr;
            m_isEditing = false;
        }
    }

    m_blocks.removeAt(index);
    calcNodeSize();
    // 注意：不触发重布局，不合并空文本块，不 delete block
    // 调用者负责后续处理
    return block;
}

void MindMapNode::moveBlock(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_blocks.size()) return;
    if (toIndex < 0 || toIndex > m_blocks.size()) return;
    if (fromIndex == toIndex) return;

    invalidateSerializationCache();

    prepareGeometryChange();
    auto* block = m_blocks.takeAt(fromIndex);
    // 调整目标索引（如果目标在移除后）
    if (toIndex > fromIndex) toIndex--;
    m_blocks.insert(toIndex, block);
    calcNodeSize();
    if (scene()) scene()->update();
}

void MindMapNode::moveBlock(MindMapBlock* block, int newIndex) {
    int idx = blockIndex(block);
    if (idx >= 0) {
        moveBlock(idx, newIndex);
    }
}

int MindMapNode::blockIndex(MindMapBlock* block) const {
    return m_blocks.indexOf(block);
}

void MindMapNode::setSelectedBlock(MindMapBlock* block) {
    if (m_selectedBlock && !m_blocks.contains(m_selectedBlock)) {
        m_selectedBlock = nullptr;
    }
    if (block && !m_blocks.contains(block)) {
        block = nullptr;
    }
    // 清理所有旧的块选中状态。图形项自身还可能保留绘制选中标记，不能只依赖指针。
    for (auto* current : m_blocks) {
        if (current != block && current->isBlockSelected()) {
            current->setBlockSelected(false);
        }
    }
    m_selectedBlock = block;
    if (block) {
        block->setBlockSelected(true);
    }
}

void MindMapNode::clearBlockSelection() {
    // 清理所有块的绘制选中状态，避免 m_selectedBlock 指针与图形项状态不同步。
    for (auto* block : m_blocks) {
        if (block && block->isBlockSelected()) {
            block->setBlockSelected(false);
        }
    }
    m_selectedBlock = nullptr;
}

void MindMapNode::insertBlockAfter(MindMapBlock* refBlock, MindMapBlock* newBlock) {
    int idx = blockIndex(refBlock);
    if (idx >= 0) {
        addBlock(newBlock, idx + 1);
    } else {
        addBlock(newBlock);
    }
}

void MindMapNode::insertBlockBefore(MindMapBlock* refBlock, MindMapBlock* newBlock) {
    int idx = blockIndex(refBlock);
    if (idx >= 0) {
        addBlock(newBlock, idx);
    } else {
        addBlock(newBlock);
    }
}

void MindMapNode::splitTextBlockAtCursor(TextBlock* block) {
    if (!block || !block->isEditing()) return;

    // Shift+Enter 应创建新的内容块，便于图片/公式在段落之间移动。
    // 只修改并保留当前 TextBlock，不合并或销毁当前 QGraphicsTextItem，
    // 避免在当前键盘事件尚未返回时产生悬空对象。
    const int index = blockIndex(block);
    if (index < 0) return;

    // 拆分点直接用文档坐标，兼容“一个文本块内含多个段落”的情况；
    // 不能用 toPlainText() 的下标（文档坐标把块分隔符也算进去）。
    const int cursorPosition = block->cursorPosition();

    if (s_undoStack && scene()) {
        s_undoStack->push(new SplitTextBlockCommand(
            this, block, index, cursorPosition, scene()));
    } else {
        auto* newBlock = block->splitAtCursorPreservingFormats();
        if (!newBlock) return;
        addBlock(newBlock, index + 1);
        startEditBlock(newBlock);
    }
}

bool MindMapNode::mergeTextBlockWithPrevious(TextBlock* block) {
    if (!block || !block->isEditing()) return false;
    const int index = blockIndex(block);
    if (index <= 0 || m_blocks[index - 1]->type() != BlockType::Text) return false;
    if (s_undoStack && scene()) {
        s_undoStack->push(new MergeTextBlockCommand(this, index,
                                                     MergeTextBlockCommand::WithPrevious, scene()));
    } else {
        auto* previous = static_cast<TextBlock*>(m_blocks[index - 1]);
        const int cursorPosition = previous->toPlainText().length();
        previous->mergeWith(block);
        removeBlockAt(index);
        startEditBlock(previous);
        previous->setCursorPosition(cursorPosition);
    }
    return true;
}

bool MindMapNode::mergeTextBlockWithNext(TextBlock* block) {
    if (!block || !block->isEditing()) return false;
    const int index = blockIndex(block);
    if (index < 0 || index + 1 >= m_blocks.size() || m_blocks[index + 1]->type() != BlockType::Text) return false;
    if (s_undoStack && scene()) {
        s_undoStack->push(new MergeTextBlockCommand(this, index,
                                                     MergeTextBlockCommand::WithNext, scene()));
    } else {
        auto* next = static_cast<TextBlock*>(m_blocks[index + 1]);
        const int cursorPosition = block->toPlainText().length();
        block->mergeWith(next);
        removeBlockAt(index + 1);
        startEditBlock(block);
        block->setCursorPosition(cursorPosition);
    }
    return true;
}

void MindMapNode::mergeEmptyTextBlocks() {
    for (int i = m_blocks.size() - 2; i >= 0; --i) {
        auto* block1 = m_blocks[i];
        auto* block2 = m_blocks[i + 1];

        if (block1->type() == BlockType::Text && block2->type() == BlockType::Text) {
            auto* tb1 = static_cast<TextBlock*>(block1);
            auto* tb2 = static_cast<TextBlock*>(block2);

            if (tb1->isEmpty() && tb2->isEmpty()) {
                // 两个都空，删除第二个
                removeBlockAt(i + 1);
            } else if (tb2->isEmpty()) {
                // 第二个空，删除它
                removeBlockAt(i + 1);
            } else if (tb1->isEmpty()) {
                // 第一个空，删除它
                removeBlockAt(i);
            }
        }
    }
}

// ==================== 文本内容 API ====================

QString MindMapNode::allText() const {
    QStringList texts;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            texts.append(static_cast<TextBlock*>(block)->toPlainText());
        }
    }
    return texts.join("\n");
}

QString MindMapNode::allHtml() const {
    QStringList htmls;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            htmls.append(static_cast<TextBlock*>(block)->toHtml());
        }
    }
    return htmls.join("<!--BLOCK-->");
}

void MindMapNode::setAllText(const QString& text) {
    invalidateSerializationCache();
    // 清除所有块
    for (auto* block : m_blocks) {
        delete block;
    }
    m_blocks.clear();

    // 创建单个文本块
    TextBlock* textBlock = new TextBlock(this);
    textBlock->setPlainText(text);
    m_blocks.append(textBlock);
    calcNodeSize();
}

void MindMapNode::setAllHtml(const QString& html) {
    invalidateSerializationCache();
    // 简化处理：先清除，再创建单个文本块
    for (auto* block : m_blocks) {
        delete block;
    }
    m_blocks.clear();

    TextBlock* textBlock = new TextBlock(this);
    textBlock->setHtml(html);
    m_blocks.append(textBlock);
    calcNodeSize();
}

// 兼容旧接口
QString MindMapNode::nodeText() const {
    return allText();
}

void MindMapNode::setNodeText(const QString& text) {
    setAllText(text);
}

QString MindMapNode::nodeHtml() const {
    return allHtml();
}

void MindMapNode::setNodeHtml(const QString& html) {
    setAllHtml(html);
}

// ==================== 图片 API ====================

void MindMapNode::addImage(const QImage& image) {
    if (image.isNull()) return;

    // 完整保留原始图像，不在插入时压缩
    ImageBlock* imgBlock = new ImageBlock(image, this);
    static qint64 imgCounter = 0;
    imgBlock->setImageName(QString("mmimg_%1").arg(++imgCounter));

    // 默认插入到顶端；如果节点内有明确选中的块，则插入到该块之后
    int insertIdx = 0;
    if (m_selectedBlock) {
        insertIdx = blockIndex(m_selectedBlock) + 1;
    }

    addBlock(imgBlock, insertIdx);
    imgBlock->imageItem()->setImageBlock(imgBlock);
    setSelectedBlock(imgBlock);

    // 【新增】插入图片后，自动调整文本块宽度与节点一致
    autoFitTextBlockWidths();
    calcNodeSize();
    MindMapNode* root = getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (scene()) scene()->update();
}

void MindMapNode::removeImage(MindMapImageItem* imageItem) {
    if (!imageItem) return;

    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i]->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(m_blocks[i]);
            if (ib->imageItem() == imageItem) {
                removeBlockAt(i);
                // 【新增】删除图片后，自动调整文本块宽度与节点一致
                autoFitTextBlockWidths();
                calcNodeSize();
                MindMapNode* root = getRoot();
                if (root) {
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                }
                if (scene()) scene()->update();
                return;
            }
        }
    }
}

QVector<QPair<QString, QPair<QSizeF, QImage>>> MindMapNode::extractImages() const {
    QVector<QPair<QString, QPair<QSizeF, QImage>>> result;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(block);
            result.append(qMakePair(ib->imageName(),
                                    qMakePair(ib->imageItem()->imageSize(), ib->originalImage())));
        }
    }
    return result;
}

void MindMapNode::injectImages(const QVector<QPair<QString, QPair<QSizeF, QImage>>>& images) {
    for (const auto& pair : images) {
        const QString& name = pair.first;
        const QSizeF& size = pair.second.first;
        const QImage& image = pair.second.second;

        if (image.isNull()) continue;

        ImageBlock* imgBlock = new ImageBlock(image, this);
        imgBlock->setImageName(name);
        imgBlock->setImageSize(size);
        addBlock(imgBlock);
    }
    // 【新增】注入图片后，自动调整文本块宽度与节点一致
    autoFitTextBlockWidths();
    calcNodeSize();
    MindMapNode* root = getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
}

// ==================== 公式 API ====================

void MindMapNode::insertFormula(const QString& latexCode) {
    if (latexCode.trimmed().isEmpty()) return;
    QImage image = MindMapFormulaDialog::renderLatexToImage(latexCode, 16);
    if (image.isNull()) return;

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(image.devicePixelRatio());

    FormulaBlock* formulaBlock = new FormulaBlock(pixmap, latexCode, this);

    // 默认插入到顶端；如果节点内有明确选中的块，则插入到该块之后
    int insertIdx = 0;
    if (m_selectedBlock) {
        insertIdx = blockIndex(m_selectedBlock) + 1;
    }

    addBlock(formulaBlock, insertIdx);
    formulaBlock->pixmapItem()->setFormulaBlock(formulaBlock);

    // 【新增】插入公式后，自动调整文本块宽度与节点一致
    autoFitTextBlockWidths();
    calcNodeSize();
    MindMapNode* root = getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (scene()) scene()->update();
}

void MindMapNode::editFormula(int index) {
    if (index < 0 || index >= m_blocks.size()) return;
    if (m_blocks[index]->type() != BlockType::Formula) return;

    auto* fb = static_cast<FormulaBlock*>(m_blocks[index]);
    fb->editFormula();
}

void MindMapNode::removeFormula(int index) {
    if (index < 0 || index >= m_blocks.size()) return;
    if (m_blocks[index]->type() != BlockType::Formula) return;
    removeBlockAt(index);
    // 【新增】删除公式后，自动调整文本块宽度与节点一致
    autoFitTextBlockWidths();
    calcNodeSize();
    MindMapNode* root = getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (scene()) scene()->update();
}

QVector<QString> MindMapNode::extractFormulas() const {
    QVector<QString> result;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Formula) {
            result.append(static_cast<FormulaBlock*>(block)->latexCode());
        }
    }
    return result;
}

void MindMapNode::injectFormulas(const QVector<QString>& formulas) {
    for (const QString& latex : formulas) {
        if (!latex.isEmpty()) {
            insertFormula(latex);
        }
    }
    // insertFormula 内部已经调用了 autoFitTextBlockWidths，这里不需要重复调用
}

// ==================== 尺寸计算 ====================

qreal MindMapNode::horizontalTextMargin() const {
    return qMax<qreal>(MARGIN_H, styleForCurrentLevel().font().pointSizeF() * 0.9);
}

qreal MindMapNode::verticalTextMargin() const {
    return qMax<qreal>(4.0, styleForCurrentLevel().font().pointSizeF() * 0.45);
}

void MindMapNode::calcNodeSize() {
    const qreal textHMargin = horizontalTextMargin();
    // 尺寸计算必须使用当前层级样式，否则这里会覆盖设置对话框应用的字体。
    const QFont font = styleForCurrentLevel().font();

    qreal totalHeight = 0;
    qreal maxContentWidth = 0;

    // 先计算非文本块（图片/公式）的最大宽度
    for (auto* block : m_blocks) {
        if (block->type() != BlockType::Text) {
            qreal w = block->width();
            if (w > maxContentWidth) maxContentWidth = w;
        }
    }

    // 确定文本块宽度
    qreal textWidth;
    if (m_textBlockWidth > 0) {
        textWidth = m_textBlockWidth;
    } else {
        qreal textNaturalWidth = 0;
        for (auto* block : m_blocks) {
            if (block->type() == BlockType::Text) {
                auto* tb = static_cast<TextBlock*>(block);
                qreal naturalW = 0;
                QTextDocument* document = tb->textItem()->document();
                for (QTextBlock paragraph = document->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
                    qreal paragraphWidth = 0;
                    for (auto it = paragraph.begin(); !it.atEnd(); ++it) {
                        const QTextFragment fragment = it.fragment();
                        if (!fragment.isValid()) continue;
                        QFont fragmentFont = font;
                        const QTextCharFormat format = fragment.charFormat();
                        const QStringList families = format.fontFamilies().toStringList();
                        if (format.hasProperty(QTextFormat::FontFamilies) && !families.isEmpty())
                            fragmentFont.setFamily(families.first());
                        if (format.hasProperty(QTextFormat::FontPointSize))
                            fragmentFont.setPointSizeF(format.fontPointSize());
                        if (format.hasProperty(QTextFormat::FontWeight))
                            fragmentFont.setWeight(static_cast<QFont::Weight>(format.fontWeight()));
                        if (format.hasProperty(QTextFormat::FontItalic))
                            fragmentFont.setItalic(format.fontItalic());
                        paragraphWidth += QFontMetricsF(fragmentFont).horizontalAdvance(fragment.text());
                    }
                    naturalW = qMax(naturalW, paragraphWidth);
                }
                if (naturalW > textNaturalWidth) textNaturalWidth = naturalW;
            }
        }
        constexpr qreal TEXT_LAYOUT_SAFETY_MARGIN = 10.0;
        const qreal maxTextWidth = qMax<qreal>(
            54.0, QSettings().value(QStringLiteral("textLineWidth"), 200).toReal());
        textWidth = qMax<qreal>(60.0, qMin(textNaturalWidth + TEXT_LAYOUT_SAFETY_MARGIN, maxTextWidth));
        if (maxContentWidth > maxTextWidth) textWidth = maxContentWidth;
    }

    if (maxContentWidth > textWidth) {
        textWidth = maxContentWidth;
    }

    if (textWidth < 60) textWidth = 60;

    // 设置所有文本块的固定宽度
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            auto* tb = static_cast<TextBlock*>(block);
            tb->textItem()->setFixedTextWidth(textWidth);
        }
    }

    qreal nodeContentWidth = qMax(textWidth, maxContentWidth);
    qreal nodeWidth = nodeContentWidth + textHMargin;

    for (auto* block : m_blocks) {
        qreal h = block->height();
        totalHeight += h;
        block->setCachedHeight(h);
    }

    if (m_blocks.isEmpty()) {
        qreal h = 40;
        qreal w = h * 0.5;
        setRect(0, 0, w, h);
        m_nodeSizeCache.markClean();
        return;
    }

    // Keep vertical breathing room proportional to the active font size.
    qreal h = totalHeight + verticalTextMargin();
    if (h < 40) h = 40;

    bool allEmpty = true;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            if (!static_cast<TextBlock*>(block)->isEmpty()) {
                allEmpty = false;
                break;
            }
        } else {
            allEmpty = false;
            break;
        }
    }

    if (allEmpty) {
        qreal w = h * 0.5;
        setRect(0, 0, w, h);
        // 【修复】空节点也要设置文本块宽度，避免后续添加内容时宽度未初始化
        for (auto* block : m_blocks) {
            if (block->type() == BlockType::Text) {
                auto* tb = static_cast<TextBlock*>(block);
                tb->textItem()->setFixedTextWidth(w - textHMargin);
            }
        }
    } else {
        setRect(0, 0, nodeWidth, h);
    }
    updateBlockLayout();
    m_nodeSizeCache.markClean();
}

void MindMapNode::invalidateNodeSize() {
    m_nodeSizeCache.invalidate();
}

void MindMapNode::refreshNodeSizeIfNeeded() {
    if (m_nodeSizeCache.consumeDirty()) {
        calcNodeSize();
    }
}

void MindMapNode::updateBlockLayout() {
    qreal contentHeight = 0;
    for (auto* block : m_blocks) {
        contentHeight += block->cachedHeight();
    }

    qreal currentY = (rect().height() - contentHeight) / 2;
    qreal nodeContentWidth = rect().width() - horizontalTextMargin();

    for (auto* block : m_blocks) {
        // 【修复】文本块使用节点内容宽度，避免短文本时 boundingRect 返回实际宽度
        qreal blockWidth = (block->type() == BlockType::Text)
                               ? nodeContentWidth
                               : block->width();
        qreal x = (rect().width() - blockWidth) / 2;
        block->setBlockPos(QPointF(x, currentY));
        currentY += block->cachedHeight();
    }
}

qreal MindMapNode::calcRequiredHeight() const {
    if (!m_expanded || m_children.isEmpty()) return rect().height();
    qreal total = 0;
    for (auto* child : m_children) {
        total += child->calcRequiredHeight();
    }
    total += (m_children.size() - 1) * V_GAP;
    return qMax(total, rect().height());
}

// 在计算任意父节点的子树高度前，先让所有后代使用最新字体、行距和文本宽度完成尺寸计算。
static void refreshSubtreeNodeSizes(MindMapNode* node) {
    if (!node) return;
    node->refreshNodeSizeIfNeeded();
    for (auto* child : node->childNodes()) {
        refreshSubtreeNodeSizes(child);
    }
}

// ==================== 核心布局 ====================

void MindMapNode::layoutTree(qreal x, qreal y) {
    PerformanceMetrics::ScopedTimer totalTimer(PerformanceMetrics::Stage::LayoutTree);
    {
        PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::LayoutNodeSizeRefresh);
        refreshSubtreeNodeSizes(this);
    }
    {
        PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::LayoutPositions);
        layoutTreeRecursive(x, y);
    }
}

void MindMapNode::layoutTreeRecursive(qreal x, qreal y) {

    // 样式应用会改变文本高度；只在最外层布局入口刷新一次整棵树。
    // 递归子调用复用已计算的 cachedHeight，避免每层重复扫描整个子树。
    qreal subtreeHeight = calcRequiredHeight();
    qreal nodeY = y + (subtreeHeight - rect().height()) / 2;
    setPos(x, nodeY);

    if (!m_expanded || m_children.isEmpty()) return;

    QVector<qreal> childHeights;
    qreal totalChildrenHeight = 0;
    for (auto* child : m_children) {
        qreal ch = child->calcRequiredHeight();
        childHeights.append(ch);
        totalChildrenHeight += ch;
    }
    totalChildrenHeight += (m_children.size() - 1) * V_GAP;

    qreal childAreaTop = y + (subtreeHeight - totalChildrenHeight) / 2;
    qreal currentY = childAreaTop;
    qreal childX = x + rect().width() + H_GAP;

    for (int i = 0; i < m_children.size(); ++i) {
        m_children[i]->layoutTreeRecursive(childX, currentY);
        currentY += childHeights[i] + V_GAP;
    }
}

void MindMapNode::shiftSubtree(qreal dy) {
    setY(y() + dy);
    for (auto* child : m_children) {
        child->shiftSubtree(dy);
    }
}

void MindMapNode::adjustLayout() {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::LayoutAdjust);
    QVector<MindMapNode*> all;
    collectNodes(this, all);
    std::sort(all.begin(), all.end(), compareNodeX);

    int i = 0;
    while (i < all.size()) {
        int j = i + 1;
        while (j < all.size() && qAbs(all[j]->x() - all[i]->x()) < 1.0) {
            ++j;
        }
        std::sort(all.begin() + i, all.begin() + j, [](MindMapNode* a, MindMapNode* b) {
            return a->y() < b->y();
        });
        for (int k = i + 1; k < j; ++k) {
            qreal prevBottom = all[k - 1]->y() + all[k - 1]->rect().height();
            qreal minY = prevBottom + V_GAP;
            if (all[k]->y() < minY) {
                qreal shift = minY - all[k]->y();
                all[k]->shiftSubtree(shift);
            }
        }
        i = j;
    }
}

// ==================== 选中 / 编辑 ====================

void MindMapNode::setNodeSelected(bool selected) {
    if (m_isSelected == selected) return;
    m_isSelected = selected;
    update();
    if (selected) {
        setFocus();
    }
}

void MindMapNode::startEdit() {
    if (m_isEditing) return;

    // 【修改】找到最后一个文本块进行编辑
    TextBlock* targetBlock = nullptr;
    for (int i = m_blocks.size() - 1; i >= 0; --i) {
        if (m_blocks[i]->type() == BlockType::Text) {
            targetBlock = static_cast<TextBlock*>(m_blocks[i]);
            break;
        }
    }

    // 如果没有文本块，在末尾创建一个
    if (!targetBlock) {
        targetBlock = new TextBlock(this);
        m_blocks.append(targetBlock);
        calcNodeSize();
    }

    startEditBlock(targetBlock);
    // 【修复】确保光标在文本末尾（startEditBlock 中 setCursorAtStart 会覆盖，所以在这里重新设置）
    // 注意：合并后 targetBlock 指针可能已失效，需要重新获取
    if (m_editingTextBlock) {
        m_editingTextBlock->setCursorAtEnd();
    }
}

void MindMapNode::startEditBlock(TextBlock* block) {
    if (!block) return;
    const bool enteringEdit = !m_isEditing;

    // 【修复】如果之前有未清理的 z-value 备份（异常情况），先安全恢复并清空
    if (!m_childOldZValues.isEmpty()) {
        for (auto* child : childItems()) {
            auto it = m_childOldZValues.find(child);
            if (it != m_childOldZValues.end()) {
                child->setZValue(it.value());
            }
        }
        m_childOldZValues.clear();
    }

    // 编辑过程中保持 TextBlock/QGraphicsTextItem 对象稳定。
    // 进入/切换编辑时销毁文本项会让当前鼠标事件继续访问悬空对象，
    // 从而产生幽灵光标和随机崩溃。
    if (m_isEditing && m_editingTextBlock && m_editingTextBlock != block) {
        m_editingTextBlock->setEditing(false);
        QObject::disconnect(m_textConnection);
        m_isEditing = false;
        m_editingTextBlock = nullptr;
    }

    if (!block || blockIndex(block) < 0) {
        return;
    }

    m_isEditing = true;
    m_editingTextBlock = block;
    // 将编辑中的节点及其所有子 item 提升到画布顶层
    m_oldZValue = zValue();
    setZValue(1000);
    for (auto* child : childItems()) {
        if (child) {
            m_childOldZValues[child] = child->zValue();
            child->setZValue(1000);
        }
    }
    // 每次进入编辑状态都重新备份所有文本块内容
    if (enteringEdit) {
        m_editBackup.clear();
        for (auto* b : m_blocks) {
            if (b->type() == BlockType::Text) {
                m_editBackup.append(static_cast<TextBlock*>(b)->toHtml());  // 【修复】toHtml() 保留格式
            }
        }
    }
    notifyViewButtonState(this);

    // 非当前文本块虽然不启用文本编辑，但必须继续接收点击并显示 I-beam，
    // 才能在同一节点的多个文本块之间切换焦点。
    for (auto* b : m_blocks) {
        if (b->type() == BlockType::Text) {
            auto* textBlock = static_cast<TextBlock*>(b);
            if (textBlock != block) {
                textBlock->setEditing(false);
            }
            textBlock->textItem()->setAcceptedMouseButtons(Qt::LeftButton);
            textBlock->textItem()->setCursor(Qt::IBeamCursor);
        }
    }

    // 临时屏蔽 focusOut，防止 setFocus 触发 finishEdit
    m_blockFocusOut = true;
    block->setEditing(true);
    // 【修改】根据原始光标位置设置焦点位置
    // 如果之前没有编辑过，光标放在末尾；否则放在之前的位置
    if (m_editBackup.isEmpty()) {
        block->setCursorAtEnd();
    } else {
        // 尝试恢复到最后一个文本块的末尾位置
        block->setCursorAtEnd();
    }
    m_blockFocusOut = false;

    m_textConnection = QObject::connect(block->textItem()->document(), &QTextDocument::contentsChanged,
                                        [this]() { onDocumentChanged(); });
    onDocumentChanged();
    if (enteringEdit && scene() && !scene()->views().isEmpty()) {
        if (auto* view = dynamic_cast<MindMapView*>(scene()->views().first())) {
            view->ensureNodeInEditSafeZone(this);
        }
    }
    update();
}


void MindMapNode::focusPrevBlock() {
    if (!m_isEditing || !m_editingTextBlock) return;
    const int currentIndex = blockIndex(m_editingTextBlock);
    for (int i = currentIndex - 1; i >= 0; --i) {
        if (m_blocks[i]->type() == BlockType::Text) {
            auto* previousBlock = static_cast<TextBlock*>(m_blocks[i]);
            startEditBlock(previousBlock);
            previousBlock->setCursorAtEnd();
            return;
        }
    }
    m_editingTextBlock->setCursorAtStart();
}

void MindMapNode::focusNextBlock() {
    if (!m_isEditing || !m_editingTextBlock) return;
    const int currentIndex = blockIndex(m_editingTextBlock);
    for (int i = currentIndex + 1; i < m_blocks.size(); ++i) {
        if (m_blocks[i]->type() == BlockType::Text) {
            auto* nextBlock = static_cast<TextBlock*>(m_blocks[i]);
            startEditBlock(nextBlock);
            nextBlock->setCursorAtStart();
            return;
        }
    }
    m_editingTextBlock->setCursorAtEnd();
}

void MindMapNode::finishEdit(bool ensureVisible) {
    if (!m_isEditing || !m_editingTextBlock) return;

    // 不再在结束编辑时拆分并销毁文本块，避免当前文本项事件栈中出现悬空对象。
    QStringList newBlocks;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            newBlocks.append(static_cast<TextBlock*>(block)->toHtml());  // 【修复】toHtml() 保留格式
        }
    }

    TextBlock* editingBlock = m_editingTextBlock;
    m_isEditing = false;
    m_editingTextBlock = nullptr;
    notifyViewButtonState(this);

    QObject::disconnect(m_textConnection);

    editingBlock->setEditing(false);

    // 结束编辑时取消块选中
    clearBlockSelection();
    // 恢复节点和所有子 item 的 z-value
    setZValue(m_oldZValue);
    // 【修复】只遍历当前实际存在的 childItems()，避免访问已删除 block 的悬空指针
    for (auto* child : childItems()) {
        auto it = m_childOldZValues.find(child);
        if (it != m_childOldZValues.end()) {
            child->setZValue(it.value());
        }
    }
    m_childOldZValues.clear();
    // External clicks transfer focus to their target after editing ends. Do
    // not focus an off-screen node here, since Qt may scroll it into view.
    if (ensureVisible) setFocus();

    // 恢复所有文本块的鼠标状态
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            auto* tb = static_cast<TextBlock*>(block);
            tb->textItem()->setAcceptedMouseButtons(Qt::NoButton);
            tb->textItem()->unsetCursor();
        }
    }

    // 重新收集当前文本块内容
    QStringList finalBlocks;
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            finalBlocks.append(static_cast<TextBlock*>(block)->toHtml());  // 【修复】toHtml() 保留格式
        }
    }

    if (finalBlocks != m_editBackup && s_undoStack) {
        s_undoStack->push(new EditTextCommand(this, m_editBackup, finalBlocks, ensureVisible));
    } else {
        calcNodeSize();
        MindMapNode* root = getRoot();
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
        if (ensureVisible && m_isSelected && scene() && !scene()->views().isEmpty()) {
            scene()->views().first()->ensureVisible(this, 50, 50);
        }
        if (scene()) scene()->update();
    }
}


void MindMapNode::cancelEdit() {
    if (!m_isEditing || !m_editingTextBlock) return;

    m_isEditing = false;
    TextBlock* editingBlock = m_editingTextBlock;
    m_editingTextBlock = nullptr;
    notifyViewButtonState(this);

    QObject::disconnect(m_textConnection);
    editingBlock->setEditing(false);
    // 取消编辑时取消块选中
    clearBlockSelection();
    // 恢复节点和所有子 item 的 z-value
    setZValue(m_oldZValue);
    // 【修复】只遍历当前实际存在的 childItems()，避免访问已删除 block 的悬空指针
    for (auto* child : childItems()) {
        auto it = m_childOldZValues.find(child);
        if (it != m_childOldZValues.end()) {
            child->setZValue(it.value());
        }
    }
    m_childOldZValues.clear();

    // 恢复所有文本块的鼠标状态
    for (auto* block : m_blocks) {
        if (block->type() == BlockType::Text) {
            auto* tb = static_cast<TextBlock*>(block);
            tb->textItem()->setAcceptedMouseButtons(Qt::NoButton);
            tb->textItem()->unsetCursor();
        }
    }

    // 就地恢复已有文本项，不能在其自身键盘事件尚未返回时销毁它们。
    int backupIndex = 0;
    for (auto* block : m_blocks) {
        if (block->type() != BlockType::Text) continue;
        auto* textBlock = static_cast<TextBlock*>(block);
        if (backupIndex < m_editBackup.size()) {
            textBlock->setHtml(m_editBackup.at(backupIndex));
        }
        ++backupIndex;
    }

    setFocus();

    calcNodeSize();
    MindMapNode* root = getRoot();
    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();

    if (m_isSelected && scene() && !scene()->views().isEmpty()) {
        scene()->views().first()->ensureVisible(this, 50, 50);
    }
    if (scene()) scene()->update();
}


void MindMapNode::handleEditTab(bool shiftPressed) {
    if (!m_isEditing || !m_editingTextBlock) return;
    if (shiftPressed) {
        m_editingTextBlock->insertText("\t");
    } else {
        finishEdit();
    }
}

// ==================== 编辑过程中实时调整 ====================

void MindMapNode::onDocumentChanged() {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::TextChange);
    if (!m_isEditing || !m_editingTextBlock) return;

    // 编辑态也沿用当前层级样式，避免进入编辑时把动态字体改回旧默认值。
    const QFont font = styleForCurrentLevel().font();
    m_editingTextBlock->textItem()->setFont(font);
    qreal textWidth = m_textBlockWidth > 0 ? m_textBlockWidth : (rect().width() - MARGIN_H);
    if (textWidth < 60) textWidth = 60;
    m_editingTextBlock->textItem()->setFixedTextWidth(textWidth);
    // 【修复】移除 adjustSize()，避免文档 size() 被收缩
    // m_editingTextBlock->textItem()->document()->adjustSize();

    calcNodeSize();
    update();
}

// ==================== 展开 / 折叠 ====================

void MindMapNode::setSubtreeVisible(bool visible) {
    for (auto* child : m_children) {
        child->setVisible(visible);
        child->setSubtreeVisible(visible && child->isExpanded());
    }
}

void MindMapNode::setExpanded(bool expanded) {
    if (m_expanded == expanded) return;
    m_expanded = expanded;

    for (auto* child : m_children) {
        child->setVisible(expanded);
        child->setSubtreeVisible(expanded && child->isExpanded());
    }

    if (MindMapNode* root = getRoot()) {
        QPointF rootPos = root->pos();
        root->clearEdges();
        root->layoutTree(rootPos.x(), rootPos.y());
        root->adjustLayout();
        root->updateAllEdges();
    }

    if (scene() && !scene()->views().isEmpty()) {
        if (auto* view = scene()->views().first()) {
            view->centerOn(this);
        }
    }
}

// ==================== 节点宽度拖拽调整（类似XMind）====================

MindMapNode::ResizeEdge MindMapNode::hitTestResizeEdge(const QPointF& pos) const {
    // 只在选中状态下检测（编辑状态也可以拖动边界）
    if (!m_isSelected || m_isEditing) return ResizeEdge::None;

    // 使用节点矩形本身（不扩展高亮框）
    QRectF r = rect();

    // 检测左右边缘（扩大判定范围到 15 像素）
    qreal edgeThreshold = 15; // 边缘检测阈值（像素）

    // 垂直方向：在整个节点高度范围内都可检测（包括高亮框扩展区域）
    qreal detectTop = r.top() - HIGHLIGHT_MARGIN;
    qreal detectBottom = r.bottom() + HIGHLIGHT_MARGIN;

    // 左边缘检测
    if (qAbs(pos.x() - r.left()) <= edgeThreshold &&
        pos.y() >= detectTop && pos.y() <= detectBottom) {
        return ResizeEdge::Left;
    }
    // 右边缘检测
    if (qAbs(pos.x() - r.right()) <= edgeThreshold &&
        pos.y() >= detectTop && pos.y() <= detectBottom) {
        return ResizeEdge::Right;
    }
    return ResizeEdge::None;
}

void MindMapNode::updateResizeCursor(ResizeEdge edge) {
    switch (edge) {
    case ResizeEdge::Left:
    case ResizeEdge::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    default:
        unsetCursor();
        break;
    }
}

void MindMapNode::startNodeResize(ResizeEdge edge, const QPointF& pos) {
    if (m_isEditing || edge == ResizeEdge::None) return;
    m_isResizing = true;
    m_resizeEdge = edge;
    m_resizeStartPos = pos;
    m_resizeStartWidth = rect().width();
}

void MindMapNode::doNodeResize(const QPointF& pos) {
    if (!m_isResizing) return;

    qreal delta = 0;
    if (m_resizeEdge == ResizeEdge::Right) {
        delta = pos.x() - m_resizeStartPos.x();
    } else {
        // 左边缘：向右拖动增加宽度，向左拖动减小宽度
        delta = m_resizeStartPos.x() - pos.x();
    }

    qreal newWidth = m_resizeStartWidth + delta;
    newWidth = qBound(m_nodeMinWidth, newWidth, m_nodeMaxWidth);

    setNodeContentWidth(newWidth);
}

void MindMapNode::endNodeResize() {
    m_isResizing = false;
    m_resizeEdge = ResizeEdge::None;
    unsetCursor();
}

void MindMapNode::setTextBlockWidth(qreal width) {
    // 限制在有效范围内
    width = qBound(m_nodeMinWidth - MARGIN_H, width, m_nodeMaxWidth - MARGIN_H);
    m_textBlockWidth = width;
    invalidateSerializationCache();

    // 重新计算布局（calcNodeSize 会根据 m_textBlockWidth 设置文本块宽度）
    calcNodeSize();
    updateBlockLayout();
    update();
}

qreal MindMapNode::textBlockWidth() const {
    return m_textBlockWidth;
}

// 【保留兼容】setNodeContentWidth 现在也是设置文本块宽度
void MindMapNode::setNodeContentWidth(qreal width) {
    setTextBlockWidth(width - MARGIN_H);
}

void MindMapNode::applyTextLineWidthPreferenceToSubtree() {
    invalidateNodeSize();
    refreshNodeSizeIfNeeded();
    for (MindMapNode* child : m_children) child->applyTextLineWidthPreferenceToSubtree();
}

qreal MindMapNode::nodeContentWidth() const {
    return rect().width() - MARGIN_H;
}

// ==================== 导航辅助 ====================

MindMapNode* MindMapNode::getPrevSibling() const {
    if (!m_parent) return nullptr;
    auto siblings = m_parent->childNodes();
    int idx = siblings.indexOf(const_cast<MindMapNode*>(this));
    if (idx > 0) return siblings[idx - 1];
    return nullptr;
}

MindMapNode* MindMapNode::getNextSibling() const {
    if (!m_parent) return nullptr;
    auto siblings = m_parent->childNodes();
    int idx = siblings.indexOf(const_cast<MindMapNode*>(this));
    if (idx >= 0 && idx < siblings.size() - 1) return siblings[idx + 1];
    return nullptr;
}

MindMapNode* MindMapNode::getFirstChild() const {
    if (!m_expanded || m_children.isEmpty()) return nullptr;
    return m_children.first();
}

MindMapNode* MindMapNode::getRoot() {
    MindMapNode* root = this;
    while (root->m_parent) root = root->m_parent;
    return root;
}

// ==================== 连线 ====================

void MindMapNode::clearEdges() {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::EdgeClear, m_parent == nullptr);
    for (auto* child : m_children) {
        child->clearEdges();
    }
    for (auto* edge : m_edgeItems) {
        if (edge && edge->scene()) edge->scene()->removeItem(edge);
        delete edge;
    }
    m_edgeItems.clear();
}

void MindMapNode::updateAllEdges() {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::EdgeRebuild, m_parent == nullptr);
    for (auto* child : m_children) {
        child->updateAllEdges();
    }
    updateEdges();
}

void MindMapNode::updateEdges() {
    for (auto* edge : m_edgeItems) {
        if (edge && edge->scene()) edge->scene()->removeItem(edge);
        delete edge;
    }
    m_edgeItems.clear();

    if (!isVisible()) return;
    if (!scene()) return;
    if (m_children.isEmpty()) return;

    if (!m_expanded) {
        QPointF start = connectionPointRight();
        QPointF end   = mapToScene(rect().width() + H_GAP / 2, rect().height() / 2);

        auto* line = new QGraphicsLineItem(start.x(), start.y(), end.x(), end.y());
        QPen pen(themeEdgeColor());
        pen.setWidth(3);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        line->setPen(pen);
        line->setZValue(-1);
        scene()->addItem(line);
        m_edgeItems.append(line);
        return;
    }

    QPointF start = connectionPointRight();

    for (auto* child : m_children) {
        if (!child->scene()) continue;
        QPointF end = child->connectionPointLeft();
        qreal midX = start.x() + (end.x() - start.x()) / 2;

        auto* line1 = new QGraphicsLineItem(start.x(), start.y(), midX, start.y());
        auto* line2 = new QGraphicsLineItem(midX, start.y(), midX, end.y());
        auto* line3 = new QGraphicsLineItem(midX, end.y(), end.x(), end.y());

        QPen pen(themeEdgeColor());
        pen.setWidth(3);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        line1->setPen(pen);
        line2->setPen(pen);
        line3->setPen(pen);

        line1->setZValue(-1);
        line2->setZValue(-1);
        line3->setZValue(-1);

        scene()->addItem(line1);
        scene()->addItem(line2);
        scene()->addItem(line3);

        m_edgeItems.append(line1);
        m_edgeItems.append(line2);
        m_edgeItems.append(line3);
    }
}

QPointF MindMapNode::connectionPointRight() const {
    return mapToScene(rect().width(), rect().height() / 2);
}

QPointF MindMapNode::connectionPointLeft() const {
    return mapToScene(0, rect().height() / 2);
}

// ==================== 悬停与指示器检测 ====================

bool MindMapNode::updateHoverState(const QPointF& nodePos) {
    bool shouldHover = false;
    if (!m_isEditing && !m_isSelected) {
        QRectF r = rect();
        qreal top = -INDICATOR_HIT_RADIUS;
        qreal bottom = r.height() + INDICATOR_HIT_RADIUS;
        qreal right = r.width() + H_GAP / 2;
        qreal left = -INDICATOR_HIT_RADIUS;

        if (nodePos.y() >= top && nodePos.y() <= bottom
            && nodePos.x() >= left && nodePos.x() <= right) {
            shouldHover = true;
        }
    }
    if (m_isHovered != shouldHover) {
        m_isHovered = shouldHover;
        update();
    }
    return shouldHover;
}

bool MindMapNode::hitIndicator(const QPointF& nodePos) const {
    if (m_children.isEmpty()) return false;
    QPointF indicatorCenter(rect().width() + H_GAP / 2, rect().height() / 2);
    return QLineF(nodePos, indicatorCenter).length() <= INDICATOR_HIT_RADIUS;
}

// ==================== 拖动重排序支持 ====================

int MindMapNode::calculateBlockInsertIndex(qreal localY) const {
    if (m_blocks.isEmpty()) return 0;

    qreal contentHeight = 0;
    for (auto* block : m_blocks) {
        contentHeight += block->cachedHeight();
    }
    contentHeight += (m_blocks.size() - 1) * BLOCK_SPACING;

    qreal contentTop = (rect().height() - contentHeight) / 2;

    for (int i = 0; i < m_blocks.size(); ++i) {
        qreal blockTop = contentTop;
        qreal blockBottom = blockTop + m_blocks[i]->cachedHeight();
        qreal midY = (blockTop + blockBottom) / 2;

        if (localY < midY) {
            return i;
        }
        contentTop += m_blocks[i]->cachedHeight() + BLOCK_SPACING;
    }

    return m_blocks.size();
}

void MindMapNode::showDragIndicator(int index) {
    if (m_blocks.isEmpty() || index < 0 || index > m_blocks.size()) {
        hideDragIndicator();
        return;
    }

    qreal contentHeight = 0;
    for (auto* block : m_blocks) {
        contentHeight += block->cachedHeight();
    }
    contentHeight += (m_blocks.size() - 1) * BLOCK_SPACING;

    qreal contentTop = (rect().height() - contentHeight) / 2;
    qreal indicatorY = contentTop;

    for (int i = 0; i < index && i < m_blocks.size(); ++i) {
        indicatorY += m_blocks[i]->cachedHeight() + BLOCK_SPACING;
    }

    const QLineF line(MARGIN_H / 2, indicatorY,
                      rect().width() - MARGIN_H / 2, indicatorY);
    if (!m_dragIndicator) {
        m_dragIndicator = new QGraphicsLineItem(line, this);
        QPen pen(QColor(0, 120, 255));
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        m_dragIndicator->setPen(pen);
        m_dragIndicator->setZValue(100);
    } else if (m_dragIndicator->line() != line) {
        m_dragIndicator->setLine(line);
    }
    m_dragIndicator->setVisible(true);
}

void MindMapNode::hideDragIndicator() {
    if (m_dragIndicator) m_dragIndicator->setVisible(false);
}

// ==================== 事件处理 ====================

void MindMapNode::handleChildPressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isEditing) {
        event->ignore();
        return;
    }

    // 取消其他节点的选中
    if (scene()) {
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                if (node != this) node->setNodeSelected(false);
            }
        }
    }

    // 如果当前有块被选中，点击节点空白位置则取消选中
    if (m_selectedBlock) {
        QPointF clickPos = event->pos();
        bool clickedOnBlock = false;
        for (auto* block : m_blocks) {
            if (block->type() == BlockType::Image) {
                auto* ib = static_cast<ImageBlock*>(block);
                if (ib->imageItem() && ib->imageItem()->boundingRect().translated(ib->blockPos()).contains(clickPos)) {
                    clickedOnBlock = true;
                    break;
                }
            } else if (block->type() == BlockType::Formula) {
                auto* fb = static_cast<FormulaBlock*>(block);
                if (fb->pixmapItem() && fb->pixmapItem()->boundingRect().translated(fb->blockPos()).contains(clickPos)) {
                    clickedOnBlock = true;
                    break;
                }
            }
        }
        if (!clickedOnBlock) {
            clearBlockSelection();
            MindMapNode* root = getRoot();
            root->clearEdges();
            root->layoutTree(0, 0);
            root->adjustLayout();
            root->updateAllEdges();
            if (scene()) scene()->update();
        }
    }

    setNodeSelected(true);
    QGraphicsRectItem::mousePressEvent(event);
}

void MindMapNode::handleChildMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF nodePos = event->pos();
    updateHoverState(nodePos);
}

void MindMapNode::handleChildReleaseEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event)
}

void MindMapNode::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // 结束其他节点的编辑
    if (scene()) {
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                if (node != this && node->isEditing()) {
                    node->finishEdit();
                }
            }
        }
    }

    // 编辑状态下点击节点内部：不退出编辑，让子文本块自己处理焦点切换
    // 但如果点击的是图片或公式块，则先退出编辑（拆分），再处理点击
    if (m_isEditing && event->button() == Qt::LeftButton) {
        QPointF localPos = event->pos();
        bool onNonTextBlock = false;
        for (auto* block : m_blocks) {
            if (block->type() == BlockType::Image) {
                auto* ib = static_cast<ImageBlock*>(block);
                if (ib->imageItem() && ib->imageItem()->boundingRect().translated(ib->blockPos()).contains(localPos)) {
                    onNonTextBlock = true;
                    break;
                }
            } else if (block->type() == BlockType::Formula) {
                auto* fb = static_cast<FormulaBlock*>(block);
                if (fb->pixmapItem() && fb->pixmapItem()->boundingRect().translated(fb->blockPos()).contains(localPos)) {
                    onNonTextBlock = true;
                    break;
                }
            }
        }
        if (onNonTextBlock) {
            // 点击了图片/公式块：先退出编辑（拆分），然后处理点击
            // 【注意】finishEdit 会自动拆分文本块
            finishEdit();
            // 重新选中当前节点，让子 item 处理点击事件
            setNodeSelected(true);
            QGraphicsRectItem::mousePressEvent(event);
            return;
        }
        // 点击文本区域或空白处：不退出编辑
        if (rect().contains(localPos)) {
            event->accept();
            return;
        }
    }

    handleChildPressEvent(event);
}

void MindMapNode::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    // 【新增】处理节点宽度拖拽
    if (m_isResizing) {
        doNodeResize(event->pos());
        event->accept();
        return;
    }

    // 【新增】悬停时检测高亮框边缘并更新光标
    if (m_isSelected && !m_isEditing) {
        ResizeEdge edge = hitTestResizeEdge(event->pos());
        updateResizeCursor(edge);
    }

    QGraphicsRectItem::mouseMoveEvent(event);
}

void MindMapNode::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    // 【新增】结束节点宽度拖拽
    if (m_isResizing && event->button() == Qt::LeftButton) {
        endNodeResize();
        // 【新增】触发全局重新布局
        MindMapNode* root = getRoot();
        if (root) {
            QPointF rootPos = root->pos();
            root->clearEdges();
            root->layoutTree(rootPos.x(), rootPos.y());
            root->adjustLayout();
            root->updateAllEdges();
        }
        if (scene()) scene()->update();
        event->accept();
        return;
    }

    QGraphicsRectItem::mouseReleaseEvent(event);
}

void MindMapNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!m_isEditing) {
        setNodeSelected(true);
        startEdit();
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

void MindMapNode::keyPressEvent(QKeyEvent* event) {
    if (m_isEditing) {
        // 编辑状态下优先处理选中的块（图片/公式）
        if (m_selectedBlock) {
            if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
                int idx = blockIndex(m_selectedBlock);
                if (idx >= 0) {
                    // 【修复】使用 RemoveBlockCommand 进入 undo 栈
                    if (s_undoStack && scene()) {
                        s_undoStack->push(new RemoveBlockCommand(this, idx, scene()));
                    } else {
                        removeBlockAt(idx);
                        // 【新增】删除块后，自动调整文本块宽度与节点一致
                        autoFitTextBlockWidths();
                        calcNodeSize();
                        MindMapNode* root = getRoot();
                        if (root) {
                            root->clearEdges();
                            root->layoutTree(0, 0);
                            root->adjustLayout();
                            root->updateAllEdges();
                        }
                        if (scene()) scene()->update();
                    }
                    event->accept();
                    return;
                }
            }
            // Shift+Enter：在选中块下方插入空文本块
            if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
                && (event->modifiers() & Qt::ShiftModifier)) {
                int idx = blockIndex(m_selectedBlock);
                if (idx >= 0) {
                    // 【修复】使用 InsertBlockCommand 进入 undo 栈
                    TextBlock* newBlock = new TextBlock(this);
                    if (s_undoStack && scene()) {
                        s_undoStack->push(new InsertBlockCommand(this, newBlock, idx + 1, scene()));
                    } else {
                        addBlock(newBlock, idx + 1);
                    }
                    startEditBlock(newBlock);
                    clearBlockSelection();
                    event->accept();
                    return;
                }
            }
        }
        if (event->key() == Qt::Key_Escape) {
            cancelEdit();
            event->accept();
            return;
        }
        // 【修改】Shift+Enter 在合并状态下插入段落分隔
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && (event->modifiers() & Qt::ShiftModifier)) {
            if (m_editingTextBlock) {
                if (!(event->modifiers() & Qt::ControlModifier)) {
                    QTextCursor cursor = m_editingTextBlock->textItem()->textCursor();
                    cursor.insertBlock();
                    m_editingTextBlock->textItem()->setTextCursor(cursor);
                } else {
                    splitTextBlockAtCursor(m_editingTextBlock);
                }
            }
            event->accept();
            return;
        }
        QGraphicsRectItem::keyPressEvent(event);
        return;
    }

    if (!m_isSelected) {
        QGraphicsRectItem::keyPressEvent(event);
        return;
    }

    MindMapNode* root = getRoot();

    // 节点选中态下，Shift+Enter 在当前选中图片/公式下方插入文本块并进入编辑。
    if (m_selectedBlock
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ShiftModifier)) {
        const int idx = blockIndex(m_selectedBlock);
        if (idx >= 0) {
            auto* newBlock = new TextBlock(this);
            if (s_undoStack && scene()) {
                s_undoStack->push(new InsertBlockCommand(this, newBlock, idx + 1, scene()));
            } else {
                addBlock(newBlock, idx + 1);
                applyStyle(styleForCurrentLevel());
            }
            // InsertBlockCommand::redo() performs the insertion; enter editing explicitly
            // afterward so both undo-enabled and direct paths have identical behavior.
            startEditBlock(newBlock);
            clearBlockSelection();
            event->accept();
            return;
        }
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (s_undoStack && scene()) {
                s_undoStack->push(new AddSiblingCommand(this, scene()));
            } else {
                MindMapNode* newNode = nullptr;
                if (m_parent) {
                    auto siblings = m_parent->childNodes();
                    int idx = siblings.indexOf(const_cast<MindMapNode*>(this));
                    newNode = new MindMapNode("", nullptr);
                    if (idx >= 0) {
                        m_parent->insertChild(idx + 1, newNode);
                    } else {
                        m_parent->addChild(newNode);
                    }
                } else {
                    newNode = new MindMapNode("", this);
                }
                if (newNode) {
                    if (scene()) scene()->addItem(newNode);
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                    setNodeSelected(false);
                    newNode->setNodeSelected(true);
                }
            }
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_Tab: {
        if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (s_undoStack && scene()) {
                s_undoStack->push(new AddChildCommand(this, scene()));
            } else {
                auto* newNode = new MindMapNode("", this);
                if (scene()) scene()->addItem(newNode);
                root->clearEdges();
                root->layoutTree(0, 0);
                root->adjustLayout();
                root->updateAllEdges();
                setNodeSelected(false);
                newNode->setNodeSelected(true);
            }
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        // 优先删除选中的块
        if (m_selectedBlock) {
            int idx = blockIndex(m_selectedBlock);
            if (idx >= 0) {
                // 【修复】使用 RemoveBlockCommand 进入 undo 栈
                if (s_undoStack && scene()) {
                    s_undoStack->push(new RemoveBlockCommand(this, idx, scene()));
                } else {
                    removeBlockAt(idx);
                }
            }
            event->accept();
            return;
        }
        if (m_parent && scene()) {
            if (s_undoStack) {
                s_undoStack->push(new RemoveNodeCommand(this, scene()));
                event->accept();
                return;
            } else {
                auto* parent = m_parent;
                auto siblings = parent->childNodes();
                int idx = siblings.indexOf(const_cast<MindMapNode*>(this));

                MindMapNode* newSelected = nullptr;
                if (idx < siblings.size() - 1) {
                    newSelected = siblings[idx + 1];
                } else if (idx > 0) {
                    newSelected = siblings[idx - 1];
                } else {
                    newSelected = parent;
                }

                parent->removeChild(this);
                m_parent = nullptr;
                delete this;

                if (newSelected) {
                    newSelected->setNodeSelected(true);
                    root = newSelected->getRoot();
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                }
                event->accept();
                return;
            }
        }
        break;
    }
    case Qt::Key_Up: {
        auto* prev = getPrevSibling();
        if (prev) {
            setNodeSelected(false);
            prev->setNodeSelected(true);
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_Down: {
        auto* next = getNextSibling();
        if (next) {
            setNodeSelected(false);
            next->setNodeSelected(true);
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_Left: {
        if (m_parent) {
            setNodeSelected(false);
            m_parent->setNodeSelected(true);
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_Right: {
        auto* firstChild = getFirstChild();
        if (firstChild) {
            setNodeSelected(false);
            firstChild->setNodeSelected(true);
            event->accept();  // 【修复】
        }
        break;
    }
    case Qt::Key_F2:
    case Qt::Key_Space: {
        startEdit();
        event->accept();  // 【修复】
        break;
    }
    case Qt::Key_Escape: {
        QApplication::quit();
        event->accept();  // 【修复】
        break;
    }
    default:
        QGraphicsRectItem::keyPressEvent(event);
    }
}

// ==================== 绘制 ====================

void MindMapNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::NodePaint);
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);

    QRectF r = rect();

    if (m_isHovered && !m_isSelected && !m_isEditing) {
        QRectF hr = r.adjusted(-HIGHLIGHT_MARGIN, -HIGHLIGHT_MARGIN,
                               HIGHLIGHT_MARGIN, HIGHLIGHT_MARGIN);
        QPen pen(themeHover());
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        if (m_shape == MindMapNodeShape::Rect) painter->drawRect(hr);
        else painter->drawRoundedRect(hr, CORNER_RADIUS + 3, CORNER_RADIUS + 3);
    }

    // 编辑状态下也显示蓝色选中框；节点本身样式保持不变。
    if (m_isSelected) {
        QRectF hr = r.adjusted(-HIGHLIGHT_MARGIN, -HIGHLIGHT_MARGIN,
                               HIGHLIGHT_MARGIN, HIGHLIGHT_MARGIN);
        QPen pen(themeHighlight());
        pen.setWidth(3);
        painter->setPen(pen);
        if (m_shape == MindMapNodeShape::Rect) painter->drawRect(hr);
        else painter->drawRoundedRect(hr, CORNER_RADIUS + 3, CORNER_RADIUS + 3);
    }

    QPainterPath path;
    if (m_shape == MindMapNodeShape::Rect) path.addRect(r);
    else path.addRoundedRect(r, CORNER_RADIUS, CORNER_RADIUS);
    painter->fillPath(path, m_fillColor);

    QPen pen(m_borderColor);
    pen.setWidth(m_borderWidth);
    painter->setPen(pen);
    const QRectF borderRect = r.adjusted(m_borderWidth / 2.0, m_borderWidth / 2.0,
                                         -m_borderWidth / 2.0, -m_borderWidth / 2.0);
    if (borderRect.isValid()) {
        if (m_shape == MindMapNodeShape::Rect) painter->drawRect(borderRect);
        else painter->drawRoundedRect(borderRect,
                                      qMax<qreal>(0, CORNER_RADIUS - m_borderWidth / 2.0),
                                      qMax<qreal>(0, CORNER_RADIUS - m_borderWidth / 2.0));
    }

    // 折叠/展开指示器（保持不变）
    if (!m_children.isEmpty() && (m_isHovered || m_isSelected)) {
        QPointF center(r.width() + H_GAP / 2, r.height() / 2);
        qreal rad = INDICATOR_RADIUS;

        painter->setBrush(Qt::white);
        QPen borderPen(themeEdgeColor());
        borderPen.setWidth(1);
        painter->setPen(borderPen);
        painter->drawEllipse(center, rad, rad);

        QPen symPen(themeEdgeColor());
        symPen.setWidth(2);
        painter->setPen(symPen);
        qreal half = rad * 0.5;

        painter->drawLine(QPointF(center.x() - half, center.y()),
                          QPointF(center.x() + half, center.y()));

        if (!m_expanded) {
            painter->drawLine(QPointF(center.x(), center.y() - half),
                              QPointF(center.x(), center.y() + half));
        }
    }
}

void MindMapNode::handleKeyPress(QKeyEvent* event) {
    keyPressEvent(event);
}

// ==================== 深拷贝 ====================

MindMapNode* MindMapNode::cloneSubtree() const {
    QJsonObject json = MindMapIO::serialize(const_cast<MindMapNode*>(this), "copy");
    MindMapNode* clone = MindMapIO::deserialize(json, nullptr);
    if (clone) {
        clone->m_parent = nullptr;
        clone->m_isSelected = false;
        clone->m_isEditing = false;
        clone->m_isHovered = false;
    }
    return clone;
}

// ==================== 主题颜色静态方法 ====================

void MindMapNode::setDarkMode(bool dark) {
    s_darkMode = dark;
}

bool MindMapNode::isDarkMode() {
    return s_darkMode;
}

QColor MindMapNode::themeEdgeColor() {
    // 【修复】深色模式下连接线改为浅蓝色，浅色模式下保持深蓝色
    return s_darkMode ? QColor(100, 160, 220) : QColor(0, 0, 139);
}

QColor MindMapNode::themeNodeFill() {
    // 【修复】深色模式下节点和连线颜色保持不变，沿用浅色模式颜色
    return QColor(211, 234, 253);
}

QColor MindMapNode::themeNodeBorder() {
    // 【修复】深色模式下节点和连线颜色保持不变，沿用浅色模式颜色
    return QColor(211, 234, 253);
}

QColor MindMapNode::themeNodeText() {
    // 【修复】深色模式下节点和连线颜色保持不变，沿用浅色模式颜色
    return QColor(5, 56, 96);
}

QColor MindMapNode::themeHighlight() {
    // 【修复】深色模式下节点和连线颜色保持不变，沿用浅色模式颜色
    return QColor(0, 120, 255);
}

QColor MindMapNode::themeHover() {
    // 【修复】深色模式下节点和连线颜色保持不变，沿用浅色模式颜色
    return QColor(180, 210, 240);
}

void MindMapNode::applyThemeColors() {
    if (!m_parent) {
        // 根节点保持原有样式
        return;
    }
    // 文档节点样式由 MindMapLevelStyles 管理，主题切换不得覆盖它们。
    m_highlightColor = themeHighlight();
    m_hoverColor = themeHover();

    // 【修复】只在非编辑状态下更新文本块颜色，避免与编辑状态冲突
    if (!m_isEditing) {
        for (auto* block : m_blocks) {
            if (block->type() == BlockType::Text) {
                auto* tb = static_cast<TextBlock*>(block);
                tb->textItem()->setDefaultTextColor(m_textColor);
            }
        }
        update();
    }
    // 编辑状态下：颜色变量已更新，但不修改textItem和调用update()
    // 等退出编辑后自然生效
}

// 【新增】自动调整所有文本块宽度与当前节点内容宽度一致
// 现在由 calcNodeSize 统一管理，此函数保留但不再重复设置
void MindMapNode::autoFitTextBlockWidths() {
    // calcNodeSize 已确保文本块宽度与节点一致，无需额外操作
}

    // calcNodeSize 已经确保文本块宽度与节点一致，无需额外操作
