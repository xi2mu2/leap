#include "PerformanceMetrics.h"

#include <QDebug>
#include <QStringList>

#include <array>

namespace PerformanceMetrics {
namespace {

constexpr qint64 kReportIntervalMs = 3000;
constexpr std::array<const char*, static_cast<size_t>(Stage::Count)> kStageNames = {
    "view.paint",
    "node.paint",
    "view.mouseMove",
    "pan.mouseMove",
    "layout.total",
    "layout.refreshNodeSizes",
    "layout.positions",
    "layout.adjust",
    "hover.query",
    "hover.stateUpdate",
    "edges.clear",
    "edges.rebuild",
    "text.documentChanged",
    "minimap.paint",
    "save.perform",
    "save.total",
    "save.serialize",
    "save.imageEncode",
    "save.jsonEncode",
    "save.fileWrite"
};

struct Aggregate {
    quint64 count = 0;
    qint64 totalNanoseconds = 0;
    qint64 maxNanoseconds = 0;
};

struct MetricsState {
    QElapsedTimer reportTimer;
    std::array<Aggregate, static_cast<size_t>(Stage::Count)> aggregates{};

    MetricsState() {
        reportTimer.start();
    }
};

MetricsState& state() {
    static MetricsState instance;
    return instance;
}

} // namespace

bool isEnabled() {
    static const bool enabled = qEnvironmentVariable("LEAP_PERF") == QStringLiteral("1");
    return enabled;
}

ScopedTimer::ScopedTimer(Stage stage, bool enabled)
    : m_stage(stage), m_active(enabled && isEnabled()) {
    if (m_active) {
        m_timer.start();
    }
}

ScopedTimer::~ScopedTimer() {
    if (m_active) {
        record(m_stage, m_timer.nsecsElapsed());
    }
}

void record(Stage stage, qint64 elapsedNanoseconds) {
    if (!isEnabled() || elapsedNanoseconds < 0) return;

    QString report;
    MetricsState& metrics = state();
    // All current call sites are on the GUI thread; avoid adding lock contention
    // to high-frequency paint callbacks while profiling is enabled.
    const size_t index = static_cast<size_t>(stage);
    if (index >= metrics.aggregates.size()) return;

    Aggregate& aggregate = metrics.aggregates[index];
    ++aggregate.count;
    aggregate.totalNanoseconds += elapsedNanoseconds;
    aggregate.maxNanoseconds = qMax(aggregate.maxNanoseconds, elapsedNanoseconds);

    const qint64 windowMilliseconds = metrics.reportTimer.elapsed();
    if (windowMilliseconds < kReportIntervalMs) return;

    QStringList entries;
    for (size_t i = 0; i < metrics.aggregates.size(); ++i) {
        const Aggregate& current = metrics.aggregates[i];
        if (current.count == 0) continue;

        const double averageMilliseconds =
            static_cast<double>(current.totalNanoseconds) / current.count / 1'000'000.0;
        const double maxMilliseconds = static_cast<double>(current.maxNanoseconds) / 1'000'000.0;
        const double totalMilliseconds = static_cast<double>(current.totalNanoseconds) / 1'000'000.0;
        entries.append(QStringLiteral("%1 n=%2 avg=%3ms max=%4ms total=%5ms")
                           .arg(QString::fromLatin1(kStageNames[i]))
                           .arg(current.count)
                           .arg(averageMilliseconds, 0, 'f', 3)
                           .arg(maxMilliseconds, 0, 'f', 3)
                           .arg(totalMilliseconds, 0, 'f', 1));
    }

    report = QStringLiteral("[leap-perf] window=%1ms %2")
                 .arg(windowMilliseconds)
                 .arg(entries.join(QStringLiteral(" | ")));
    metrics.aggregates.fill(Aggregate{});
    metrics.reportTimer.restart();

    if (!report.isEmpty()) {
        qInfo().noquote() << report;
    }
}

} // namespace PerformanceMetrics
