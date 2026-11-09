#include "SettingsWidgets.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QTextLayout>
#include <QTextOption>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace {

// 与 MindMapNode.h 中的绘制常量保持一致，预览才能如实反映最终效果。
constexpr qreal kMarginH = 6.0;       // MindMapNode::MARGIN_H
constexpr qreal kMarginV = 4.0;       // MindMapNode::MARGIN_V
constexpr qreal kCornerRadius = 8.0;  // MindMapNode::CORNER_RADIUS

void repolish(QWidget* widget) {
    if (!widget) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString formatHex(const QColor& color) {
    return color.name(QColor::HexRgb).toUpper();
}

// 用与节点绘制相同的方式测量多行文本高度。
qreal measureTextHeight(const QString& text, const QFont& font, qreal width, qreal lineSpacing) {
    if (text.isEmpty() || width <= 0) return 0.0;
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal y = 0.0;
    const qreal lineHeight = QFontMetricsF(font).height() * qMax(0.8, lineSpacing);
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += lineHeight;
    }
    layout.endLayout();
    return y;
}

void drawNodeText(QPainter* painter, const QRectF& rect, const MindMapStyle& style, const QString& text) {
    const QFont font = style.font();
    const qreal textWidth = rect.width() - 2 * kMarginH;
    if (textWidth <= 0) return;

    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(style.alignment);
    layout.setTextOption(option);

    qreal y = 0.0;
    const qreal lineHeight = QFontMetricsF(font).height() * qMax(0.8, style.lineSpacing);
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(textWidth);
        line.setPosition(QPointF(0, y));
        y += lineHeight;
    }
    layout.endLayout();

    painter->setPen(style.textColor);
    const QPointF origin(rect.left() + kMarginH, rect.top() + (rect.height() - y) / 2.0);
    layout.draw(painter, origin);
}

// 与 MindMapNode::paint() 一致：先填满圆角路径，再把笔宽内缩一半描边。
void drawNodeShape(QPainter* painter, const QRectF& rect, const MindMapStyle& style) {
    QPainterPath fillPath;
    if (style.shape == MindMapNodeShape::RoundedRect) {
        fillPath.addRoundedRect(rect, kCornerRadius, kCornerRadius);
    } else {
        fillPath.addRect(rect);
    }
    painter->fillPath(fillPath, style.fillColor);

    const qreal penWidth = qMax(0, style.borderWidth);
    if (penWidth <= 0) return;

    QPen pen(style.effectiveBorderColor());
    pen.setWidthF(penWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QRectF borderRect = rect.adjusted(penWidth / 2.0, penWidth / 2.0,
                                            -penWidth / 2.0, -penWidth / 2.0);
    QPainterPath borderPath;
    if (style.shape == MindMapNodeShape::RoundedRect) {
        borderPath.addRoundedRect(borderRect, kCornerRadius, kCornerRadius);
    } else {
        borderPath.addRect(borderRect);
    }
    painter->drawPath(borderPath);
}

} // namespace

// ============================================================
// ColorField
// ============================================================

ColorField::ColorField(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("colorField"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(34);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 6, 0);
    layout->setSpacing(8);

    m_swatch = new QPushButton(this);
    m_swatch->setObjectName(QStringLiteral("colorSwatch"));
    m_swatch->setFixedSize(22, 22);
    m_swatch->setCursor(Qt::PointingHandCursor);
    m_swatch->setToolTip(QStringLiteral("选择颜色"));
    layout->addWidget(m_swatch, 0, Qt::AlignVCenter);

    m_hex = new QLabel(this);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(13);
    m_hex->setFont(mono);
    m_hex->setMinimumWidth(64);
    layout->addWidget(m_hex, 1);

    m_reset = new QToolButton(this);
    m_reset->setObjectName(QStringLiteral("colorReset"));
    m_reset->setText(QStringLiteral("重置"));
    m_reset->setCursor(Qt::PointingHandCursor);
    m_reset->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(m_reset, 0, Qt::AlignVCenter);

    connect(m_swatch, &QPushButton::clicked, this, &ColorField::chooseColor);
    connect(m_reset, &QToolButton::clicked, this, &ColorField::resetToDefault);

    refresh();
}

void ColorField::setColor(const QColor& color) {
    if (!color.isValid() || color == m_color) return;
    m_color = color;
    refresh();
}

void ColorField::setDefaultColor(const QColor& color) {
    m_defaultColor = color;
    refresh();
}

void ColorField::setFieldLabel(const QString& name) {
    QWidget::setAccessibleName(name);
    m_swatch->setToolTip(QStringLiteral("选择颜色：%1").arg(name));
}

void ColorField::setEnabled(bool enabled) {
    if (m_fieldEnabled == enabled) return;
    m_fieldEnabled = enabled;
    QWidget::setEnabled(enabled);
    setProperty("fieldDisabled", !enabled);
    repolish(this);
}

void ColorField::chooseColor() {
    if (!m_fieldEnabled) return;
    const QColor picked = QColorDialog::getColor(m_color, this, QStringLiteral("选择颜色"));
    if (!picked.isValid() || picked == m_color) return;
    m_color = picked;
    refresh();
    emit colorChanged(m_color);
}

void ColorField::resetToDefault() {
    if (!m_defaultColor.isValid() || m_defaultColor == m_color) return;
    m_color = m_defaultColor;
    refresh();
    emit colorChanged(m_color);
}

void ColorField::refresh() {
    if (m_swatch) {
        m_swatch->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: 1px solid %2; border-radius: 4px;"
            " min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px; padding: 0; }")
            .arg(m_color.name(QColor::HexRgb),
                 QStringLiteral("rgba(0, 0, 0, 60)")));
    }
    if (m_hex) m_hex->setText(formatHex(m_color));
    if (m_reset) m_reset->setVisible(m_defaultColor.isValid() && m_color != m_defaultColor);
}

// ============================================================
// SegmentedControl
// ============================================================

SegmentedControl::SegmentedControl(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("segmentedControl"));
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(3, 3, 3, 3);
    m_layout->setSpacing(2);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
        setCurrentIndex(id);
    });
}

void SegmentedControl::addItem(const QString& text, const QVariant& data) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("segmentButton"));
    button->setText(text);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::TabFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    const int id = m_buttons.size();
    m_buttons.append(button);
    m_group->addButton(button, id);
    m_layout->addWidget(button);
    if (!data.isNull()) button->setProperty("segmentData", data);

    if (m_current < 0) setCurrentIndex(0);
}

QVariant SegmentedControl::currentData() const {
    if (m_current < 0 || m_current >= m_buttons.size()) return {};
    return m_buttons.at(m_current)->property("segmentData");
}

void SegmentedControl::setCurrentIndex(int index) {
    if (index < 0 || index >= m_buttons.size()) return;
    if (m_current == index && m_buttons.at(index)->isChecked()) return;
    m_current = index;
    m_updating = true;
    for (int i = 0; i < m_buttons.size(); ++i) {
        m_buttons.at(i)->setChecked(i == index);
    }
    m_updating = false;
    emit currentChanged(m_current);
}

void SegmentedControl::setCurrentData(const QVariant& data) {
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i)->property("segmentData") == data) {
            setCurrentIndex(i);
            return;
        }
    }
}

// ============================================================
// StylePreviewWidget
// ============================================================

StylePreviewWidget::StylePreviewWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(220);
}

void StylePreviewWidget::setStyles(const MindMapLevelStyles& styles) {
    m_styles = styles;
    update();
}

void StylePreviewWidget::setActiveLevel(int level) {
    const int clamped = qBound(0, level, 2);
    if (m_activeLevel == clamped) return;
    m_activeLevel = clamped;
    update();
}

void StylePreviewWidget::setAccentColor(const QColor& color) {
    m_accent = color;
    update();
}

void StylePreviewWidget::setConnectorColor(const QColor& color) {
    m_connector = color;
    update();
}

QSize StylePreviewWidget::sizeHint() const {
    return QSize(248, 320);
}

QSize StylePreviewWidget::minimumSizeHint() const {
    return QSize(150, 200);
}

void StylePreviewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal pad = 16.0;
    const qreal availWidth = qMax<qreal>(120.0, width() - 2 * pad);
    const qreal gap = 26.0;

    const QString rootText = QStringLiteral("中心主题");
    const QString childText = QStringLiteral("子节点：多行文本用于观察行距与对齐效果");
    const QString deeperText = QStringLiteral("更深层节点");

    const qreal rootWidth = availWidth;
    const qreal childWidth = qMax<qreal>(100.0, availWidth - 36.0);
    const qreal deeperWidth = qMax<qreal>(90.0, availWidth - 66.0);

    const qreal rootTextHeight = measureTextHeight(
        rootText, m_styles.root.font(), rootWidth - 2 * kMarginH, m_styles.root.lineSpacing);
    const qreal childTextHeight = measureTextHeight(
        childText, m_styles.firstChild.font(), childWidth - 2 * kMarginH, m_styles.firstChild.lineSpacing);
    const qreal deeperTextHeight = measureTextHeight(
        deeperText, m_styles.deeper.font(), deeperWidth - 2 * kMarginH, m_styles.deeper.lineSpacing);

    const qreal rootHeight = qMax(rootTextHeight + 2 * kMarginV + 4.0, 46.0);
    const qreal childHeight = qMax(childTextHeight + 2 * kMarginV + 4.0, 42.0);
    const qreal deeperHeight = qMax(deeperTextHeight + 2 * kMarginV + 4.0, 36.0);

    const qreal totalHeight = rootHeight + gap + childHeight + gap + deeperHeight;
    qreal top = (height() - totalHeight) / 2.0;
    if (top < pad) top = pad;

    const QRectF rootRect(pad, top, rootWidth, rootHeight);
    const QRectF childRect(pad + 36.0, rootRect.bottom() + gap, childWidth, childHeight);
    const QRectF deeperRect(pad + 66.0, childRect.bottom() + gap, deeperWidth, deeperHeight);

    // 连接线先画，节点随后覆盖在线上，形成真实导图的折线观感。
    QPen connectorPen(m_connector);
    connectorPen.setWidthF(1.6);
    connectorPen.setCapStyle(Qt::RoundCap);
    painter.setPen(connectorPen);
    const qreal spineX = pad + 17.0;
    painter.drawLine(QPointF(spineX, rootRect.bottom()),
                     QPointF(spineX, deeperRect.center().y()));
    painter.drawLine(QPointF(spineX, childRect.center().y()),
                     QPointF(childRect.left(), childRect.center().y()));
    painter.drawLine(QPointF(spineX, deeperRect.center().y()),
                     QPointF(deeperRect.left(), deeperRect.center().y()));

    drawNodeShape(&painter, rootRect, m_styles.root);
    drawNodeShape(&painter, childRect, m_styles.firstChild);
    drawNodeShape(&painter, deeperRect, m_styles.deeper);

    drawNodeText(&painter, rootRect, m_styles.root, rootText);
    drawNodeText(&painter, childRect, m_styles.firstChild, childText);
    drawNodeText(&painter, deeperRect, m_styles.deeper, deeperText);

    const QRectF activeRect = m_activeLevel == 0 ? rootRect
                            : m_activeLevel == 1 ? childRect
                                                 : deeperRect;
    QPen ringPen(m_accent);
    ringPen.setWidthF(2.0);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath ringPath;
    ringPath.addRoundedRect(activeRect.adjusted(-5, -5, 5, 5), kCornerRadius + 5, kCornerRadius + 5);
    painter.drawPath(ringPath);
}

// ============================================================
// 主题缩略图
// ============================================================

QPixmap makeThemeThumbnail(const QSize& size, bool dark, bool system, qreal dpr) {
    const int w = qMax(16, size.width());
    const int h = qMax(12, size.height());
    const qreal ratio = dpr > 0 ? dpr : 1.0;

    QPixmap pixmap(qRound(w * ratio), qRound(h * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame(0.5, 0.5, w - 1.0, h - 1.0);
    QPainterPath clip;
    clip.addRoundedRect(frame, 5, 5);
    painter.save();
    painter.setClipPath(clip);

    const auto paintScheme = [&painter](bool isDark, const QRectF& area) {
        const QColor bg = isDark ? QColor("#1e1e1e") : QColor("#f8fafc");
        const QColor headerBg = isDark ? QColor("#252526") : QColor("#ffffff");
        const QColor bar = isDark ? QColor("#4b5563") : QColor("#e2e8f0");
        const QColor accent = isDark ? QColor("#3b82f6") : QColor("#2563eb");

        painter.fillRect(area, bg);
        const qreal inset = 6.0;
        const QRectF inner = area.adjusted(inset, inset, -inset, -inset);
        painter.fillRect(QRectF(inner.left(), inner.top(), inner.width(), 6.0), headerBg);

        const qreal lineTop = inner.top() + 12.0;
        for (int i = 0; i < 3; ++i) {
            const qreal ratio = i == 2 ? 0.55 : 0.92;
            painter.fillRect(QRectF(inner.left(), lineTop + i * 6.0, inner.width() * ratio, 3.0), bar);
        }
        painter.fillRect(QRectF(inner.left(), inner.bottom() - 2.0, 16.0, 4.0), accent);
    };

    if (system) {
        const QRectF left(frame.left(), frame.top(), frame.width() / 2.0, frame.height());
        const QRectF right(frame.center().x(), frame.top(), frame.width() / 2.0, frame.height());
        paintScheme(false, left);
        paintScheme(true, right);
    } else {
        paintScheme(dark, frame);
    }
    painter.restore();

    QColor border = QColor("#cbd5e1");
    if (!system) border = dark ? QColor("#4b5563") : QColor("#cbd5e1");
    QPen borderPen(border);
    borderPen.setWidthF(1.0);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, 5, 5);
    return pixmap;
}
