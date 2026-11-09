#include "MindMapHoverTracker.h"

#include "MindMapNode.h"

#include <QGraphicsItem>
#include <QGraphicsScene>

MindMapHoverTracker::~MindMapHoverTracker() {
    clear();
}

void MindMapHoverTracker::forgetNode(MindMapNode* node) {
    m_hoveredNodes.removeAll(node);
}

void MindMapHoverTracker::clear(QGraphicsScene* scene) {
    for (MindMapNode* node : m_hoveredNodes) {
        bool stillAlive = false;
        if (scene) {
            for (QGraphicsItem* item : scene->items()) {
                if (item == node) {
                    stillAlive = true;
                    break;
                }
            }
        }
        if (stillAlive && node->m_hoverOwner == this) node->m_hoverOwner = nullptr;
    }
    m_hoveredNodes.clear();
}

void MindMapHoverTracker::update(QGraphicsScene* scene, const QPointF& scenePosition,
                                 const QList<QGraphicsItem*>& nearbyItems) {
    if (!scene) return;

    QVector<MindMapNode*> candidates;
    for (QGraphicsItem* item : nearbyItems) {
        for (QGraphicsItem* ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
            if (auto* node = dynamic_cast<MindMapNode*>(ancestor)) {
                if (!candidates.contains(node)) {
                    candidates.append(node);
                }
                break;
            }
        }
    }

    QVector<MindMapNode*> updatedHoveredNodes;
    QVector<MindMapNode*> processed;
    auto updateNode = [&](MindMapNode* node) {
        if (!node || processed.contains(node)) return;
        if (node->scene() != scene) return;
        processed.append(node);

        const bool hovered = node->updateHoverState(node->mapFromScene(scenePosition));
        if (hovered) {
            node->m_hoverOwner = this;
            updatedHoveredNodes.append(node);
        } else if (node->m_hoverOwner == this) {
            node->m_hoverOwner = nullptr;
        }
    };

    for (MindMapNode* node : m_hoveredNodes) {
        updateNode(node);
    }
    for (MindMapNode* node : candidates) {
        updateNode(node);
    }
    m_hoveredNodes = updatedHoveredNodes;
}
