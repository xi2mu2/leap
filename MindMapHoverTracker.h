#pragma once

#include <QList>
#include <QPointF>
#include <QVector>

class MindMapNode;
class QGraphicsItem;
class QGraphicsScene;

class MindMapHoverTracker {
public:
    ~MindMapHoverTracker();

    void update(QGraphicsScene* scene, const QPointF& scenePosition,
                const QList<QGraphicsItem*>& nearbyItems);
    void forgetNode(MindMapNode* node);
    void clear(QGraphicsScene* scene = nullptr);

private:
    QVector<MindMapNode*> m_hoveredNodes;
};
