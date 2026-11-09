#pragma once

#include "MindMapHoverTracker.h"
#include <QGraphicsView>
#include <QList>
#include <QSet>
#include <QLineF>
#include <QMap>
#include <QVector>

class QGraphicsScene;
class QPushButton;
class MindMapNode;
class MindMapMiniMap;
class QGraphicsRectItem;
class QParallelAnimationGroup;

// 自定义思维导图视图
class MindMapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MindMapView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void setCurrentFilePath(const QString& path) { m_currentFilePath = path; }
    QString currentFilePath() const { return m_currentFilePath; }

    void ensureSceneRectContains();
    void ensureNodeVisible(MindMapNode* node, qreal margin = 50);
    // 仅当节点超出编辑安全区时调整视野；安全区内保持当前视野。
    void ensureNodeInEditSafeZone(MindMapNode* node, qreal horizontalMargin = 120,
                                  qreal verticalMargin = 80);

    void setInsertImageButton(QPushButton* btn) { m_insertImgBtn = btn; }
    void updateInsertImageButtonState();
    void notifyGuideEditingExitedByKey();
    // 清理场景前释放所有交互态，避免鼠标事件继续持有已删除节点指针。
    void resetForSceneChange();
    int selectedNodeCount() const;
    void clearNodeSelection(MindMapNode* exceptNode = nullptr);
    QVector<MindMapNode*> selectedNodes() const;

signals:
    void saveRequested();
    void openRequested();
    void newRequested();
    void zoomChanged();           // 【优化】缩放变化时发出信号
    void editStateChanged(bool isEditing);  // 【优化】编辑状态变化
    void guideNodeSelected();
    void guideChildCreated();
    void guideSiblingCreated();
    void guideEditingStarted();
    void guideEditingExitedByKey();
    void guideViewportDragged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    bool m_dragging;
    QPoint m_lastPos;
    QPoint m_rightDragStartPos;
    bool m_rightDragReported = false;
    QString m_currentFilePath;
    QPushButton* m_insertImgBtn = nullptr;
    MindMapMiniMap* m_miniMap = nullptr;
    MindMapHoverTracker m_hoverTracker;
    QRectF m_hoverQueryRect;
    QList<QGraphicsItem*> m_hoverQueryItems;
    bool m_hasHoverQueryCache = false;
    bool m_hoverQueryCacheDirty = false;
    bool m_miniMapUpdatePending = false;
    QParallelAnimationGroup* m_viewportAnimation = nullptr;
    static const QSet<int> s_navKeys;

    bool m_isDraggingNode = false;
    MindMapNode* m_draggedNode = nullptr;
    MindMapNode* m_dropTargetNode = nullptr;
    int m_dropInsertIndex = -1;
    QPointF m_dragStartPos;
    QGraphicsItem* m_dropIndicator = nullptr;
    QGraphicsPathItem* m_dragProxy = nullptr;

    // 【新增】节点宽度拖拽调整状态
    bool m_isResizingNode = false;
    MindMapNode* m_resizingNode = nullptr;
    int m_resizeEdge = 0;  // 0=None, 1=Left, 2=Right
    QPointF m_resizeStartPos;
    qreal m_resizeStartWidth = 0;
    // 【新增】记录拖动开始时节点中心在视野中的位置，用于重新布局后恢复视野
    QPoint m_resizeViewportCenterPos;

    bool m_dragCandidate = false;
    QPoint m_dragCandidateStartViewportPos;
    MindMapNode* m_clickCandidateNode = nullptr;
    bool m_isRubberSelecting = false;
    QPointF m_rubberBandStart;
    QGraphicsRectItem* m_rubberBandItem = nullptr;
    bool m_isDraggingSelection = false;
    QVector<MindMapNode*> m_multiDragNodes;
    QVector<QPointF> m_multiDragStartPositions;


    void updateDropIndicator();
    void resetDragState();

    void updateRubberBandSelection(const QRectF& selectionRect);
    bool hasSelectedAncestor(MindMapNode* node, const QVector<MindMapNode*>& nodes) const;
    QVector<MindMapNode*> selectedRootNodes() const;
    void copySelectedNodesToClipboard();
    void pasteMultiClipboardTo(MindMapNode* targetNode);
    MindMapNode* firstSelectedNode() const;
    void deleteSelectedNodes();
    void resetStateForSceneChange();
    void updateMiniMapGeometry();
    void showMiniMapForViewportChange();
    void scheduleMiniMapForViewportChange();
    void invalidateHoverQueryCache();
    void animateViewportTo(int horizontalValue, int verticalValue);
    void stopViewportAnimation();




};


