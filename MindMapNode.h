#pragma once

#include "MindMapBlock.h"
#include "MindMapStyle.h"
#include "MindMapNodeSizeCache.h"
#include <QJsonObject>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QVector>
#include <QKeyEvent>
#include <QFocusEvent>

class QGraphicsScene;
class QUndoStack;
class MindMapView;
class MindMapHoverTracker;

// ============================================================
// MindMapNode - 思维导图节点（Block 系统版本）
// ============================================================

class MindMapNode : public QGraphicsRectItem {
public:
    MindMapNode(const QString& text, MindMapNode* parent = nullptr);
    ~MindMapNode();

    QRectF boundingRect() const override;

    void addChild(MindMapNode* child);
    void insertChild(int index, MindMapNode* child);
    void removeChild(MindMapNode* child);

    MindMapNode* parentNode() const { return m_parent; }
    QVector<MindMapNode*> childNodes() const { return m_children; }

    // ===== Block 管理 =====
    void addBlock(MindMapBlock* block, int index = -1); // index=-1 表示末尾
    void removeBlock(MindMapBlock* block);
    void removeBlockAt(int index);
    MindMapBlock* takeBlockAt(int index);  // 【新增】只移除不删除，返回block指针
    void clearBlocks();
    void moveBlock(int fromIndex, int toIndex);
    void moveBlock(MindMapBlock* block, int newIndex);
    QVector<MindMapBlock*> blocks() const { return m_blocks; }
    int blockIndex(MindMapBlock* block) const;

    // 当前选中的块（图片或公式）
    MindMapBlock* selectedBlock() const { return m_selectedBlock; }
    void setSelectedBlock(MindMapBlock* block);
    void clearBlockSelection();
    void clearBlockSelectionAndUpdate();

    // 获取当前编辑的文本块
    TextBlock* editingTextBlock() const { return m_editingTextBlock; }
    void setEditingTextBlock(TextBlock* block) { m_editingTextBlock = block; }

    // 在指定块之后插入新块
    void insertBlockAfter(MindMapBlock* refBlock, MindMapBlock* newBlock);
    // 在指定块之前插入新块
    void insertBlockBefore(MindMapBlock* refBlock, MindMapBlock* newBlock);

    // 在光标处拆分文本块（Shift+Enter）
    void splitTextBlockAtCursor(TextBlock* block);

    // 合并文本块
    bool mergeTextBlockWithPrevious(TextBlock* block);
    bool mergeTextBlockWithNext(TextBlock* block);

    // 合并相邻的空文本块
    void mergeEmptyTextBlocks();

    // 获取所有文本内容（用于序列化/编辑备份）
    QString allText() const;
    QString allHtml() const;
    void setAllText(const QString& text);
    void setAllHtml(const QString& html);

    // 兼容旧接口
    QString nodeText() const;
    void setNodeText(const QString& text);
    QString nodeHtml() const;
    void setNodeHtml(const QString& html);
    // ===== 布局 =====
    void layoutTree(qreal x, qreal y);
    qreal calcRequiredHeight() const;
    void adjustLayout();
    void calcNodeSize();
    void invalidateNodeSize();
    void refreshNodeSizeIfNeeded();
    void updateBlockLayout();
    qreal horizontalTextMargin() const;
    qreal verticalTextMargin() const;

    // ===== 选中 / 编辑 =====
    void setNodeSelected(bool selected);
    bool isNodeSelected() const { return m_isSelected; }
    void startEdit();
    void startEditBlock(TextBlock* block);
    void finishEdit(bool ensureVisible = true);
    void focusPrevBlock();
    void focusNextBlock();
    void cancelEdit();
    bool isEditing() const { return m_isEditing; }

    // ===== 展开 / 折叠 =====
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    void setSubtreeVisible(bool visible);

    // ===== 导航 =====
    MindMapNode* getPrevSibling() const;
    MindMapNode* getNextSibling() const;
    MindMapNode* getFirstChild() const;
    MindMapNode* getRoot();

    // ===== 连线 =====
    void updateAllEdges();
    void clearEdges();
    void updateEdges();
    QPointF connectionPointRight() const;
    QPointF connectionPointLeft() const;

    // ===== 悬停与指示器 =====
    bool updateHoverState(const QPointF& nodePos);
    bool hitIndicator(const QPointF& nodePos) const;

    // ===== 键盘处理 =====
    void handleKeyPress(QKeyEvent* event);
    void handleEditTab(bool shiftPressed);

    // 供子 item 转发事件
    void handleChildPressEvent(QGraphicsSceneMouseEvent* event);
    void handleChildMoveEvent(QGraphicsSceneMouseEvent* event);
    void handleChildReleaseEvent(QGraphicsSceneMouseEvent* event);

    // ===== 焦点控制 =====
    void setBlockFocusOut(bool block) { m_blockFocusOut = block; }
    bool isBlockFocusOut() const { return m_blockFocusOut; }

    // ===== 撤销/恢复 =====
    static QUndoStack* s_undoStack;
    static void setUndoStack(QUndoStack* stack) { s_undoStack = stack; }

    // ===== 剪贴板 =====
    static QJsonObject s_clipboardData;
    static bool s_hasClipboard;

    // 【新增】自动调整所有文本块宽度与节点宽度一致
    void autoFitTextBlockWidths();

    // ===== 深拷贝 =====
    MindMapNode* cloneSubtree() const;

    // ===== 拖动重排序支持 =====
    int calculateBlockInsertIndex(qreal localY) const;
    void showDragIndicator(int index);
    void hideDragIndicator();

    // ===== 公式API（兼容旧接口）=====
    void insertFormula(const QString& latexCode);
    void editFormula(int index); // index 是 block 列表中的索引
    void removeFormula(int index);

    // ===== 图片API（兼容旧接口）=====
    void addImage(const QImage& image);
    void removeImage(MindMapImageItem* imageItem);

    // 获取图片块的序列化数据
    QVector<QPair<QString, QPair<QSizeF, QImage>>> extractImages() const;
    void injectImages(const QVector<QPair<QString, QPair<QSizeF, QImage>>>& images);

    // 获取公式块的序列化数据
    QVector<QString> extractFormulas() const;
    void injectFormulas(const QVector<QString>& formulas);

    // 查找当前正在编辑的节点
    static MindMapNode* findEditingNode(QGraphicsScene* scene);

    // 【新增】编辑时合并相邻文本块，退出编辑时按段落拆分
    TextBlock* mergeTextBlocksForEditing(TextBlock* originalBlock);
    void splitTextBlocksAfterEditing();
    QVector<MindMapBlock*> splitTextBlocksForMediaDrag();
    QVector<MindMapBlock*> takeMediaDragTransientBlocks();
    void restoreBlockSequence(const QVector<MindMapBlock*>& blocks);
    void mergeAdjacentTextBlocksForMediaDrag();
    // 【新增】辅助：将多个HTML合并为一个（处理段落边界）
    static QString mergeHtmlBlocks(const QVector<TextBlock*>& blocks);

    // 【新增】应用主题颜色
    void applyThemeColors();

    // 文档级三层样式由根节点持有；普通节点通过父节点关系读取。
    MindMapNode* styleRoot() const;
    int styleLevel() const;
    MindMapStyle styleForCurrentLevel() const;
    void applyStyle(const MindMapStyle& style, bool updateText = true,
                    bool respectExplicitTextFormats = false);
    void applyLevelStyles(const MindMapLevelStyles& styles);
    const MindMapLevelStyles& levelStyles() const;
    MindMapLevelStyles& levelStyles();
    void setLevelStyles(const MindMapLevelStyles& styles);
    // Local serialized content only; IDs, children, styles and fold state are live.
    void invalidateSerializationCache();
    bool hasSerializationCache() const { return m_hasSerializationCache; }
    QJsonObject serializationCache() const { return m_serializationCache; }
    void setSerializationCache(const QJsonObject& cache);
    MindMapNodeShape nodeShape() const { return m_shape; }

    // 【修复】主题颜色静态接口：深色模式下节点和连线颜色保持不变（使用浅色模式颜色）
    static void setDarkMode(bool dark);
    static bool isDarkMode();
    static QColor themeEdgeColor();
    static QColor themeNodeFill();
    static QColor themeNodeBorder();
    static QColor themeNodeText();
    static QColor themeHighlight();
    static QColor themeHover();

        // 【新增】节点宽度拖拽调整（类似XMind）
    enum class ResizeEdge { None, Left, Right };
    ResizeEdge hitTestResizeEdge(const QPointF& pos) const;
    void updateResizeCursor(ResizeEdge edge);
    void startNodeResize(ResizeEdge edge, const QPointF& pos);
    void doNodeResize(const QPointF& pos);
    void endNodeResize();
    bool isResizing() const { return m_isResizing; }

    // 【新增】设置节点内容宽度（所有文本块同步）
    void setNodeContentWidth(qreal width);
    void applyTextLineWidthPreferenceToSubtree();
    // 【新增】文本块宽度管理（直接调整文本块宽度，节点宽度由内容决定）
    qreal textBlockWidth() const;
    void setTextBlockWidth(qreal width);
    bool hasDraggedWidth() const { return m_textBlockWidth > 0; }
    // 【新增】获取/设置用户拖动过的节点宽度（-1 表示未拖动过）
    qreal nodeContentWidth() const;

    // 颜色（public 供拖拽代理使用）
 QColor m_fillColor = QColor(211, 234, 253);
    QColor m_borderColor = QColor(211, 234, 253);
    int m_borderWidth = 4;
    QColor m_textColor = QColor(5, 56, 96);
    QColor m_highlightColor = QColor(0, 120, 255);
    // 根节点专用样式（仅根节点使用）
    QColor m_rootFillColor = QColor(25, 55, 109);     // 深蓝色填充
    QColor m_rootBorderColor = QColor(25, 55, 109);     // 深蓝色边框
    QColor m_rootTextColor = QColor(255, 255, 255);     // 白色文字
    QColor m_hoverColor = QColor(180, 210, 240);
    MindMapNodeShape m_shape = MindMapNodeShape::RoundedRect;
    MindMapLevelStyles m_levelStyles;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;

public:
    // 内部方法
    void shiftSubtree(qreal dy);
    void onDocumentChanged();

    // 成员变量
    MindMapNode* m_parent;
    QVector<MindMapNode*> m_children;
    QVector<QGraphicsLineItem*> m_edgeItems;

    QVector<MindMapBlock*> m_blocks;
    MindMapBlock* m_selectedBlock = nullptr;
    TextBlock* m_editingTextBlock = nullptr;

    bool m_isSelected = false;
    bool m_isEditing = false;
    bool m_isHovered = false;
    bool m_expanded = true;

    QStringList m_editBackup; // 编辑前备份所有文本块的内容

    QMetaObject::Connection m_textConnection;
    bool m_blockFocusOut = false;
    qreal m_oldZValue = 0;
    QMap<QGraphicsItem*, qreal> m_childOldZValues;
    QVector<MindMapBlock*> m_mediaDragTransientBlocks;

    // 拖动指示器
    QGraphicsLineItem* m_dragIndicator = nullptr;

    // 常量
    static constexpr qreal H_GAP = 40;
    static constexpr qreal V_GAP = 16;
    static constexpr qreal MARGIN_H = 6;
    static constexpr qreal MARGIN_V = 4;
    static constexpr qreal CORNER_RADIUS = 8;
    static constexpr qreal HIGHLIGHT_MARGIN = 6;
    static constexpr qreal BLOCK_SPACING = 0;

    static constexpr qreal INDICATOR_RADIUS = 6;
    static constexpr qreal INDICATOR_HIT_RADIUS = 15;

private:
    friend class MindMapHoverTracker;

    MindMapHoverTracker* m_hoverOwner = nullptr;
    void layoutTreeRecursive(qreal x, qreal y);
    MindMapNodeSizeCache m_nodeSizeCache;
    QJsonObject m_serializationCache;
    bool m_hasSerializationCache = false;

    // 【新增】节点宽度拖拽调整状态
    bool m_isResizing = false;
    ResizeEdge m_resizeEdge = ResizeEdge::None;
    QPointF m_resizeStartPos;
    qreal m_resizeStartWidth = 0;
    qreal m_nodeMinWidth = 60;   // 节点最小宽度
    qreal m_nodeMaxWidth = 800;  // 节点最大宽度
    qreal m_textBlockWidth = -1;  // -1 表示未手动设置过文本块宽度



    static bool s_darkMode;

};



