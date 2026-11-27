#pragma once

#include <QUndoStack>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QPointF>
#include <QVector>

class MindMapNode;
class MindMapBlock;
class TextBlock;

// 结构性重排：整棵树回到规范原点 (0, 0) 重新布局。
// layoutTree() 的 y 参数是「子树顶部」而不是节点自身的 y，所以规范起点只能是
// (0, 0)；传入 root->pos().y() 会让根节点每次重排都向下累积偏移，而别处的重排
// 又把偏移抹平，画面就在两套绝对坐标之间跳。
// 若场景连着 MindMapView，则同时把内容钉回屏幕原位（滚动补偿），
// 这样收尾编辑、撤销/重做、宽度拖拽都不会让视野跳动。
void relayout(MindMapNode* root);

// 编辑节点文本（保存所有文本块的完整状态）
class EditTextCommand : public QUndoCommand {
public:
    EditTextCommand(MindMapNode* node, const QStringList& oldBlocks, const QStringList& newBlocks,
                    bool ensureVisible = true);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    QStringList m_oldBlocks;
    QStringList m_newBlocks;
    // The edited node already contains m_newBlocks when this command is pushed.
    // QUndoStack::push() invokes redo() synchronously, so skip only that first call.
    bool m_skipInitialRedo = true;
    bool m_ensureVisible = true;
};

// 添加兄弟节点（Enter）
class AddSiblingCommand : public QUndoCommand {
public:
    AddSiblingCommand(MindMapNode* refNode, QGraphicsScene* scene);
    ~AddSiblingCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_parent;
    MindMapNode* m_newNode;
    int m_index;
    QGraphicsScene* m_scene;
    MindMapNode* m_prevSelected;
    bool m_ownsNode;
};

// 添加子节点（Tab）
class AddChildCommand : public QUndoCommand {
public:
    AddChildCommand(MindMapNode* parent, QGraphicsScene* scene);
    ~AddChildCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_parent;
    MindMapNode* m_newNode;
    QGraphicsScene* m_scene;
    MindMapNode* m_prevSelected;
    bool m_ownsNode;
};

// 删除节点（Delete/Backspace）
class RemoveNodeCommand : public QUndoCommand {
public:
    RemoveNodeCommand(MindMapNode* node, QGraphicsScene* scene);
    ~RemoveNodeCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    MindMapNode* m_parent;
    int m_index;
    QGraphicsScene* m_scene;
    MindMapNode* m_newSelected;
};

// 拖拽移动节点
class DragMoveCommand : public QUndoCommand {
public:
    DragMoveCommand(MindMapNode* node, MindMapNode* oldParent, int oldIndex,
                    MindMapNode* newParent, int newIndex, QGraphicsScene* scene);
    ~DragMoveCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    MindMapNode* m_oldParent;
    int m_oldIndex;
    MindMapNode* m_newParent;
    int m_newIndex;
    QGraphicsScene* m_scene;
};

class MultiReparentCommand : public QUndoCommand {
public:
    MultiReparentCommand(const QVector<MindMapNode*>& nodes,
                         MindMapNode* newParent, int newIndex, QGraphicsScene* scene);
    void undo() override;
    void redo() override;

private:
    void detachFromCurrentParents();
    void restoreOldParents();

    QVector<MindMapNode*> m_nodes;
    QVector<MindMapNode*> m_oldParents;
    QVector<int> m_oldIndices;
    MindMapNode* m_newParent = nullptr;
    int m_newIndex = -1;
    QGraphicsScene* m_scene = nullptr;
};

// 展开/折叠节点
class MultiMoveCommand : public QUndoCommand {
public:
    MultiMoveCommand(const QVector<MindMapNode*>& nodes,
                     const QVector<QPointF>& oldPositions,
                     const QVector<QPointF>& newPositions,
                     QGraphicsScene* scene);
    void undo() override;
    void redo() override;

private:
    void applyPositions(const QVector<QPointF>& positions);

    QVector<MindMapNode*> m_nodes;
    QVector<QPointF> m_oldPositions;
    QVector<QPointF> m_newPositions;
    QGraphicsScene* m_scene;
};

class ToggleExpandCommand : public QUndoCommand {
public:
    ToggleExpandCommand(MindMapNode* node, bool expanded);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    bool m_oldExpanded;
    bool m_newExpanded;
};

// 粘贴子树（Ctrl+V）
class PasteSubtreeCommand : public QUndoCommand {
public:
    PasteSubtreeCommand(MindMapNode* targetParent, MindMapNode* clonedRoot, QGraphicsScene* scene);
    ~PasteSubtreeCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_targetParent;
    MindMapNode* m_clonedRoot;
    QGraphicsScene* m_scene;
    bool m_ownsNode;
};

// ===== Block 系统命令 =====

// 插入 Block（图片/公式）
class InsertBlockCommand : public QUndoCommand {
public:
    InsertBlockCommand(MindMapNode* node, MindMapBlock* block, int index, QGraphicsScene* scene);
    ~InsertBlockCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    MindMapBlock* m_block;
    int m_index;
    QGraphicsScene* m_scene;
    bool m_ownsBlock;
};

// 删除 Block
class RemoveBlockCommand : public QUndoCommand {
public:
    RemoveBlockCommand(MindMapNode* node, int index, QGraphicsScene* scene);
    ~RemoveBlockCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    MindMapBlock* m_block;
    int m_index;
    QGraphicsScene* m_scene;
    bool m_ownsBlock;
};

// 移动 Block（重排序）
class MoveBlockCommand : public QUndoCommand {
public:
    MoveBlockCommand(MindMapNode* node, int oldIndex, int newIndex, QGraphicsScene* scene);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    int m_oldIndex;
    int m_newIndex;
    QGraphicsScene* m_scene;
};

class MoveMediaBlockCommand : public QUndoCommand {
public:
    MoveMediaBlockCommand(MindMapNode* node, MindMapBlock* mediaBlock,
                          const QVector<MindMapBlock*>& beforeBlocks,
                          const QVector<MindMapBlock*>& afterBlocks,
                          const QVector<MindMapBlock*>& transientBlocks,
                          QGraphicsScene* scene);
    ~MoveMediaBlockCommand();
    void undo() override;
    void redo() override;

private:
    MindMapNode* m_node;
    MindMapBlock* m_mediaBlock;
    QVector<MindMapBlock*> m_beforeBlocks;
    QVector<MindMapBlock*> m_afterBlocks;
    QGraphicsScene* m_scene;
    bool m_skipInitialRedo = true;
    QVector<MindMapBlock*> m_allBlocks;
    void apply(const QVector<MindMapBlock*>& blocks);
};

// 拆分文本块（Shift+Enter）
class SplitTextBlockCommand : public QUndoCommand {
public:
    SplitTextBlockCommand(MindMapNode* node, TextBlock* block, int blockIndex,
                          int cursorPosition, QGraphicsScene* scene);
    ~SplitTextBlockCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    TextBlock* m_originalBlock;
    TextBlock* m_newBlock;
    int m_blockIndex;
    int m_cursorPosition;
    QString m_originalHtml;
    QString m_beforeHtml;
    QString m_afterHtml;
    QGraphicsScene* m_scene;
    bool m_ownsNewBlock;
};

// 合并文本块（Backspace/Delete）
class MergeTextBlockCommand : public QUndoCommand {
public:
    enum Direction { WithPrevious, WithNext };
    MergeTextBlockCommand(MindMapNode* node, int blockIndex, Direction dir, QGraphicsScene* scene);
    ~MergeTextBlockCommand();
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    int m_blockIndex;
    Direction m_direction;
    QString m_firstHtml;
    QString m_secondHtml;
    int m_cursorPosition = 0;
    QGraphicsScene* m_scene;
    TextBlock* m_newBlock = nullptr;
    bool m_ownsNewBlock = false;
};

// 缩放图片块
class ResizeImageCommand : public QUndoCommand {
public:
    ResizeImageCommand(MindMapNode* node, int blockIndex, const QSizeF& oldSize, const QSizeF& newSize, QGraphicsScene* scene);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    int m_blockIndex;
    QSizeF m_oldSize;
    QSizeF m_newSize;
    QGraphicsScene* m_scene;
};

// 编辑公式块
class EditFormulaCommand : public QUndoCommand {
public:
    EditFormulaCommand(MindMapNode* node, int blockIndex, const QString& oldLatex, const QString& newLatex,
                        const QPixmap& oldPixmap, const QPixmap& newPixmap, QGraphicsScene* scene);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    int m_blockIndex;
    QString m_oldLatex;
    QString m_newLatex;
    QPixmap m_oldPixmap;
    QPixmap m_newPixmap;
    QGraphicsScene* m_scene;
};

// 【新增】文字格式命令（加粗/斜体/颜色）
class FormatTextCommand : public QUndoCommand {
public:
    FormatTextCommand(MindMapNode* node, int blockIndex,
                      const QString& oldHtml, const QString& newHtml,
                      int cursorPos, QGraphicsScene* scene);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    int m_blockIndex;
    QString m_oldHtml;
    QString m_newHtml;
    int m_cursorPos;
    QGraphicsScene* m_scene;
};

// 【新增】调整节点宽度命令
class ResizeNodeCommand : public QUndoCommand {
public:
    ResizeNodeCommand(MindMapNode* node, qreal oldWidth, qreal newWidth, QGraphicsScene* scene);
    void undo() override;
    void redo() override;
private:
    MindMapNode* m_node;
    qreal m_oldWidth;
    qreal m_newWidth;
    QGraphicsScene* m_scene;
};

// 【新增】整份文档替换（供插件批量改写文档用）。
//
// 只保存两份 JSON 快照，不持有节点对象。原因是「持有两棵树」的写法会出人命：
// 撤销栈里排在它前面的命令（EditTextCommand 等）还引用着旧树的节点，
// 一旦本命令析构把旧树删了，那些命令再撤销就是访问已释放内存。
// 保存 JSON 就没有归属权纠缠，代价是撤销/重做时各反序列化一次。
//
// 使用前提（由调用方保证）：push 之前已经 m_undoStack->clear()，
// 这样栈里没有别的命令引用即将被替换掉的节点。
class ReplaceDocumentCommand : public QUndoCommand {
public:
    ReplaceDocumentCommand(const QJsonObject& oldDoc, const QJsonObject& newDoc,
                           QGraphicsScene* scene, const QString& text = QStringLiteral("替换文档"));
    void undo() override;
    void redo() override;

private:
    // 清空场景并把 doc 描述的那棵树装进去。
    void install(const QJsonObject& doc);

    QJsonObject m_oldDoc;
    QJsonObject m_newDoc;
    QGraphicsScene* m_scene = nullptr;
};

