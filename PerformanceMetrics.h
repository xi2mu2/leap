#pragma once

#include <QElapsedTimer>
#include <QtGlobal>

namespace PerformanceMetrics {

enum class Stage {
    ViewPaint,
    NodePaint,
    MouseMove,
    PanMouseMove,
    LayoutTree,
    LayoutNodeSizeRefresh,
    LayoutPositions,
    LayoutAdjust,
    HoverQuery,
    HoverStateUpdate,
    EdgeClear,
    EdgeRebuild,
    TextChange,
    MiniMapPaint,
    SavePerform,
    SaveTotal,
    SaveSerialize,
    SaveImageEncode,
    SaveJsonEncode,
    SaveFileWrite,
    Count
};

bool isEnabled();
void record(Stage stage, qint64 elapsedNanoseconds);

class ScopedTimer {
public:
    explicit ScopedTimer(Stage stage, bool enabled = true);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    Stage m_stage;
    bool m_active;
    QElapsedTimer m_timer;
};

} // namespace PerformanceMetrics
