#include "FloatingEditToolbar.h"

#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QToolButton>

namespace {

QIcon renderSvgIcon(const QString& iconName, const QString& color) {
    QFile file(QStringLiteral(":/icons/%1.svg").arg(iconName));
    if (!file.open(QIODevice::ReadOnly)) {
        return QIcon();
    }

    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.toUtf8());

    QSvgRenderer renderer(svg);
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter);
    return QIcon(pixmap);
}

} // namespace

FloatingEditToolbar::FloatingEditToolbar(QWidget* parent)
    : QFrame(parent) {
    setObjectName("FloatingEditToolbar");
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    connect(addButton("bold", QStringLiteral("加粗")), &QToolButton::clicked, this, &FloatingEditToolbar::boldRequested);
    connect(addButton("italic", QStringLiteral("斜体")), &QToolButton::clicked, this, &FloatingEditToolbar::italicRequested);
    connect(addButton("underline", QStringLiteral("下划线")), &QToolButton::clicked, this, &FloatingEditToolbar::underlineRequested);

    m_highlightButton = addButton("highlight", QStringLiteral("高亮"));
    m_highlightButton->setObjectName("highlightMainButton");
    connect(m_highlightButton, &QToolButton::clicked, this, &FloatingEditToolbar::highlightRequested);
    m_highlightDropButton = new QToolButton(this);
    m_highlightDropButton->setArrowType(Qt::DownArrow);
    m_highlightDropButton->setToolTip(QStringLiteral("选择高亮颜色"));
    m_highlightDropButton->setFixedSize(16, 28);
    m_highlightDropButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_highlightDropButton);
    connect(m_highlightDropButton, &QToolButton::clicked, this, &FloatingEditToolbar::highlightColorRequested);

    layout->addSpacing(3);
#if 0
    connect(addButton("align-left", QStringLiteral("左对齐")), &QToolButton::clicked, this, &FloatingEditToolbar::alignLeftRequested);
    connect(addButton("align-right", QStringLiteral("右对齐")), &QToolButton::clicked, this, &FloatingEditToolbar::alignRightRequested);

#endif
    applyTheme(false);
    setHighlightColor(m_highlightColor);
}

QToolButton* FloatingEditToolbar::addButton(const QString& iconName, const QString& tooltip) {
    auto* button = new QToolButton(this);
    button->setProperty("iconName", iconName);
    button->setIconSize(QSize(18, 18));
    button->setText(QString());
    button->setToolTip(tooltip);
    button->setFixedSize(30, 28);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(false);
    layout()->addWidget(button);
    return button;
}

void FloatingEditToolbar::applyTheme(bool dark) {
    const QString panelBg = dark ? QStringLiteral("rgba(37, 37, 38, 236)") : QStringLiteral("rgba(255, 255, 255, 242)");
    const QString border = dark ? QStringLiteral("#4b5563") : QStringLiteral("#cbd5e1");
    const QString text = dark ? QStringLiteral("#e5e7eb") : QStringLiteral("#334155");
    const QString hover = dark ? QStringLiteral("rgba(75, 85, 99, 210)") : QStringLiteral("rgba(241, 245, 249, 245)");
    const QString pressed = dark ? QStringLiteral("rgba(100, 116, 139, 230)") : QStringLiteral("rgba(226, 232, 240, 250)");

    setStyleSheet(QString(
        "QFrame#FloatingEditToolbar {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 9px;"
        "}"
        "QToolButton {"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  color: %3;"
        "}"
        "QToolButton:hover {"
        "  background-color: %4;"
        "  border-color: %2;"
        "}"
        "QToolButton:pressed {"
        "  background-color: %5;"
        "}"
    ).arg(panelBg, border, text, hover, pressed));

    updateButtonIcons(text);
}

void FloatingEditToolbar::setHighlightColor(const QColor& color) {
    if (!color.isValid() || !m_highlightButton) return;
    m_highlightColor = color;
    m_highlightButton->setStyleSheet(
        QStringLiteral("QToolButton#highlightMainButton { border-bottom: 4px solid %1; }")
            .arg(color.name()));
}

void FloatingEditToolbar::updateButtonIcons(const QString& color) {
    const auto buttons = findChildren<QToolButton*>();
    for (auto* button : buttons) {
        const QString iconName = button->property("iconName").toString();
        if (iconName.isEmpty()) {
            continue;
        }

        const QString iconColor = color;
        button->setIcon(renderSvgIcon(iconName, iconColor));
        button->setText(QString());
    }
}
