#include "MindMapBlock.h"
#include "MindMapNode.h"
#include "MindMapView.h"
#include "MindMapImageItem.h"
#include "MindMapFormulaDialog.h"
#include "MindMapCommands.h"
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextBlock>
#include <QTextLayout>
#include <QGraphicsScene>
#include <QPointer>
#include <QTimer>
#include <QInputMethodEvent>
#include <QApplication>
#include <QClipboard>
#include <QLineF>
#include <QGraphicsView>
#include <QScrollBar>

// ==================== MindMapTextItem ====================

MindMapTextItem::MindMapTextItem(QGraphicsItem* parent)
    : QGraphicsTextItem(parent), m_fixedTextWidth(-1) {
    setTabChangesFocus(false);
    setAcceptHoverEvents(true);
}

QString MindMapTextItem::textAfterCursor() const {
    QTextCursor cursor = textCursor();
    int pos = cursor.position();
    QString fullText = toPlainText();
    if (pos >= 0 && pos < fullText.length()) {
        return fullText.mid(pos);
    }
    return QString();
}

QString MindMapTextItem::textBeforeCursor() const {
    QTextCursor cursor = textCursor();
    int pos = cursor.position();
    QString fullText = toPlainText();
    if (pos > 0 && pos <= fullText.length()) {
        return fullText.left(pos);
    }
    return QString();
}

void MindMapTextItem::removeTextAfterCursor() {
    QTextCursor cursor = textCursor();
    int pos = cursor.position();
    QString fullText = toPlainText();
    if (pos >= 0 && pos < fullText.length()) {
        cursor.setPosition(pos);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}

bool MindMapTextItem::cursorAtEnd() const {
    QTextCursor cursor = textCursor();
    return cursor.position() == toPlainText().length();
}

bool MindMapTextItem::cursorAtStart() const {
    QTextCursor cursor = textCursor();
    return cursor.position() == 0;
}

bool MindMapTextItem::sceneEvent(QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                if (node->isEditing()) {
                    bool shift = (keyEvent->key() == Qt::Key_Backtab)
                    || (keyEvent->modifiers() & Qt::ShiftModifier);
                    if (shift) {
                        event->accept();
                        textCursor().insertText("\t");
                    } else {
                        event->accept();
                        node->finishEdit();
                        if (node->scene() && !node->scene()->views().isEmpty()) {
                            if (auto* view = dynamic_cast<MindMapView*>(node->scene()->views().first())) {
                                view->notifyGuideEditingExitedByKey();
                            }
                        }
                    }
                    return true;
                }
            }
        }
    }
    return QGraphicsTextItem::sceneEvent(event);
}

void MindMapTextItem::focusInEvent(QFocusEvent* event) {
    QGraphicsTextItem::focusInEvent(event);
}

void MindMapTextItem::focusOutEvent(QFocusEvent* event) {
    QGraphicsTextItem::focusOutEvent(event);
}

void MindMapTextItem::inputMethodEvent(QInputMethodEvent* event) {
    QGraphicsTextItem::inputMethodEvent(event);
}

QVariant MindMapTextItem::inputMethodQuery(Qt::InputMethodQuery query) const {
    return QGraphicsTextItem::inputMethodQuery(query);
}

void MindMapTextItem::mergeCurrentCharFormat(const QTextCharFormat& format) {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        QTextCharFormat merged = cursor.charFormat();
        merged.merge(format);
        cursor.setCharFormat(merged);
        setTextCursor(cursor);
    } else {
        cursor.mergeCharFormat(format);
        setTextCursor(cursor);
    }
}

void MindMapTextItem::setFixedTextWidth(qreal width) {
    if (m_fixedTextWidth != width) {
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) node->invalidateSerializationCache();
    }
    m_fixedTextWidth = width;
    if (width > 0) {
        setTextWidth(width);
        // 【修复】不调用 adjustSize()，避免文档 size() 被收缩为内容实际宽度
        // 这样 boundingRect() 会保持与 textWidth 一致
    } else {
        setTextWidth(-1);
        document()->adjustSize();
    }
    prepareGeometryChange();
    update();
}

// 【新增】重写 paint，禁用 Qt 自动焦点框（我们自己画）
void MindMapTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QStyleOptionGraphicsItem opt = *option;
    // 移除焦点状态，避免 Qt 在 boundingRect() 范围内绘制窄焦点框
    opt.state &= ~QStyle::State_HasFocus;
    QGraphicsTextItem::paint(painter, &opt, widget);
}

qreal MindMapTextItem::effectiveWidth() const {
    if (hasFixedWidth()) {
        return m_fixedTextWidth;
    }
    return boundingRect().width();
}

void MindMapTextItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
            if (!node->isEditing()) {
                for (auto* block : node->blocks()) {
                    if (block->type() == BlockType::Text) {
                        auto* tb = static_cast<TextBlock*>(block);
                        if (tb->textItem() == this) {
                            node->startEditBlock(tb);
                            break;
                        }
                    }
                }
                QGraphicsTextItem::mousePressEvent(event);
                return;
            }
            // 【修改】合并状态下只有一个文本块，不需要切换块
            // 只需将焦点设置到点击位置
            if (node->editingTextBlock() && node->editingTextBlock()->textItem() != this) {
                for (auto* block : node->blocks()) {
                    if (block->type() == BlockType::Text) {
                        auto* textBlock = static_cast<TextBlock*>(block);
                        if (textBlock->textItem() == this) {
                            node->startEditBlock(textBlock);
                            break;
                        }
                    }
                }
                QGraphicsTextItem::mousePressEvent(event);
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton) {
        const QPointF clickPos = event->pos();
        QTextDocument* doc = document();
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            QTextLayout* layout = block.layout();
            if (!layout) continue;
            const QPointF blockOrigin = doc->documentLayout()->blockBoundingRect(block).topLeft();
            for (int lineIndex = 0; lineIndex < layout->lineCount(); ++lineIndex) {
                const QTextLine line = layout->lineAt(lineIndex);
                const QRectF lineRect(blockOrigin.x() + line.x(),
                                      blockOrigin.y() + line.y(),
                                      line.width(), line.height());
                if (!lineRect.contains(clickPos)) continue;
                const int lineEnd = line.textStart() + line.textLength();
                const qreal textEndX = blockOrigin.x() + line.x() + line.cursorToX(lineEnd);
                if (clickPos.x() > textEndX) {
                    QTextCursor cursor(doc);
                    cursor.setVisualNavigation(true);
                    cursor.setPosition(block.position() + line.textStart());
                    cursor.movePosition(QTextCursor::EndOfLine);
                    setTextCursor(cursor);
                    event->accept();
                    return;
                }
                break;
            }
        }
    }
    QGraphicsTextItem::mousePressEvent(event);
}

void MindMapTextItem::keyPressEvent(QKeyEvent* event) {
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_V) {
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            QTextCursor cursor = textCursor();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                const MindMapStyle style = node->styleForCurrentLevel();
                QTextCharFormat format;
                format.setFont(style.font());
                format.setForeground(style.textColor);
                cursor.insertText(text, format);
            } else {
                cursor.insertText(text);
            }
            setTextCursor(cursor);
        }
        event->accept();
        return;
    }
    // 【新增】富文本编辑快捷键
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_U: {
            // 下划线
            event->accept();
            QTextCursor cursor = textCursor();
            if (!cursor.hasSelection()) {
                QTextCharFormat fmt;
                fmt.setFontUnderline(!cursor.charFormat().fontUnderline());
                mergeCurrentCharFormat(fmt);
            } else {
                QTextCharFormat fmt;
                fmt.setFontUnderline(!cursor.charFormat().fontUnderline());
                cursor.mergeCharFormat(fmt);
                setTextCursor(cursor);
            }
            return;
        }
        case Qt::Key_L: {
            // 左对齐
            event->accept();
            QTextCursor cursor = textCursor();
            QTextBlockFormat blockFmt = cursor.blockFormat();
            blockFmt.setAlignment(Qt::AlignLeft);
            cursor.setBlockFormat(blockFmt);
            setTextCursor(cursor);
            update();
            return;
        }
        case Qt::Key_E: {
            // 居中对齐
            event->accept();
            QTextCursor cursor = textCursor();
            QTextBlockFormat blockFmt = cursor.blockFormat();
            blockFmt.setAlignment(Qt::AlignCenter);
            cursor.setBlockFormat(blockFmt);
            setTextCursor(cursor);
            update();
            return;
        }
        case Qt::Key_R: {
            // 右对齐
            event->accept();
            QTextCursor cursor = textCursor();
            QTextBlockFormat blockFmt = cursor.blockFormat();
            blockFmt.setAlignment(Qt::AlignRight);
            cursor.setBlockFormat(blockFmt);
            setTextCursor(cursor);
            update();
            return;
        }
        case Qt::Key_J: {
            // 两端对齐
            event->accept();
            QTextCursor cursor = textCursor();
            QTextBlockFormat blockFmt = cursor.blockFormat();
            blockFmt.setAlignment(Qt::AlignJustify);
            cursor.setBlockFormat(blockFmt);
            setTextCursor(cursor);
            update();
            return;
        }
        default:
            break;
        }
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ShiftModifier)) {
        event->accept();
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
            if (auto* editBlock = node->editingTextBlock()) {
                // 【修改】合并状态下，Shift+Enter 插入段落分隔
                if (!(event->modifiers() & Qt::ControlModifier)) {
                    QTextCursor cursor = textCursor();
                    cursor.insertBlock();
                    setTextCursor(cursor);
                }
            }
        }
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
            node->finishEdit();
            if (node->scene() && !node->scene()->views().isEmpty()) {
                if (auto* view = dynamic_cast<MindMapView*>(node->scene()->views().first())) {
                    view->notifyGuideEditingExitedByKey();
                }
            }
        }
        return;
    }

    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
        && (event->modifiers() & Qt::ControlModifier)) {
        event->accept();
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
            if (event->key() == Qt::Key_Up) {
                node->focusPrevBlock();
            } else {
                node->focusNextBlock();
            }
        }
        return;
    }

    if (event->key() == Qt::Key_Right && !(event->modifiers() & Qt::ControlModifier)) {
        if (cursorAtEnd()) {
            event->accept();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                node->focusNextBlock();
            }
            return;
        }
    }
    if (event->key() == Qt::Key_Left && !(event->modifiers() & Qt::ControlModifier)) {
        if (cursorAtStart()) {
            event->accept();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                node->focusPrevBlock();
            }
            return;
        }
    }

    if (event->key() == Qt::Key_Down && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = textCursor();
        int oldPos = cursor.position();
        cursor.movePosition(QTextCursor::Down);
        if (cursor.position() == oldPos) {
            event->accept();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                node->focusNextBlock();
            }
            return;
        }
    }
    if (event->key() == Qt::Key_Up && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = textCursor();
        int oldPos = cursor.position();
        cursor.movePosition(QTextCursor::Up);
        if (cursor.position() == oldPos) {
            event->accept();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                node->focusPrevBlock();
            }
            return;
        }
    }

    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        bool shift = (event->key() == Qt::Key_Backtab) || (event->modifiers() & Qt::ShiftModifier);
        if (shift) {
            event->accept();
            textCursor().insertText("\t");
        } else {
            event->accept();
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                node->finishEdit();
            }
        }
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        event->accept();
        if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
            node->cancelEdit();
        }
        return;
    }

    if (event->key() == Qt::Key_Backspace && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = textCursor();
        if (!cursor.hasSelection() && cursorAtStart()) {
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                if (auto* editBlock = node->editingTextBlock()) {
                    // 【修改】合并状态下，在段落开头按 Backspace 合并到上一段落
                    if (node->mergeTextBlockWithPrevious(editBlock)) {
                        event->accept();
                        return;
                    }
                }
            }
        }
    }

    if (event->key() == Qt::Key_Delete && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = textCursor();
        if (!cursor.hasSelection() && cursorAtEnd()) {
            if (auto* node = dynamic_cast<MindMapNode*>(parentItem())) {
                if (auto* editBlock = node->editingTextBlock()) {
                    // 【修改】合并状态下，在段落末尾按 Delete 合并到下一段落
                    if (node->mergeTextBlockWithNext(editBlock)) {
                        event->accept();
                        return;
                    }
                }
            }
        }
    }

    QGraphicsTextItem::keyPressEvent(event);
}

// ==================== TextBlock ====================

TextBlock::TextBlock(MindMapNode* parent)
    : m_textItem(new MindMapTextItem(parent)) {
    m_parentNode = parent;
    m_textItem->setDefaultTextColor(QColor(5, 56, 96));
    m_textItem->setAcceptedMouseButtons(Qt::NoButton);
    m_textItem->setTextInteractionFlags(Qt::NoTextInteraction);
    QObject::connect(m_textItem->document(), &QTextDocument::contentsChanged,
                     [this]() {
                         if (m_parentNode) {
                             m_parentNode->invalidateNodeSize();
                             m_parentNode->invalidateSerializationCache();
                         }
                     });
}

TextBlock::~TextBlock() {
    if (m_textItem) {
        if (m_textItem->scene()) m_textItem->scene()->removeItem(m_textItem);
        delete m_textItem;
    }
}

qreal TextBlock::height() const {
    if (!m_textItem) return 0;
    return m_textItem->boundingRect().height();
}

qreal TextBlock::width() const {
    if (!m_textItem) return 0;
    return m_textItem->effectiveWidth();
}

void TextBlock::setBlockPos(const QPointF& pos) {
    if (m_textItem) m_textItem->setPos(pos);
}

QPointF TextBlock::blockPos() const {
    if (!m_textItem) return QPointF();
    return m_textItem->pos();
}

void TextBlock::setPlainText(const QString& text) {
    if (m_textItem) m_textItem->setPlainText(text);
}

QString TextBlock::toPlainText() const {
    return m_textItem ? m_textItem->toPlainText() : "";
}

void TextBlock::setHtml(const QString& html) {
    if (m_textItem) m_textItem->setHtml(html);
}

QString TextBlock::toHtml() const {
    return m_textItem ? m_textItem->toHtml() : "";
}

void TextBlock::setEditing(bool editing) {
    m_isEditing = editing;
    if (!m_textItem) return;

    if (editing) {
        m_textItem->setTextInteractionFlags(Qt::TextEditorInteraction);
        m_textItem->setAcceptedMouseButtons(Qt::LeftButton);
        m_textItem->setCursor(Qt::IBeamCursor);
        m_textItem->setFocus();
    } else {
        // 结束编辑时文本项会失焦，QGraphicsView 会把视图滚动到该项。若正在编辑的
        // 节点已在视野之外（用户先滚走了再点空白/点其它节点），这一下被动滚屏就
        // 表现为「视野跳变」。这里在结束编辑前后保存并恢复滚动位置。
        QVector<QPair<QGraphicsView*, QPoint>> savedScroll;
        if (QGraphicsScene* itemScene = m_textItem->scene()) {
            for (QGraphicsView* v : itemScene->views()) {
                savedScroll.append({v, QPoint(v->horizontalScrollBar()->value(),
                                              v->verticalScrollBar()->value())});
            }
        }
        QTextCursor cursor = m_textItem->textCursor();
        cursor.clearSelection();
        m_textItem->setTextCursor(cursor);
        m_textItem->setTextInteractionFlags(Qt::NoTextInteraction);
        m_textItem->setAcceptedMouseButtons(Qt::NoButton);
        m_textItem->clearFocus();
        m_textItem->unsetCursor();
        for (const auto& entry : savedScroll) {
            entry.first->horizontalScrollBar()->setValue(entry.second.x());
            entry.first->verticalScrollBar()->setValue(entry.second.y());
        }
    }
}

void TextBlock::setCursorAtEnd() {
    if (!m_textItem) return;
    QTextCursor cursor = m_textItem->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_textItem->setTextCursor(cursor);
}

void TextBlock::setCursorAtStart() {
    if (!m_textItem) return;
    QTextCursor cursor = m_textItem->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_textItem->setTextCursor(cursor);
}

void TextBlock::insertText(const QString& text) {
    if (m_textItem) m_textItem->textCursor().insertText(text);
}

// 【新增】光标位置管理
int TextBlock::cursorPosition() const {
    if (!m_textItem) return 0;
    return m_textItem->textCursor().position();
}

void TextBlock::setCursorPosition(int pos) {
    if (!m_textItem) return;
    QTextCursor cursor = m_textItem->textCursor();
    cursor.setPosition(qBound(0, pos, m_textItem->toPlainText().length()));
    m_textItem->setTextCursor(cursor);
}

TextBlock* TextBlock::splitAtCursor() {
    return splitAtCursorPreservingFormats();
}

void TextBlock::copyParagraphFragments(QTextCursor& dst, const QTextBlock& srcBlock,
                                       const QTextDocument* srcDoc, int startOffset, int endOffset) {
    if (!srcDoc) return;
    const int paraStart = srcBlock.position();
    const int paraLength = srcBlock.length() - 1;  // 不含块分隔符
    if (endOffset < 0 || endOffset > paraLength) endOffset = paraLength;
    if (startOffset < 0) startOffset = 0;
    if (startOffset >= endOffset) return;

    for (QTextBlock::iterator it = srcBlock.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) continue;
        const int fragStart = fragment.position() - paraStart;
        const int fragEnd = fragStart + fragment.length();
        if (fragEnd <= startOffset || fragStart >= endOffset) continue;

        QTextCharFormat format = fragment.charFormat();
        if (format.isImageFormat()) {
            // 文本里的内嵌图片按资源名引用，必须把资源一并带到目标文档
            const QTextImageFormat imageFormat = format.toImageFormat();
            if (dst.document()) {
                const QVariant resource =
                    srcDoc->resource(QTextDocument::ImageResource, QUrl(imageFormat.name()));
                if (resource.isValid()) {
                    dst.document()->addResource(QTextDocument::ImageResource,
                                                QUrl(imageFormat.name()), resource);
                }
            }
            dst.insertImage(imageFormat);
            continue;
        }
        const int from = qMax(startOffset, fragStart) - fragStart;
        const int to = qMin(endOffset, fragEnd) - fragStart;
        if (to <= from) continue;
        dst.insertText(fragment.text().mid(from, to - from), format);
    }
}

void TextBlock::copyParagraph(QTextCursor& dst, const QTextBlock& srcBlock, const QTextDocument* srcDoc,
                              bool targetIsFresh, bool joinWithPrevious,
                              int startOffset, int endOffset) {
    if (!srcDoc) return;
    if (!joinWithPrevious) {
        const QTextBlockFormat srcFormat = srcBlock.blockFormat();
        if (targetIsFresh) {
            // 目标文档只有唯一的空段落：直接复用它，避免多出空行
            dst.setBlockFormat(srcFormat);
        } else {
            dst.insertBlock(srcFormat);
        }
    }
    copyParagraphFragments(dst, srcBlock, srcDoc, startOffset, endOffset);
}

void TextBlock::copyParagraphs(QTextCursor& dst, const QTextDocument* srcDoc, bool targetIsFresh,
                               bool joinFirst) {
    if (!srcDoc) return;
    bool fresh = targetIsFresh;
    bool join = joinFirst;
    const int count = srcDoc->blockCount();
    for (int p = 0; p < count; ++p) {
        copyParagraph(dst, srcDoc->findBlockByNumber(p), srcDoc, fresh, join);
        fresh = false;
        join = false;
    }
}

TextBlock* TextBlock::splitAtCursorPreservingFormats() {
    if (!m_textItem) return nullptr;
    QTextDocument* doc = m_textItem->document();
    if (!doc) return nullptr;

    const int cursorPos = qBound(0, m_textItem->textCursor().position(), doc->characterCount() - 1);
    QTextBlock para = doc->findBlock(cursorPos);
    if (!para.isValid()) return nullptr;
    const int offsetInPara = cursorPos - para.position();

    auto* part = new TextBlock(m_parentNode);
    QTextDocument* partDoc = part->textItem()->document();
    // 继承默认字体/字色：没有显式字符格式的文字（编辑时新敲入的内容）保持原样
    partDoc->setDefaultFont(doc->defaultFont());
    part->textItem()->setDefaultTextColor(m_textItem->defaultTextColor());

    QTextCursor dst(partDoc);
    dst.movePosition(QTextCursor::Start);
    // 1) 拆分段落的后半部分（沿用原段落的 QTextBlockFormat）
    copyParagraph(dst, para, doc, true, false, offsetInPara);
    // 2) 其后的段落整段复制
    for (QTextBlock next = para.next(); next.isValid(); next = next.next()) {
        copyParagraph(dst, next, doc, false);
    }

    // 3) 原块只保留前半部分
    QTextCursor cut(doc);
    cut.setPosition(cursorPos);
    cut.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cut.removeSelectedText();
    if (offsetInPara == 0 && doc->blockCount() > 1) {
        // 光标在段首时，末尾会残留一个空段落；删掉它的块分隔符，
        // 让前半部分正好结束在上一段（空段本身作为“上一段”被保留）。
        QTextCursor tail(doc);
        tail.movePosition(QTextCursor::End);
        tail.deletePreviousChar();
    }

    QTextCursor back(doc);
    back.setPosition(qMin(cursorPos, doc->characterCount() - 1));
    m_textItem->setTextCursor(back);
    return part;
}

void TextBlock::mergeWith(TextBlock* other) {
    if (!other || !m_textItem || !other->m_textItem) return;
    // 首段接在本块最后一行末尾（与 QTextDocumentFragment 的语义一致），
    // 其余段落按各自的 QTextBlockFormat 插入，段落/字符格式都不丢。
    QTextCursor cursor(m_textItem->document());
    cursor.movePosition(QTextCursor::End);
    copyParagraphs(cursor, other->textItem()->document(), false, true);
}

bool TextBlock::isEmpty() const {
    return toPlainText().trimmed().isEmpty();
}

// ==================== FormulaPixmapItem ====================

FormulaPixmapItem::FormulaPixmapItem(const QPixmap& pixmap, MindMapNode* parentNode)
    : QGraphicsPixmapItem(pixmap, parentNode), m_parentNode(parentNode) {
    setAcceptHoverEvents(true);
    setCursor(Qt::PointingHandCursor);
    setTransformationMode(Qt::SmoothTransformation);
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
}

void FormulaPixmapItem::setFormulaSelected(bool selected) {
    if (m_isSelected == selected) return;
    m_isSelected = selected;
    update();
}

void FormulaPixmapItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_parentNode && m_parentNode->scene()) {
            for (auto* item : m_parentNode->scene()->items()) {
                if (auto* node = dynamic_cast<MindMapNode*>(item)) {
                    if (node != m_parentNode) {
                        node->setNodeSelected(false);
                        node->clearBlockSelection();
                    }
                }
            }
            m_parentNode->setNodeSelected(true);
            for (auto* block : m_parentNode->blocks()) {
                if (block != m_formulaBlock && block->isBlockSelected()) {
                    block->setBlockSelected(false);
                }
            }
            m_parentNode->clearBlockSelection();

            if (m_formulaBlock) {
                m_parentNode->setSelectedBlock(m_formulaBlock);
            }
        }
        setFormulaSelected(true);

        m_isDragging = true;
        m_dragStarted = false;
        m_dragBeforeBlocks.clear();
        m_dragStartPos = event->pos();
        if (m_formulaBlock) {
            m_originalBlockIndex = m_parentNode->blockIndex(m_formulaBlock);
        }

        event->accept();
        return;
    }
    if (m_parentNode) {
        m_parentNode->handleChildPressEvent(event);
    } else {
        QGraphicsPixmapItem::mousePressEvent(event);
    }
}

void FormulaPixmapItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isDragging && m_formulaBlock && m_parentNode) {
        if (!m_dragStarted && QLineF(m_dragStartPos, event->pos()).length() >= QApplication::startDragDistance()) {
            m_dragBeforeBlocks = m_parentNode->splitTextBlocksForMediaDrag();
            m_dragStarted = true;
        }
        if (!m_dragStarted) {
            event->accept();
            return;
        }
        QPointF nodePos = mapToParent(event->pos());
        int newIndex = m_parentNode->calculateBlockInsertIndex(nodePos.y());
        m_parentNode->showDragIndicator(newIndex);
        setOpacity(0.6);
        event->accept();
        return;
    }
    if (m_parentNode) {
        m_parentNode->handleChildMoveEvent(event);
    } else {
        QGraphicsPixmapItem::mouseMoveEvent(event);
    }
}

void FormulaPixmapItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isDragging) {
        m_isDragging = false;
        setOpacity(1.0);

        if (!m_dragStarted && QLineF(m_dragStartPos, event->pos()).length() >= QApplication::startDragDistance() &&
            m_formulaBlock && m_parentNode) {
            m_dragBeforeBlocks = m_parentNode->splitTextBlocksForMediaDrag();
            m_dragStarted = true;
        }

        if (m_dragStarted && m_formulaBlock && m_parentNode) {
            QPointF nodePos = mapToParent(event->pos());
            int newIndex = m_parentNode->calculateBlockInsertIndex(nodePos.y());
            m_parentNode->hideDragIndicator();

            int currentIndex = m_parentNode->blockIndex(m_formulaBlock);
            if (currentIndex >= 0 && newIndex >= 0 && newIndex <= m_parentNode->blocks().size() &&
                !(newIndex > currentIndex && newIndex - 1 == currentIndex))
                m_parentNode->moveBlock(currentIndex, newIndex);
            m_parentNode->mergeAdjacentTextBlocksForMediaDrag();
            QVector<MindMapBlock*> afterBlocks = m_parentNode->blocks();
            QVector<MindMapBlock*> transientBlocks = m_parentNode->takeMediaDragTransientBlocks();
            if (MindMapNode::s_undoStack && m_parentNode->scene()) {
                MindMapNode::s_undoStack->push(new MoveMediaBlockCommand(
                    m_parentNode, m_formulaBlock, m_dragBeforeBlocks, afterBlocks, transientBlocks, m_parentNode->scene()));
            } else {
                const QVector<MindMapBlock*> current = m_parentNode->blocks();
                for (MindMapBlock* block : m_dragBeforeBlocks)
                    if (!current.contains(block) && block->type() == BlockType::Text) delete block;
                for (MindMapBlock* block : transientBlocks)
                    if (!current.contains(block) && block->type() == BlockType::Text) delete block;
            }
        }
        m_dragStarted = false;
        m_dragBeforeBlocks.clear();

        event->accept();
        return;
    }

    if (m_parentNode) {
        m_parentNode->handleChildReleaseEvent(event);
    } else {
        QGraphicsPixmapItem::mouseReleaseEvent(event);
    }
}

void FormulaPixmapItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsPixmapItem::paint(painter, option, widget);
    if (m_isSelected) {
        QRectF r = boundingRect();
        QRectF selectRect = r.adjusted(-2, -2, 2, 2);
        QPen pen(QColor(0, 120, 255));
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(selectRect);

        qreal hs = HANDLE_SIZE;
        auto drawHandle = [&](const QPointF& center) {
            QRectF hr(center.x() - hs/2, center.y() - hs/2, hs, hs);
            painter->fillRect(hr, QColor(0, 120, 255));
            painter->setPen(QPen(Qt::white, 1));
            painter->drawRect(hr);
        };
        drawHandle(r.topLeft());
        drawHandle(r.topRight());
        drawHandle(r.bottomLeft());
        drawHandle(r.bottomRight());
    }
}

void FormulaPixmapItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    if (m_isSelected) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::PointingHandCursor);
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void FormulaPixmapItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (m_parentNode) {
        for (int i = 0; i < m_parentNode->blocks().size(); ++i) {
            auto* block = m_parentNode->blocks()[i];
            if (block->type() == BlockType::Formula) {
                auto* fb = static_cast<FormulaBlock*>(block);
                if (fb->pixmapItem() == this) {
                    fb->editFormula();
                    break;
                }
            }
        }
    }
    event->accept();
}

void FormulaPixmapItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* editAction = menu.addAction("编辑公式");
    QAction* deleteAction = menu.addAction("删除公式");
    QAction* selected = menu.exec(event->screenPos());

    if (m_parentNode) {
        int index = -1;
        for (int i = 0; i < m_parentNode->blocks().size(); ++i) {
            auto* block = m_parentNode->blocks()[i];
            if (block->type() == BlockType::Formula) {
                auto* fb = static_cast<FormulaBlock*>(block);
                if (fb->pixmapItem() == this) {
                    index = i;
                    break;
                }
            }
        }

        if (selected == editAction && index >= 0) {
        } else if (selected == deleteAction && index >= 0) {
            if (MindMapNode::s_undoStack && m_parentNode->scene()) {
                MindMapNode::s_undoStack->push(new RemoveBlockCommand(
                    m_parentNode, index, m_parentNode->scene()));
            } else {
                m_parentNode->removeBlockAt(index);
                m_parentNode->autoFitTextBlockWidths();
                m_parentNode->calcNodeSize();
                MindMapNode* root = m_parentNode->getRoot();
                if (root) {
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                }
                if (m_parentNode->scene()) m_parentNode->scene()->update();
            }
        }
    }
    event->accept();
}

void FormulaPixmapItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = true;
    QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect();
    effect->setBlurRadius(8);
    effect->setColor(QColor(0, 120, 255, 180));
    effect->setOffset(0, 0);
    setGraphicsEffect(effect);
    update();
    QGraphicsPixmapItem::hoverEnterEvent(event);
}

void FormulaPixmapItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = false;
    setGraphicsEffect(nullptr);
    unsetCursor();
    update();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}

// ==================== FormulaBlock ====================

FormulaBlock::FormulaBlock(const QPixmap& pixmap, const QString& latex, MindMapNode* parent)
    : m_latexCode(latex), m_isSelected(false) {
    m_parentNode = parent;
    m_pixmapItem = new FormulaPixmapItem(pixmap, parent);
    m_pixmapItem->setZValue(10);
}

FormulaBlock::~FormulaBlock() {
    if (m_pixmapItem) {
        if (m_pixmapItem->parentItem()) {
            if (m_pixmapItem->scene()) m_pixmapItem->scene()->removeItem(m_pixmapItem);
        } else {
            if (m_pixmapItem->scene()) m_pixmapItem->scene()->removeItem(m_pixmapItem);
            delete m_pixmapItem;
        }
    }
}

qreal FormulaBlock::height() const {
    if (!m_pixmapItem) return 0;
    return m_pixmapItem->boundingRect().height() + 4;
}

qreal FormulaBlock::width() const {
    if (!m_pixmapItem) return 0;
    return m_pixmapItem->boundingRect().width();
}

void FormulaBlock::setBlockPos(const QPointF& pos) {
    if (m_pixmapItem) m_pixmapItem->setPos(pos);
}

QPointF FormulaBlock::blockPos() const {
    if (!m_pixmapItem) return QPointF();
    return m_pixmapItem->pos();
}

void FormulaBlock::setBlockSelected(bool selected) {
    m_isSelected = selected;
    if (m_pixmapItem) {
        m_pixmapItem->setOpacity(selected ? 0.8 : 1.0);
        m_pixmapItem->setFormulaSelected(selected);
    }
}
void FormulaBlock::editFormula() {
    if (!m_parentNode) return;
    m_parentNode->setBlockFocusOut(true);

    QString oldLatex = m_latexCode;
    QPixmap oldPixmap = m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap();
    int blockIdx = m_parentNode->blockIndex(this);

    MindMapFormulaDialog dialog;
    dialog.setFormula(m_latexCode);

    if (dialog.exec() == QDialog::Accepted) {
        QString newLatex = dialog.formula();
        if (newLatex != m_latexCode && !newLatex.trimmed().isEmpty()) {
            QImage image = MindMapFormulaDialog::renderLatexToImage(newLatex, 16);
            if (!image.isNull()) {
                QPixmap newPixmap = QPixmap::fromImage(image);
                newPixmap.setDevicePixelRatio(image.devicePixelRatio());
                if (MindMapNode::s_undoStack && m_parentNode->scene() && blockIdx >= 0) {
                    MindMapNode::s_undoStack->push(new EditFormulaCommand(
                        m_parentNode, blockIdx, oldLatex, newLatex, oldPixmap, newPixmap, m_parentNode->scene()));
                } else {
                    updatePixmap(newPixmap);
                    setLatexCode(newLatex);
                    if (m_parentNode) m_parentNode->calcNodeSize();
                }
            }
        }
    }

    m_parentNode->setBlockFocusOut(false);
    if (m_parentNode->editingTextBlock()) {
        m_parentNode->editingTextBlock()->textItem()->setFocus();
    }
}

void FormulaBlock::updatePixmap(const QPixmap& pixmap) {
    if (m_pixmapItem) {
        m_pixmapItem->setPixmap(pixmap);
        if (m_parentNode) m_parentNode->invalidateNodeSize();
    }
}

void FormulaBlock::setLatexCode(const QString& latex) {
    if (m_latexCode == latex) return;
    m_latexCode = latex;
    if (m_parentNode) m_parentNode->invalidateSerializationCache();
}

// ==================== ImageBlock ====================

ImageBlock::ImageBlock(const QImage& image, MindMapNode* parent)
    : m_isSelected(false), m_isDragging(false), m_isResizing(false) {
    m_parentNode = parent;
    m_imageItem = new MindMapImageItem(image, parent);
    m_imageItem->setZValue(5);
}

ImageBlock::~ImageBlock() {
    if (m_imageItem) {
        if (m_imageItem->parentItem()) {
            if (m_imageItem->scene()) m_imageItem->scene()->removeItem(m_imageItem);
        } else {
            if (m_imageItem->scene()) m_imageItem->scene()->removeItem(m_imageItem);
            delete m_imageItem;
        }
    }
}

qreal ImageBlock::height() const {
    if (!m_imageItem) return 0;
    return m_imageItem->boundingRect().height() + 4;
}

qreal ImageBlock::width() const {
    if (!m_imageItem) return 0;
    return m_imageItem->boundingRect().width();
}

void ImageBlock::setBlockPos(const QPointF& pos) {
    if (m_imageItem) m_imageItem->setPos(pos);
}

QPointF ImageBlock::blockPos() const {
    if (!m_imageItem) return QPointF();
    return m_imageItem->pos();
}

void ImageBlock::setBlockSelected(bool selected) {
    m_isSelected = selected;
    if (m_imageItem) m_imageItem->setImageSelected(selected);
}

QString ImageBlock::imageName() const {
    return m_imageItem ? m_imageItem->imageName() : "";
}

void ImageBlock::setImageName(const QString& name) {
    if (m_imageItem) m_imageItem->setImageName(name);
}

QImage ImageBlock::originalImage() const {
    return m_imageItem ? m_imageItem->originalImage() : QImage();
}

void ImageBlock::setImageSize(const QSizeF& size) {
    if (m_imageItem) {
        m_imageItem->setImageSize(size);
        if (m_parentNode) m_parentNode->invalidateNodeSize();
    }
}

void ImageBlock::startDrag() {
    m_isDragging = true;
    if (m_imageItem) m_imageItem->setOpacity(0.7);
}

void ImageBlock::endDrag() {
    m_isDragging = false;
    if (m_imageItem) m_imageItem->setOpacity(1.0);
}

// ===== 修复：不完整类型指针转换 =====
QGraphicsItem* FormulaBlock::graphicsItem() { return m_pixmapItem; }
QGraphicsItem* ImageBlock::graphicsItem() { return m_imageItem; }
