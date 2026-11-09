#include "MindMapCommands.h"
#include "MindMapNode.h"
#include <algorithm>
#include "MindMapBlock.h"
#include "MindMapImageItem.h"
#include "MindMapImageItem.h"
#include "MindMapView.h"

static MindMapNode* findSelectedNode(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    for (auto* item : scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected()) return node;
        }
    }
    return nullptr;
}

void relayout(MindMapNode* root) {
    if (!root) return;
    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
}

void detachSubtree(MindMapNode* node, QGraphicsScene* scene) {
    if (!node) return;
    node->clearEdges();
    if (scene && node->scene()) scene->removeItem(node);
    for (auto* child : node->childNodes()) {
        detachSubtree(child, scene);
    }
}

void attachSubtree(MindMapNode* node, QGraphicsScene* scene) {
    if (!node) return;
    if (scene && !node->scene()) scene->addItem(node);
    for (auto* child : node->childNodes()) {
        attachSubtree(child, scene);
    }
}

void triggerSceneExpand(MindMapNode* node, bool ensureVisible = true) {
    if (!node || !node->scene()) return;
    if (!node->scene()->views().isEmpty()) {
        if (auto* view = dynamic_cast<MindMapView*>(node->scene()->views().first())) {
            view->ensureSceneRectContains();
            if (ensureVisible) view->ensureNodeVisible(node);
        }
    }
}

// ===== EditTextCommand =====
EditTextCommand::EditTextCommand(MindMapNode* node, const QStringList& oldBlocks, const QStringList& newBlocks,
                                 bool ensureVisible)
    : m_node(node), m_oldBlocks(oldBlocks), m_newBlocks(newBlocks), m_ensureVisible(ensureVisible) {
    setText("编辑节点文本");
}

static void applyBlockTexts(MindMapNode* node, const QStringList& texts) {
    if (!node) return;

    // 收集当前所有文本块
    QVector<TextBlock*> textBlocks;
    QVector<MindMapBlock*> nonTextBlocks;
    for (auto* block : node->blocks()) {
        if (block->type() == BlockType::Text) {
            textBlocks.append(static_cast<TextBlock*>(block));
        } else {
            nonTextBlocks.append(block);
        }
    }

    // 如果文本块数量不匹配，需要增删
    if (textBlocks.size() > texts.size()) {
        // 删除多余的文本块（从后往前删）
        for (int i = textBlocks.size() - 1; i >= texts.size(); --i) {
            int idx = node->blockIndex(textBlocks[i]);
            if (idx >= 0) node->removeBlockAt(idx);
        }
        textBlocks.resize(texts.size());
    } else if (textBlocks.size() < texts.size()) {
        // 添加新的文本块
        for (int i = textBlocks.size(); i < texts.size(); ++i) {
            TextBlock* tb = new TextBlock(node);
            node->addBlock(tb);
            textBlocks.append(tb);
        }
    }

    // 更新每个文本块的内容
    for (int i = 0; i < texts.size() && i < textBlocks.size(); ++i) {
        textBlocks[i]->setHtml(texts[i]);  // 【修复】setHtml() 保留格式
    }
}

void EditTextCommand::undo() {
    if (!m_node) return;
    applyBlockTexts(m_node, m_oldBlocks);
    relayout(m_node->getRoot());
    if (m_node->scene()) m_node->scene()->update();
    triggerSceneExpand(m_node, m_ensureVisible);
}

void EditTextCommand::redo() {
    if (!m_node) return;
    if (m_skipInitialRedo) {
        m_skipInitialRedo = false;
        // The node already contains the edited HTML when the command is pushed.
        // Still perform the required layout pass because editing may have changed
        // this node's size and affected sibling/descendant positions.
        relayout(m_node->getRoot());
        if (m_node->scene()) m_node->scene()->update();
        triggerSceneExpand(m_node, m_ensureVisible);
        return;
    }
    applyBlockTexts(m_node, m_newBlocks);
    relayout(m_node->getRoot());
    if (m_node->scene()) m_node->scene()->update();
    triggerSceneExpand(m_node, m_ensureVisible);
}

// ===== AddSiblingCommand =====
AddSiblingCommand::AddSiblingCommand(MindMapNode* refNode, QGraphicsScene* scene)
    : m_parent(nullptr), m_newNode(nullptr), m_index(-1), m_scene(scene),
      m_prevSelected(nullptr), m_ownsNode(false) {
    setText("添加节点");
    if (!refNode) return;
    m_prevSelected = findSelectedNode(scene);
    m_parent = refNode->parentNode();
    if (m_parent) {
        auto siblings = m_parent->childNodes();
        int idx = siblings.indexOf(refNode);
        m_index = (idx >= 0) ? idx + 1 : m_parent->childNodes().size();
    } else {
        m_parent = refNode;
        m_index = m_parent->childNodes().size();
    }
    m_newNode = new MindMapNode("", nullptr);
}

AddSiblingCommand::~AddSiblingCommand() {
    if (m_ownsNode && m_newNode && !m_newNode->parentNode() && !m_newNode->scene()) {
        delete m_newNode;
    }
}

void AddSiblingCommand::undo() {
    if (!m_newNode || !m_parent) return;
    m_parent->removeChild(m_newNode);
    detachSubtree(m_newNode, m_scene);
    if (m_prevSelected) m_prevSelected->setNodeSelected(true);
    else m_newNode->setNodeSelected(false);
    relayout(m_parent->getRoot());
    m_ownsNode = true;
    triggerSceneExpand(m_parent);
}

void AddSiblingCommand::redo() {
    if (!m_newNode || !m_parent) return;
    m_parent->insertChild(m_index, m_newNode);
    attachSubtree(m_newNode, m_scene);
    if (m_prevSelected) m_prevSelected->setNodeSelected(false);
    m_newNode->setNodeSelected(true);
    relayout(m_parent->getRoot());
    m_ownsNode = false;
    triggerSceneExpand(m_newNode);
    if (m_scene && !m_scene->views().isEmpty()) {
        if (auto* view = dynamic_cast<MindMapView*>(m_scene->views().first()))
            view->ensureNodeInEditSafeZone(m_newNode);
    }
}

// ===== AddChildCommand =====
AddChildCommand::AddChildCommand(MindMapNode* parent, QGraphicsScene* scene)
    : m_parent(parent), m_newNode(nullptr), m_scene(scene),
      m_prevSelected(nullptr), m_ownsNode(false) {
    setText("添加子节点");
    if (!m_parent) return;
    m_prevSelected = findSelectedNode(scene);
    m_newNode = new MindMapNode("", nullptr);
}

AddChildCommand::~AddChildCommand() {
    if (m_ownsNode && m_newNode && !m_newNode->parentNode() && !m_newNode->scene()) {
        delete m_newNode;
    }
}

void AddChildCommand::undo() {
    if (!m_newNode || !m_parent) return;
    m_parent->removeChild(m_newNode);
    detachSubtree(m_newNode, m_scene);
    if (m_prevSelected) m_prevSelected->setNodeSelected(true);
    else m_newNode->setNodeSelected(false);
    relayout(m_parent->getRoot());
    m_ownsNode = true;
    triggerSceneExpand(m_parent);
}

void AddChildCommand::redo() {
    if (!m_newNode || !m_parent) return;
    m_parent->addChild(m_newNode);
    attachSubtree(m_newNode, m_scene);
    if (m_prevSelected) m_prevSelected->setNodeSelected(false);
    m_newNode->setNodeSelected(true);
    relayout(m_parent->getRoot());
    m_ownsNode = false;
    triggerSceneExpand(m_newNode);
    if (m_scene && !m_scene->views().isEmpty()) {
        if (auto* view = dynamic_cast<MindMapView*>(m_scene->views().first()))
            view->ensureNodeInEditSafeZone(m_newNode);
    }
}

// ===== RemoveNodeCommand =====
RemoveNodeCommand::RemoveNodeCommand(MindMapNode* node, QGraphicsScene* scene)
    : m_node(node), m_parent(nullptr), m_index(-1), m_scene(scene), m_newSelected(nullptr) {
    setText("删除节点");
    if (!m_node) return;
    m_parent = m_node->parentNode();
    if (m_parent) {
        m_index = m_parent->childNodes().indexOf(m_node);
        auto siblings = m_parent->childNodes();
        int idx = m_index;
        if (idx >= 0 && idx < siblings.size() - 1) {
            m_newSelected = siblings[idx + 1];
        } else if (idx > 0) {
            m_newSelected = siblings[idx - 1];
        } else {
            m_newSelected = m_parent;
        }
    }
}

RemoveNodeCommand::~RemoveNodeCommand() {
    if (m_node && !m_node->parentNode() && !m_node->scene()) {
        delete m_node;
    }
}

void RemoveNodeCommand::undo() {
    if (!m_node) return;
    if (m_parent) {
        m_parent->insertChild(m_index, m_node);
    }
    attachSubtree(m_node, m_scene);
    if (m_newSelected) m_newSelected->setNodeSelected(false);
    m_node->setNodeSelected(true);
    relayout(m_node->getRoot());
    triggerSceneExpand(m_node);
}

void RemoveNodeCommand::redo() {
    if (!m_node) return;
    if (m_parent) {
        m_parent->removeChild(m_node);
    }
    detachSubtree(m_node, m_scene);
    m_node->setNodeSelected(false);
    if (m_newSelected) m_newSelected->setNodeSelected(true);
    if (m_parent) relayout(m_parent->getRoot());
    else if (m_newSelected) relayout(m_newSelected->getRoot());
    triggerSceneExpand(m_newSelected ? m_newSelected : m_parent);
}

// ===== ToggleExpandCommand =====
ToggleExpandCommand::ToggleExpandCommand(MindMapNode* node, bool expanded)
    : m_node(node), m_newExpanded(expanded), m_oldExpanded(node ? node->isExpanded() : true) {
    setText(expanded ? "展开节点" : "折叠节点");
}

void ToggleExpandCommand::undo() {
    if (!m_node) return;
    m_node->setExpanded(m_oldExpanded);
    triggerSceneExpand(m_node);
}

void ToggleExpandCommand::redo() {
    if (!m_node) return;
    m_node->setExpanded(m_newExpanded);
    triggerSceneExpand(m_node);
}

// ===== DragMoveCommand =====
DragMoveCommand::DragMoveCommand(MindMapNode* node, MindMapNode* oldParent, int oldIndex,
                                 MindMapNode* newParent, int newIndex, QGraphicsScene* scene)
    : m_node(node), m_oldParent(oldParent), m_oldIndex(oldIndex),
      m_newParent(newParent), m_newIndex(newIndex), m_scene(scene) {
    setText("移动节点");
}

DragMoveCommand::~DragMoveCommand() {
}

void DragMoveCommand::undo() {
    if (!m_node || !m_newParent) return;
    m_newParent->removeChild(m_node);
    if (m_oldParent) {
        if (m_oldIndex >= 0) {
            m_oldParent->insertChild(m_oldIndex, m_node);
        } else {
            m_oldParent->addChild(m_node);
        }
    }
    if (m_scene) {
        if (!m_node->scene()) m_scene->addItem(m_node);
        for (auto* child : m_node->childNodes()) {
            attachSubtree(child, m_scene);
        }
    }
    MindMapNode* root = m_node->getRoot();
    relayout(root);
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void DragMoveCommand::redo() {
    if (!m_node || !m_newParent) return;
    if (m_oldParent) m_oldParent->removeChild(m_node);
    if (m_newIndex >= 0) {
        m_newParent->insertChild(m_newIndex, m_node);
    } else {
        m_newParent->addChild(m_node);
    }
    if (m_scene) {
        if (!m_node->scene()) m_scene->addItem(m_node);
        for (auto* child : m_node->childNodes()) {
            attachSubtree(child, m_scene);
        }
    }
    MindMapNode* root = m_newParent->getRoot();
    relayout(root);
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

MultiReparentCommand::MultiReparentCommand(const QVector<MindMapNode*>& nodes,
                                           MindMapNode* newParent, int newIndex,
                                           QGraphicsScene* scene)
    : m_nodes(nodes), m_newParent(newParent), m_newIndex(newIndex), m_scene(scene) {
    for (MindMapNode* node : m_nodes) {
        MindMapNode* parent = node ? node->parentNode() : nullptr;
        m_oldParents.append(parent);
        m_oldIndices.append(parent ? parent->childNodes().indexOf(node) : -1);
    }
    setText(QStringLiteral("移动多个节点"));
}

void MultiReparentCommand::detachFromCurrentParents() {
    for (MindMapNode* node : m_nodes) {
        if (node && node->parentNode()) node->parentNode()->removeChild(node);
    }
}

void MultiReparentCommand::restoreOldParents() {
    QVector<int> order;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i] && m_oldParents[i]) order.append(i);
    }
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        if (m_oldParents[a] != m_oldParents[b])
            return quintptr(m_oldParents[a]) < quintptr(m_oldParents[b]);
        return m_oldIndices[a] < m_oldIndices[b];
    });
    for (int i : order) {
        m_oldParents[i]->insertChild(m_oldIndices[i], m_nodes[i]);
    }
}

void MultiReparentCommand::undo() {
    if (m_nodes.isEmpty()) return;
    detachFromCurrentParents();
    restoreOldParents();
    for (MindMapNode* node : m_nodes) attachSubtree(node, m_scene);
    MindMapNode* root = m_nodes.first() ? m_nodes.first()->getRoot() : nullptr;
    relayout(root);
    if (m_scene) m_scene->update();
}

void MultiReparentCommand::redo() {
    if (m_nodes.isEmpty() || !m_newParent) return;
    int adjustedIndex = m_newIndex;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_oldParents[i] == m_newParent && m_oldIndices[i] < m_newIndex)
            --adjustedIndex;
    }
    detachFromCurrentParents();
    adjustedIndex = qBound(0, adjustedIndex, int(m_newParent->childNodes().size()));
    for (int i = 0; i < m_nodes.size(); ++i) {
        m_newParent->insertChild(adjustedIndex + i, m_nodes[i]);
        attachSubtree(m_nodes[i], m_scene);
    }
    MindMapNode* root = m_newParent->getRoot();
    relayout(root);
    if (m_scene) m_scene->update();
}

MultiMoveCommand::MultiMoveCommand(const QVector<MindMapNode*>& nodes,
                                   const QVector<QPointF>& oldPositions,
                                   const QVector<QPointF>& newPositions,
                                   QGraphicsScene* scene)
    : m_nodes(nodes),
      m_oldPositions(oldPositions),
      m_newPositions(newPositions),
      m_scene(scene) {
    setText(QStringLiteral("移动多个节点"));
}

void MultiMoveCommand::undo() {
    applyPositions(m_oldPositions);
}

void MultiMoveCommand::redo() {
    applyPositions(m_newPositions);
}

void MultiMoveCommand::applyPositions(const QVector<QPointF>& positions) {
    const int count = qMin(m_nodes.size(), positions.size());
    QVector<MindMapNode*> rootsToUpdate;
    for (int i = 0; i < count; ++i) {
        MindMapNode* node = m_nodes.at(i);
        if (!node) continue;
        node->setPos(positions.at(i));
        MindMapNode* root = node->getRoot();
        if (root && !rootsToUpdate.contains(root)) {
            rootsToUpdate.append(root);
        }
    }

    for (MindMapNode* root : rootsToUpdate) {
        root->clearEdges();
        root->updateAllEdges();
    }
    if (m_scene) m_scene->update();
}

// ===== PasteSubtreeCommand =====
PasteSubtreeCommand::PasteSubtreeCommand(MindMapNode* targetParent, MindMapNode* clonedRoot, QGraphicsScene* scene)
    : m_targetParent(targetParent), m_clonedRoot(clonedRoot), m_scene(scene), m_ownsNode(true) {
    setText("粘贴子树");
}

PasteSubtreeCommand::~PasteSubtreeCommand() {
    if (m_ownsNode && m_clonedRoot && !m_clonedRoot->parentNode() && !m_clonedRoot->scene()) {
        delete m_clonedRoot;
    }
}

void PasteSubtreeCommand::undo() {
    if (!m_clonedRoot || !m_targetParent) return;
    m_targetParent->removeChild(m_clonedRoot);
    detachSubtree(m_clonedRoot, m_scene);
    m_ownsNode = true;
    MindMapNode* root = m_targetParent->getRoot();
    relayout(root);
    triggerSceneExpand(m_targetParent);
}

void PasteSubtreeCommand::redo() {
    if (!m_clonedRoot || !m_targetParent) return;
    m_targetParent->addChild(m_clonedRoot);
    attachSubtree(m_clonedRoot, m_scene);
    m_ownsNode = false;
    MindMapNode* root = m_targetParent->getRoot();
    relayout(root);
    triggerSceneExpand(m_clonedRoot);
}

// ===== InsertBlockCommand =====
InsertBlockCommand::InsertBlockCommand(MindMapNode* node, MindMapBlock* block, int index, QGraphicsScene* scene)
    : m_node(node), m_block(block), m_index(index), m_scene(scene), m_ownsBlock(true) {
    setText("插入内容块");
}

InsertBlockCommand::~InsertBlockCommand() {
    if (m_ownsBlock && m_block) {
        // 【修复】如果 block 有 graphicsItem 且 parentItem 是 MindMapNode，
        // 需要先解除父子关系，让 block 析构时安全 delete item
        if (m_block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(m_block);
            if (ib->imageItem() && ib->imageItem()->parentItem()) {
                ib->imageItem()->setParentItem(nullptr);
            }
        } else if (m_block->type() == BlockType::Formula) {
            auto* fb = static_cast<FormulaBlock*>(m_block);
            if (fb->pixmapItem() && fb->pixmapItem()->parentItem()) {
                fb->pixmapItem()->setParentItem(nullptr);
            }
        }
        delete m_block;
    }
}

void InsertBlockCommand::undo() {
    if (!m_node || !m_block) return;
    int idx = m_node->blockIndex(m_block);
    if (idx >= 0) {
        // 【修复】使用 takeBlockAt 只移除不删除，避免 double-free
        MindMapBlock* taken = m_node->takeBlockAt(idx);
        // 确保取出的就是我们持有的 block
        if (taken && taken != m_block) {
            // 防御性处理：如果取出的是别的 block，删除它
            delete taken;
        }
    }
    // block 已从节点列表移除，图形项也必须脱离节点并隐藏。
    // 否则图片/公式虽然不再属于 m_blocks，仍会继续绘制，
    // 表现为撤销插入后内容没有消失。
    if (m_block->type() == BlockType::Image) {
        auto* ib = static_cast<ImageBlock*>(m_block);
        if (ib->imageItem()) {
            ib->imageItem()->setParentItem(nullptr);
            ib->imageItem()->setVisible(false);
            ib->imageItem()->setImageSelected(false);
        }
    } else if (m_block->type() == BlockType::Formula) {
        auto* fb = static_cast<FormulaBlock*>(m_block);
        if (fb->pixmapItem()) {
            fb->pixmapItem()->setParentItem(nullptr);
            fb->pixmapItem()->setVisible(false);
            fb->pixmapItem()->setFormulaSelected(false);
        }
    }
    m_ownsBlock = true;
    MindMapNode* root = m_node->getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    // 【新增】撤销插入后，自动调整文本块宽度与节点一致
    m_node->autoFitTextBlockWidths();
    m_node->calcNodeSize();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void InsertBlockCommand::redo() {
    if (!m_node || !m_block) return;
    // 【修复】不要从场景中移除 graphicsItem
    // ImageBlock/FormulaBlock 的 graphicsItem 是 MindMapNode 的子 item
    // 只要 MindMapNode 在场景中，它们就在场景中
    // 我们只需要确保它们在 m_blocks 列表中，updateBlockLayout 会设置位置
    m_node->addBlock(m_block, m_index);
    // 重新建立 block 与 item 的关联（图片/公式块需要）
    if (m_block->type() == BlockType::Image) {
        auto* ib = static_cast<ImageBlock*>(m_block);
        if (ib->imageItem()) {
            ib->imageItem()->setParentItem(m_node);
            ib->imageItem()->setVisible(true);
            ib->imageItem()->setImageBlock(ib);
        }
    } else if (m_block->type() == BlockType::Formula) {
        auto* fb = static_cast<FormulaBlock*>(m_block);
        if (fb->pixmapItem()) {
            fb->pixmapItem()->setParentItem(m_node);
            fb->pixmapItem()->setVisible(true);
            fb->pixmapItem()->setFormulaBlock(fb);
        }
    }
    m_ownsBlock = false;
    MindMapNode* root = m_node->getRoot();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    // 【新增】插入图片/公式后，自动调整文本块宽度与节点一致
    m_node->autoFitTextBlockWidths();
    m_node->calcNodeSize();
    if (root) {
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

// ===== RemoveBlockCommand =====
RemoveBlockCommand::RemoveBlockCommand(MindMapNode* node, int index, QGraphicsScene* scene)
    : m_node(node), m_block(nullptr), m_index(index), m_scene(scene), m_ownsBlock(false) {
    setText("删除内容块");
    if (m_node && index >= 0 && index < m_node->blocks().size()) {
        m_block = m_node->blocks()[index];
    }
}

RemoveBlockCommand::~RemoveBlockCommand() {
    if (m_ownsBlock && m_block) {
        // 【修复】先解除父子关系，避免 block 析构时的 double-free
        if (m_block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(m_block);
            if (ib->imageItem() && ib->imageItem()->parentItem()) {
                ib->imageItem()->setParentItem(nullptr);
            }
        } else if (m_block->type() == BlockType::Formula) {
            auto* fb = static_cast<FormulaBlock*>(m_block);
            if (fb->pixmapItem() && fb->pixmapItem()->parentItem()) {
                fb->pixmapItem()->setParentItem(nullptr);
            }
        }
        delete m_block;
    }
}

void RemoveBlockCommand::undo() {
    if (!m_node || !m_block) return;
    m_node->addBlock(m_block, m_index);
    // 【修复】撤销删除时，恢复 Image/Formula 图形项的可见性
    if (m_block->type() == BlockType::Image) {
        auto* ib = static_cast<ImageBlock*>(m_block);
        if (ib->imageItem()) {
            ib->imageItem()->setVisible(true);
            ib->imageItem()->setParentItem(m_node);
        }
    } else if (m_block->type() == BlockType::Formula) {
        auto* fb = static_cast<FormulaBlock*>(m_block);
        if (fb->pixmapItem()) {
            fb->pixmapItem()->setVisible(true);
            fb->pixmapItem()->setParentItem(m_node);
        }
    }
    m_ownsBlock = false;
    relayout(m_node->getRoot());
    // 【新增】撤销删除后，自动调整文本块宽度与节点一致
    m_node->autoFitTextBlockWidths();
    m_node->calcNodeSize();
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void RemoveBlockCommand::redo() {
    if (!m_node || !m_block) return;
    int idx = m_node->blockIndex(m_block);
    if (idx >= 0) {
        // 【修复】使用 takeBlockAt 只移除不删除，避免 double-free
        MindMapBlock* taken = m_node->takeBlockAt(idx);
        // 确保取出的就是我们持有的 block
        if (taken && taken != m_block) {
            // 防御性处理：如果取出的是别的 block，删除它
            // 先解除父子关系
            if (taken->type() == BlockType::Image) {
                auto* ib = static_cast<ImageBlock*>(taken);
                if (ib->imageItem() && ib->imageItem()->parentItem()) {
                    ib->imageItem()->setParentItem(nullptr);
                }
            } else if (taken->type() == BlockType::Formula) {
                auto* fb = static_cast<FormulaBlock*>(taken);
                if (fb->pixmapItem() && fb->pixmapItem()->parentItem()) {
                    fb->pixmapItem()->setParentItem(nullptr);
                }
            }
            delete taken;
        }
        // 【修复】删除 block 后，将 Image/Formula 的图形项从画面中隐藏，避免残留显示
        if (m_block->type() == BlockType::Image) {
            auto* ib = static_cast<ImageBlock*>(m_block);
            if (ib->imageItem()) {
                ib->imageItem()->setParentItem(nullptr);
                ib->imageItem()->setVisible(false);
            }
        } else if (m_block->type() == BlockType::Formula) {
            auto* fb = static_cast<FormulaBlock*>(m_block);
            if (fb->pixmapItem()) {
                fb->pixmapItem()->setParentItem(nullptr);
                fb->pixmapItem()->setVisible(false);
            }
        }
    }
    m_ownsBlock = true;
    relayout(m_node->getRoot());
    // 【新增】删除后，自动调整文本块宽度与节点一致
    m_node->autoFitTextBlockWidths();
    m_node->calcNodeSize();
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

// ===== MoveBlockCommand =====
MoveBlockCommand::MoveBlockCommand(MindMapNode* node, int oldIndex, int newIndex, QGraphicsScene* scene)
    : m_node(node), m_oldIndex(oldIndex), m_newIndex(newIndex), m_scene(scene) {
    setText("移动内容块");
}

void MoveBlockCommand::undo() {
    if (!m_node) return;
    const int movedIndex = m_newIndex > m_oldIndex ? m_newIndex - 1 : m_newIndex;
    m_node->moveBlock(movedIndex, m_oldIndex);
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void MoveBlockCommand::redo() {
    if (!m_node) return;
    m_node->moveBlock(m_oldIndex, m_newIndex);
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

MoveMediaBlockCommand::MoveMediaBlockCommand(MindMapNode* node, MindMapBlock* mediaBlock,
                                             const QVector<MindMapBlock*>& beforeBlocks,
                                             const QVector<MindMapBlock*>& afterBlocks,
                                             const QVector<MindMapBlock*>& transientBlocks,
                                             QGraphicsScene* scene)
    : m_node(node), m_mediaBlock(mediaBlock), m_beforeBlocks(beforeBlocks),
      m_afterBlocks(afterBlocks), m_scene(scene) {
    setText("移动图片或公式");
    for (MindMapBlock* block : beforeBlocks) if (!m_allBlocks.contains(block)) m_allBlocks.append(block);
    for (MindMapBlock* block : afterBlocks) if (!m_allBlocks.contains(block)) m_allBlocks.append(block);
    for (MindMapBlock* block : transientBlocks) if (!m_allBlocks.contains(block)) m_allBlocks.append(block);
}

MoveMediaBlockCommand::~MoveMediaBlockCommand() {
    if (!m_node) return;
    const QVector<MindMapBlock*> current = m_node->blocks();
    for (MindMapBlock* block : m_allBlocks) {
        if (block && !current.contains(block)) delete block;
    }
}

void MoveMediaBlockCommand::apply(const QVector<MindMapBlock*>& blocks) {
    if (!m_node) return;
    for (MindMapBlock* block : m_allBlocks) {
        if (block && !blocks.contains(block) && block->type() == BlockType::Text)
            static_cast<TextBlock*>(block)->textItem()->setVisible(false);
    }
    m_node->restoreBlockSequence(blocks);
    if (m_node->selectedBlock() && m_node->selectedBlock() != m_mediaBlock)
        m_node->setSelectedBlock(m_mediaBlock);
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void MoveMediaBlockCommand::undo() {
    apply(m_beforeBlocks);
}

void MoveMediaBlockCommand::redo() {
    if (m_skipInitialRedo) {
        m_skipInitialRedo = false;
        return;
    }
    apply(m_afterBlocks);
}

// ===== SplitTextBlockCommand =====
SplitTextBlockCommand::SplitTextBlockCommand(MindMapNode* node, TextBlock* block, int blockIndex,
                                             int cursorPosition, QGraphicsScene* scene)
    : m_node(node), m_originalBlock(block), m_newBlock(nullptr), m_blockIndex(blockIndex),
      m_cursorPosition(cursorPosition), m_scene(scene), m_ownsNewBlock(false) {
    setText("拆分文本块");
    // 拆分前的完整内容：撤销时按 html 还原，段落/字符格式一并回来。
    if (block) {
        m_originalHtml = block->toHtml();
    }
}

SplitTextBlockCommand::~SplitTextBlockCommand() {
    if (m_ownsNewBlock && m_newBlock) {
        delete m_newBlock;
    }
}

void SplitTextBlockCommand::undo() {
    if (!m_node || !m_originalBlock) return;
    // 把新块从节点上摘下（takeBlockAt 不 delete，留给 redo 复用）
    if (m_newBlock) {
        int idx = m_node->blockIndex(m_newBlock);
        if (idx >= 0) {
            m_node->takeBlockAt(idx);
        }
        m_newBlock->textItem()->setVisible(false);
    }
    // 恢复原文本块内容（含段落/字符格式）
    if (!m_originalHtml.isEmpty()) {
        m_originalBlock->setHtml(m_originalHtml);
        m_originalBlock->setCursorPosition(m_cursorPosition);
    }
    m_ownsNewBlock = true;
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
}

void SplitTextBlockCommand::redo() {
    if (!m_node || !m_originalBlock) return;
    if (m_beforeHtml.isEmpty()) {
        // 首次执行：按文档坐标无损拆分（段落/字符格式随各自文档带走）
        m_originalBlock->setCursorPosition(m_cursorPosition);
        m_newBlock = m_originalBlock->splitAtCursorPreservingFormats();
        if (!m_newBlock) return;
        m_beforeHtml = m_originalBlock->toHtml();
        m_afterHtml = m_newBlock->toHtml();
    } else {
        m_originalBlock->setHtml(m_beforeHtml);
        if (!m_newBlock) {
            m_newBlock = new TextBlock(m_node);
        }
        m_newBlock->setHtml(m_afterHtml);
    }
    m_node->addBlock(m_newBlock, m_blockIndex + 1);
    m_newBlock->textItem()->setVisible(true);
    m_ownsNewBlock = false;
    relayout(m_node->getRoot());
    if (m_scene) m_scene->update();
    triggerSceneExpand(m_node);
    // 将焦点切换到新块
    m_node->startEditBlock(m_newBlock);
}

// ===== MergeTextBlockCommand =====
MergeTextBlockCommand::MergeTextBlockCommand(MindMapNode* node, int blockIndex, Direction dir, QGraphicsScene* scene)
    : m_node(node), m_blockIndex(blockIndex), m_direction(dir), m_scene(scene), m_newBlock(nullptr), m_ownsNewBlock(false) {
    setText(dir == WithPrevious ? "合并到上一个文本块" : "合并到下一个文本块");
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (dir == WithPrevious) {
        if (blockIndex > 0 && blockIndex < blocks.size()) {
            if (auto* prev = dynamic_cast<TextBlock*>(blocks[blockIndex - 1])) {
                m_firstHtml = prev->toHtml();
            }
            if (auto* curr = dynamic_cast<TextBlock*>(blocks[blockIndex])) {
                m_secondHtml = curr->toHtml();
                m_cursorPosition = m_node->blocks()[blockIndex - 1]->type() == BlockType::Text
                    ? static_cast<TextBlock*>(m_node->blocks()[blockIndex - 1])->toPlainText().length() + curr->cursorPosition()
                    : curr->cursorPosition();
            }
        }
    } else {
        if (blockIndex >= 0 && blockIndex + 1 < blocks.size()) {
            if (auto* curr = dynamic_cast<TextBlock*>(blocks[blockIndex])) {
                m_firstHtml = curr->toHtml();
                m_cursorPosition = curr->cursorPosition();
            }
            if (auto* next = dynamic_cast<TextBlock*>(blocks[blockIndex + 1])) {
                m_secondHtml = next->toHtml();
            }
        }
    }
}

MergeTextBlockCommand::~MergeTextBlockCommand() {
    if (m_ownsNewBlock && m_newBlock) {
        delete m_newBlock;
    }
}

void MergeTextBlockCommand::undo() {
    if (!m_node) return;
    if (m_direction == WithPrevious) {
        auto blocks = m_node->blocks();
        TextBlock* previous = nullptr;
        if (m_blockIndex - 1 >= 0 && m_blockIndex - 1 < blocks.size()) {
            if ((previous = dynamic_cast<TextBlock*>(blocks[m_blockIndex - 1]))) {
                previous->setHtml(m_firstHtml);
            }
        }
        if (!m_newBlock) {
            m_newBlock = new TextBlock(m_node);
            m_newBlock->setHtml(m_secondHtml);
        }
        m_node->addBlock(m_newBlock, m_blockIndex);
        m_ownsNewBlock = false;
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
        if (previous) {
            m_node->startEditBlock(previous);
            previous->setCursorPosition(m_cursorPosition);
        }
    } else {
        auto blocks = m_node->blocks();
        if (m_blockIndex >= 0 && m_blockIndex < blocks.size()) {
            if (auto* curr = dynamic_cast<TextBlock*>(blocks[m_blockIndex])) {
                curr->setHtml(m_firstHtml);
            }
        }
        if (!m_newBlock) {
            m_newBlock = new TextBlock(m_node);
            m_newBlock->setHtml(m_secondHtml);
        }
        m_node->addBlock(m_newBlock, m_blockIndex + 1);
        m_ownsNewBlock = false;
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
        if (auto* curr = dynamic_cast<TextBlock*>(m_node->blocks()[m_blockIndex])) {
            m_node->startEditBlock(curr);
            curr->setCursorPosition(m_cursorPosition);
        }
    }
}

void MergeTextBlockCommand::redo() {
    if (!m_node) return;
    if (m_direction == WithPrevious) {
        auto blocks = m_node->blocks();
        if (m_blockIndex <= 0 || m_blockIndex >= blocks.size()) return;
        auto* prev = dynamic_cast<TextBlock*>(blocks[m_blockIndex - 1]);
        auto* curr = dynamic_cast<TextBlock*>(blocks[m_blockIndex]);
        if (!prev || !curr) return;
        prev->mergeWith(curr);
        m_node->removeBlockAt(m_blockIndex);
        auto newBlocks = m_node->blocks();
        if (m_blockIndex - 1 >= 0 && m_blockIndex - 1 < newBlocks.size()) {
            if (auto* prev = dynamic_cast<TextBlock*>(newBlocks[m_blockIndex - 1])) {
                m_node->startEditBlock(prev);
                prev->setCursorPosition(m_cursorPosition);
            }
        }
        triggerSceneExpand(m_node);
    } else {
        auto blocks = m_node->blocks();
        if (m_blockIndex < 0 || m_blockIndex + 1 >= blocks.size()) return;
        auto* curr = dynamic_cast<TextBlock*>(blocks[m_blockIndex]);
        auto* next = dynamic_cast<TextBlock*>(blocks[m_blockIndex + 1]);
        if (!curr || !next) return;
        curr->mergeWith(next);
        m_node->removeBlockAt(m_blockIndex + 1);
        auto newBlocks = m_node->blocks();
        if (m_blockIndex >= 0 && m_blockIndex < newBlocks.size()) {
            if (auto* curr = dynamic_cast<TextBlock*>(newBlocks[m_blockIndex])) {
                m_node->startEditBlock(curr);
                curr->setCursorPosition(m_cursorPosition);
            }
        }
        triggerSceneExpand(m_node);
    }
}


// ===== ResizeImageCommand =====
ResizeImageCommand::ResizeImageCommand(MindMapNode* node, int blockIndex, const QSizeF& oldSize, const QSizeF& newSize, QGraphicsScene* scene)
    : m_node(node), m_blockIndex(blockIndex), m_oldSize(oldSize), m_newSize(newSize), m_scene(scene) {
    setText("缩放图片");
}

void ResizeImageCommand::undo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* ib = dynamic_cast<ImageBlock*>(blocks[m_blockIndex])) {
        ib->setImageSize(m_oldSize);
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        // 【新增】撤销缩放后，自动调整文本块宽度与节点一致
        m_node->autoFitTextBlockWidths();
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
    }
}

void ResizeImageCommand::redo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* ib = dynamic_cast<ImageBlock*>(blocks[m_blockIndex])) {
        ib->setImageSize(m_newSize);
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        // 【新增】缩放图片后，自动调整文本块宽度与节点一致
        m_node->autoFitTextBlockWidths();
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
    }
}

// ===== EditFormulaCommand =====
EditFormulaCommand::EditFormulaCommand(MindMapNode* node, int blockIndex, const QString& oldLatex, const QString& newLatex,
                                        const QPixmap& oldPixmap, const QPixmap& newPixmap, QGraphicsScene* scene)
    : m_node(node), m_blockIndex(blockIndex), m_oldLatex(oldLatex), m_newLatex(newLatex),
      m_oldPixmap(oldPixmap), m_newPixmap(newPixmap), m_scene(scene) {
    setText("编辑公式");
}

void EditFormulaCommand::undo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* fb = dynamic_cast<FormulaBlock*>(blocks[m_blockIndex])) {
        fb->setLatexCode(m_oldLatex);
        fb->updatePixmap(m_oldPixmap);
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        // 【新增】撤销公式编辑后，自动调整文本块宽度与节点一致
        m_node->autoFitTextBlockWidths();
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
    }
}

void EditFormulaCommand::redo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* fb = dynamic_cast<FormulaBlock*>(blocks[m_blockIndex])) {
        fb->setLatexCode(m_newLatex);
        fb->updatePixmap(m_newPixmap);
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        // 【新增】编辑公式后，自动调整文本块宽度与节点一致
        m_node->autoFitTextBlockWidths();
        m_node->calcNodeSize();
        relayout(m_node->getRoot());
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
    }
}

// ===== FormatTextCommand =====
FormatTextCommand::FormatTextCommand(MindMapNode* node, int blockIndex,
                                     const QString& oldHtml, const QString& newHtml,
                                     int cursorPos, QGraphicsScene* scene)
    : m_node(node), m_blockIndex(blockIndex), m_oldHtml(oldHtml), m_newHtml(newHtml),
      m_cursorPos(cursorPos), m_scene(scene) {
    setText("修改文字格式");
}

void FormatTextCommand::undo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* tb = dynamic_cast<TextBlock*>(blocks[m_blockIndex])) {
        tb->setHtml(m_oldHtml);
        m_node->calcNodeSize();
        MindMapNode* root = m_node->getRoot();
        if (root) {
            root->clearEdges();
            root->layoutTree(0, 0);
            root->adjustLayout();
            root->updateAllEdges();
        }
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
        // 恢复编辑状态
        if (!m_node->isEditing()) {
            m_node->startEditBlock(tb);
        }
        QTextCursor cursor = tb->textItem()->textCursor();
        cursor.setPosition(qMin(m_cursorPos, tb->toPlainText().length()));
        tb->textItem()->setTextCursor(cursor);
    }
}

void FormatTextCommand::redo() {
    if (!m_node) return;
    auto blocks = m_node->blocks();
    if (m_blockIndex < 0 || m_blockIndex >= blocks.size()) return;
    if (auto* tb = dynamic_cast<TextBlock*>(blocks[m_blockIndex])) {
        tb->setHtml(m_newHtml);
        m_node->calcNodeSize();
        MindMapNode* root = m_node->getRoot();
        if (root) {
            root->clearEdges();
            root->layoutTree(0, 0);
            root->adjustLayout();
            root->updateAllEdges();
        }
        if (m_scene) m_scene->update();
        triggerSceneExpand(m_node);
        // 恢复编辑状态
        if (!m_node->isEditing()) {
            m_node->startEditBlock(tb);
        }
        QTextCursor cursor = tb->textItem()->textCursor();
        cursor.setPosition(qMin(m_cursorPos, tb->toPlainText().length()));
        tb->textItem()->setTextCursor(cursor);
    }
}

// ===== ResizeNodeCommand =====
ResizeNodeCommand::ResizeNodeCommand(MindMapNode* node, qreal oldWidth, qreal newWidth, QGraphicsScene* scene)
    : m_node(node), m_oldWidth(oldWidth), m_newWidth(newWidth), m_scene(scene) {
    setText("调整节点宽度");
}

void ResizeNodeCommand::undo() {
    if (!m_node) return;
    m_node->setNodeContentWidth(m_oldWidth);
    MindMapNode* root = m_node->getRoot();
    if (root) {
        QPointF rootPos = root->pos();
        root->clearEdges();
        root->layoutTree(rootPos.x(), rootPos.y());
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (m_scene) m_scene->update();
}

void ResizeNodeCommand::redo() {
    if (!m_node) return;
    m_node->setNodeContentWidth(m_newWidth);
    MindMapNode* root = m_node->getRoot();
    if (root) {
        QPointF rootPos = root->pos();
        root->clearEdges();
        root->layoutTree(rootPos.x(), rootPos.y());
        root->adjustLayout();
        root->updateAllEdges();
    }
    if (m_scene) m_scene->update();
}
