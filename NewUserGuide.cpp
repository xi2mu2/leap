#include "NewUserGuide.h"

#include <QApplication>
#include <QLabel>
#include <QSettings>
#include <QTimer>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
constexpr auto kCompleted = "newUserGuideCompleted";
const QStringList kCaptions = {
    QStringLiteral("用鼠标左键选中一个节点"),
    QStringLiteral("按Tab键创建子节点"),
    QStringLiteral("按Enter键创建同级节点"),
    QStringLiteral("按空格键开始编辑节点"),
    QStringLiteral("按Tab或Enter键退出编辑"),
    QStringLiteral("按住鼠标右键拖动视野"),
    QStringLiteral("恭喜您已学会了leap的基本操作，让思维在节点之间尽情跳跃吧！")
};
}

NewUserGuideOverlay::NewUserGuideOverlay(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setWordWrap(true);
    setMinimumHeight(48);
    setMargin(12);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setDarkMode(false);
    hide();
}

void NewUserGuideOverlay::setDarkMode(bool dark) {
    setStyleSheet(dark
        ? QStringLiteral("QLabel { background: rgba(30,41,59,235); color: #f8fafc; border: 1px solid #475569; border-radius: 8px; font-size: 16px; }")
        : QStringLiteral("QLabel { background: rgba(255,255,255,240); color: #1f2937; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 16px; }") );
}

void NewUserGuideOverlay::reposition() {
    if (!parentWidget()) return;
    const int margin = 24;
    const int w = qMin(parentWidget()->width() - margin * 2, 720);
    setFixedWidth(qMax(240, w));
    adjustSize();
    const int centeredY = parentWidget()->height() * 2 / 3 - height() / 2;
    const int minY = margin;
    const int maxY = qMax(minY, parentWidget()->height() - height() - margin);
    move((parentWidget()->width() - width()) / 2,
         qBound(minY, centeredY, maxY));
}

void NewUserGuideOverlay::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    reposition();
}

NewUserGuideController::NewUserGuideController(QWidget* host, QObject* parent)
    : QObject(parent), m_host(host), m_overlay(new NewUserGuideOverlay(host)) {}

bool NewUserGuideController::completed() const {
    return QSettings().value(QString::fromLatin1(kCompleted), false).toBool();
}

void NewUserGuideController::setDarkMode(bool dark) { if (m_overlay) m_overlay->setDarkMode(dark); }

void NewUserGuideController::start() {
    m_manualRun = true;
    m_step = 0;
    showStep();
}

void NewUserGuideController::showStep() {
    if (!m_overlay || m_step < 0) return;
    m_overlay->setText(kCaptions.at(m_step));
    m_overlay->show();
    m_overlay->raise();
    m_overlay->reposition();
    if (m_step == kCaptions.size() - 1) {
        QSettings().setValue(QString::fromLatin1(kCompleted), true);
        QTimer::singleShot(3000, m_overlay.data(), &QWidget::hide);
    }
}

void NewUserGuideController::advance(int expectedStep) {
    if (m_step != expectedStep) return;
    ++m_step;
    showStep();
}
void NewUserGuideController::nodeSelected() { advance(0); }
void NewUserGuideController::childCreated() { advance(1); }
void NewUserGuideController::siblingCreated() { advance(2); }
void NewUserGuideController::editingStarted() { advance(3); }
void NewUserGuideController::editingExitedByKey() { advance(4); }
void NewUserGuideController::viewportDragged() { advance(5); }
