#pragma once

#include <QGraphicsItem>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QTextCursor>
#include <QVector>

class MindMapNode;
class MindMapImageItem;
class FormulaPixmapItem;
class QTextBlock;
class QTextDocument;

// ============================================================
// Block 系统：节点内的内容分块管理
// ============================================================

enum class BlockType { Text, Formula, Image };

// 前向声明
class TextBlock;
class FormulaBlock;
class ImageBlock;

// Block 基类
class MindMapBlock {
public:
    virtual ~MindMapBlock() = default;
    virtual BlockType type() const = 0;
    virtual qreal height() const = 0;
    virtual qreal width() const = 0;
    virtual void setBlockPos(const QPointF& pos) = 0;
    virtual QPointF blockPos() const = 0;
    virtual QGraphicsItem* graphicsItem() = 0;
    virtual void setBlockSelected(bool selected) {}
    virtual bool isBlockSelected() const { return false; }

    // 获取父节点
    MindMapNode* parentNode() const { return m_parentNode; }
    void setParentNode(MindMapNode* node) { m_parentNode = node; }

    // 用于布局的缓存高度
    void setCachedHeight(qreal h) { m_cachedHeight = h; }
    qreal cachedHeight() const { return m_cachedHeight; }

protected:
    MindMapNode* m_parentNode = nullptr;
    qreal m_cachedHeight = 0;
};

// ============================================================
// 自定义文本项：在编辑模式下拦截 Enter/Esc/Tab
// 宽度由节点统一管理，不再支持边缘拖拽调整
// ============================================================
class MindMapTextItem : public QGraphicsTextItem {
public:
    explicit MindMapTextItem(QGraphicsItem* parent = nullptr);

    // 分块支持：在光标处拆分文本块
    QString textAfterCursor() const;
    QString textBeforeCursor() const;
    void removeTextAfterCursor();

    // 检查光标是否在文本末尾
    bool cursorAtEnd() const;
    // 检查光标是否在文本开头
    bool cursorAtStart() const;

    // 【新增】合并当前字符格式（用于加粗/斜体/颜色）
    void mergeCurrentCharFormat(const QTextCharFormat& format);

    // 设置/获取固定宽度（由节点统一设置，不再支持用户拖拽）
    void setFixedTextWidth(qreal width);
    qreal fixedTextWidth() const { return m_fixedTextWidth; }
    bool hasFixedWidth() const { return m_fixedTextWidth > 0; }
    // 获取有效宽度（固定宽度或内容宽度）
    qreal effectiveWidth() const;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;


protected:
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    bool sceneEvent(QEvent* event) override;

private:
    qreal m_fixedTextWidth = -1;  // -1 表示使用自动宽度（无固定宽度）
};

// ============================================================
// 文本块
// ============================================================
class TextBlock : public MindMapBlock {
public:
    explicit TextBlock(MindMapNode* parent);
    ~TextBlock();
    BlockType type() const override { return BlockType::Text; }
    qreal height() const override;
    qreal width() const override;
    void setBlockPos(const QPointF& pos) override;
    QPointF blockPos() const override;
    QGraphicsItem* graphicsItem() override { return m_textItem; }

    void setPlainText(const QString& text);
    QString toPlainText() const;
    void setHtml(const QString& html);
    QString toHtml() const;

    void setEditing(bool editing);
    bool isEditing() const { return m_isEditing; }

    void setCursorAtEnd();
    void setCursorAtStart();
    void insertText(const QString& text);

    // 【新增】光标位置管理（用于合并/拆分后恢复光标）
    int cursorPosition() const;
    void setCursorPosition(int pos);

    // 在光标处拆分：返回后半部分的新文本块
    TextBlock* splitAtCursor();
    // 与下一个文本块合并（将 other 的内容追加到本块末尾）
    void mergeWith(TextBlock* other);

    // 在光标处无损拆分：前半部分留在本块，返回承载后半部分的新块。
    // 段落格式（对齐/行距/缩进）与字符格式都按原文档逐段复制，不走 HTML 往返。
    TextBlock* splitAtCursorPreservingFormats();

    // —— 段落级格式的无损复制 ——
    // QTextDocumentFragment/selection().toHtml() 只带 margin / -qt-block-indent /
    // text-indent，会丢掉 align、line-height 等段落级格式，因此这里逐段落、逐片段复制。
    // startOffset/endOffset 是段内字符偏移（相对段落文本，不含块分隔符），用于拆分。
    static void copyParagraphFragments(QTextCursor& dst, const QTextBlock& srcBlock,
                                       const QTextDocument* srcDoc,
                                       int startOffset = 0, int endOffset = -1);
    static void copyParagraph(QTextCursor& dst, const QTextBlock& srcBlock, const QTextDocument* srcDoc,
                              bool targetIsFresh, bool joinWithPrevious = false,
                              int startOffset = 0, int endOffset = -1);
    static void copyParagraphs(QTextCursor& dst, const QTextDocument* srcDoc, bool targetIsFresh,
                               bool joinFirst = false);

    MindMapTextItem* textItem() const { return m_textItem; }

    // 检查是否为空（无内容或只有空白）
    bool isEmpty() const;

private:
    MindMapTextItem* m_textItem;
    bool m_isEditing = false;
};

// ============================================================
// 公式块
// ============================================================
class FormulaBlock : public MindMapBlock {
public:
    explicit FormulaBlock(const QPixmap& pixmap, const QString& latex, MindMapNode* parent);
    ~FormulaBlock();
    BlockType type() const override { return BlockType::Formula; }
    qreal height() const override;
    qreal width() const override;
    void setBlockPos(const QPointF& pos) override;
    QPointF blockPos() const override;
    QGraphicsItem* graphicsItem() override;
    void setBlockSelected(bool selected) override;
    bool isBlockSelected() const override { return m_isSelected; }

    QString latexCode() const { return m_latexCode; }
    void setLatexCode(const QString& latex);
    FormulaPixmapItem* pixmapItem() const { return m_pixmapItem; }

    void editFormula(); // 弹出对话框编辑
    void updatePixmap(const QPixmap& pixmap);

private:
    FormulaPixmapItem* m_pixmapItem;
    QString m_latexCode;
    bool m_isSelected = false;
};

// ============================================================
// 图片块
// ============================================================
class ImageBlock : public MindMapBlock {
public:
    explicit ImageBlock(const QImage& image, MindMapNode* parent);
    ~ImageBlock();
    BlockType type() const override { return BlockType::Image; }
    qreal height() const override;
    qreal width() const override;
    void setBlockPos(const QPointF& pos) override;
    QPointF blockPos() const override;
    QGraphicsItem* graphicsItem() override;
    void setBlockSelected(bool selected) override;
    bool isBlockSelected() const override { return m_isSelected; }

    MindMapImageItem* imageItem() const { return m_imageItem; }
    QString imageName() const;
    void setImageName(const QString& name);
    QImage originalImage() const;
    void setImageSize(const QSizeF& size);

    // 拖动重排序支持
    void startDrag();
    void endDrag();
    bool isDragging() const { return m_isDragging; }

    // 缩放支持
    void setResizing(bool resizing) { m_isResizing = resizing; }
    bool isResizing() const { return m_isResizing; }

private:
    MindMapImageItem* m_imageItem;
    bool m_isSelected = false;
    bool m_isDragging = false;
    bool m_isResizing = false;
};

// ============================================================
// 公式图片项（用于显示渲染后的 LaTeX）
// ============================================================
class FormulaPixmapItem : public QGraphicsPixmapItem {
public:
    explicit FormulaPixmapItem(const QPixmap& pixmap, MindMapNode* parentNode);

    void setFormulaBlock(FormulaBlock* block) { m_formulaBlock = block; }
    FormulaBlock* formulaBlock() const { return m_formulaBlock; }
    void setFormulaSelected(bool selected);
    bool isFormulaSelected() const { return m_isSelected; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    MindMapNode* m_parentNode;
    FormulaBlock* m_formulaBlock = nullptr;
    bool m_isHovered = false;
    bool m_isSelected = false;
    bool m_isDragging = false;
    bool m_dragStarted = false;
    QPointF m_dragStartPos;
    int m_originalBlockIndex = -1;
    QVector<MindMapBlock*> m_dragBeforeBlocks;
    static constexpr qreal HANDLE_SIZE = 8;
};
