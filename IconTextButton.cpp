#include "IconTextButton.h"

#include <QEvent>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace UiColors {
    extern bool isDarkMode();
}

IconTextButton::IconTextButton(const QString& iconName, const QString& text,
               int iconFontSize, int textFontSize,
               QWidget* parent) : QToolButton(parent),
               m_iconName(iconName), m_text(text),
               m_iconFontSize(iconFontSize), m_textFontSize(textFontSize) {
    setFixedSize(52, 50);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAutoRaise(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 4, 2, 4);
    layout->setSpacing(1);

    m_iconLabel = new QLabel();
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(qMax(24, iconFontSize + 4), 25);
    layout->addWidget(m_iconLabel, 3, Qt::AlignCenter);

    m_textLabel = new QLabel(text);
    m_textLabel->setAlignment(Qt::AlignCenter);
    m_textLabel->setFixedHeight(15);
    layout->addWidget(m_textLabel, 2, Qt::AlignCenter);

    applyStyleSheet();
}

void IconTextButton::changeEvent(QEvent* event) {
    if (event->type() == QEvent::EnabledChange) {
        applyStyleSheet();
    }
    QToolButton::changeEvent(event);
}

void IconTextButton::applyStyleSheet() {
    QString textColor = UiColors::isDarkMode() ? "#e4e6eb" : "#4b5563";
    QString hoverBg = UiColors::isDarkMode() ? "rgba(74, 74, 74, 0.86)" : "rgba(241, 245, 249, 0.96)";
    QString pressedBg = UiColors::isDarkMode() ? "rgba(90, 90, 90, 0.92)" : "rgba(226, 232, 240, 0.98)";
    QString hoverBorder = UiColors::isDarkMode() ? "#5f6368" : "#cbd5e1";
    QString disabledColor = UiColors::isDarkMode() ? "#666666" : "#bdc1c6";

    const QString accentRule = m_accentColor.isValid()
        ? QStringLiteral("border-bottom: 4px solid %1;").arg(m_accentColor.name())
        : QString();
    setStyleSheet(QString(
        "IconTextButton {"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 8px;"
        "  padding: 0px;"
        "  %4"
        "}"
        "IconTextButton:hover {"
        "  background-color: %1;"
        "  border-color: %3;"
        "  %4"
        "}"
        "IconTextButton:pressed {"
        "  background-color: %2;"
        "  border-color: %3;"
        "  %4"
        "}"
        "IconTextButton:disabled {"
        "  background-color: transparent;"
        "  border-color: transparent;"
        "}"
    ).arg(hoverBg, pressedBg, hoverBorder, accentRule));

    const QString effectiveColor = isEnabled() ? textColor : disabledColor;
    if (m_textLabel) {
        m_textLabel->setStyleSheet(QString(
            "font-size: %1px; color: %2; font-weight: 600;"
        ).arg(m_textFontSize).arg(effectiveColor));
    }

    updateIconPixmap(effectiveColor);
}

void IconTextButton::setIconEmoji(const QString& iconName) {
    m_iconName = iconName;
    m_iconPath.clear();
    applyStyleSheet();
}

void IconTextButton::setIconFromFile(const QString& iconFilePath) {
    m_iconPath = iconFilePath;
    applyStyleSheet();
}

void IconTextButton::setAccentColor(const QColor& color) {
    m_accentColor = color;
    applyStyleSheet();
}

void IconTextButton::updateIconPixmap(const QString& color) {
    if (!m_iconLabel) return;

    const int size = qBound(16, m_iconFontSize + 2, 30);

    // 【新增】插件图标走外部文件路径：SVG 仍按主题替换 currentColor，
    // 位图（png/jpg）原样缩放，作者自己保证深色背景下可见。
    if (!m_iconPath.isEmpty()) {
        QFile iconFile(m_iconPath);
        if (iconFile.open(QIODevice::ReadOnly)) {
            const QByteArray data = iconFile.readAll();
            iconFile.close();

            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);

            if (m_iconPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                QByteArray svg = data;
                svg.replace("currentColor", color.toUtf8());
                QSvgRenderer renderer(svg);
                renderer.render(&painter);
            } else {
                QPixmap loaded;
                if (loaded.loadFromData(data)) {
                    painter.drawPixmap(QRect(0, 0, size, size),
                                       loaded.scaled(size, size, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
                }
            }
            painter.end();

            if (!pixmap.isNull()) {
                m_iconLabel->setPixmap(pixmap);
                return;
            }
        }
        // 文件读不到就落到下面的内置图标，至少不会变成空白按钮。
    }

    QFile file(QString(":/icons/%1.svg").arg(m_iconName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_iconLabel->clear();
        return;
    }

    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.toUtf8());

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(svg);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();

    m_iconLabel->setPixmap(pixmap);
}
