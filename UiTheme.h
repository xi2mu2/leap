#pragma once

// ============================================================
// UiTheme - 界面设计令牌 + 设置对话框样式表
//
// 说明：
// 1. 这里是“设置”窗口唯一的配色来源，所有颜色都通过 @TOKEN 占位注入 QSS，
//    因此浅色 / 深色两套主题只维护一份样式规则。
// 2. header-only：避免为纯常量引入额外的编译单元。
// 3. 与 UiColors.h 的分工：UiColors 服务于画布/工具栏，UiTheme 服务于
//    设置窗口这类浮层对话框，两者共用同一组蓝灰色调。
// ============================================================

#include <QColor>
#include <QPair>
#include <QString>
#include <QVector>
#include <algorithm>

namespace UiTheme {

struct Tokens {
    QColor bg;                 // 窗口底色
    QColor navBg;              // 左侧导航底色
    QColor navHover;
    QColor navSelected;
    QColor card;               // 分组卡片
    QColor border;             // 卡片 / 分隔线
    QColor control;            // 输入控件底色
    QColor controlBorder;
    QColor controlHover;
    QColor controlDisabled;
    QColor textPrimary;
    QColor textLabel;
    QColor textSecondary;
    QColor textDisabled;
    QColor accent;
    QColor accentHover;
    QColor accentSoft;
    QColor accentBorder;
    QColor onAccent;
    QColor focusRing;
    QColor segmentChecked;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor swatchBorder;
    bool dark = false;
};

inline Tokens light() {
    Tokens t;
    t.bg = QColor("#f8fafc");
    t.navBg = QColor("#f1f5f9");
    t.navHover = QColor("#e2e8f0");
    t.navSelected = QColor("#ffffff");
    t.card = QColor("#ffffff");
    t.border = QColor("#e5e7eb");
    t.control = QColor("#ffffff");
    t.controlBorder = QColor("#cbd5e1");
    t.controlHover = QColor("#93c5fd");
    t.controlDisabled = QColor("#f1f5f9");
    t.textPrimary = QColor("#0f172a");
    t.textLabel = QColor("#334155");
    t.textSecondary = QColor("#64748b");
    t.textDisabled = QColor("#94a3b8");
    t.accent = QColor("#2563eb");
    t.accentHover = QColor("#1d4ed8");
    t.accentSoft = QColor("#eff6ff");
    t.accentBorder = QColor("#bfdbfe");
    t.onAccent = QColor("#ffffff");
    t.focusRing = QColor("#1d4ed8");
    t.segmentChecked = QColor("#ffffff");
    t.scrollHandle = QColor("#cbd5e1");
    t.scrollHandleHover = QColor("#94a3b8");
    t.swatchBorder = QColor(15, 23, 42, 45);
    t.dark = false;
    return t;
}

inline Tokens dark() {
    Tokens t;
    t.bg = QColor("#1e1e1e");
    t.navBg = QColor("#252526");
    t.navHover = QColor("#333333");
    t.navSelected = QColor("#323233");
    t.card = QColor("#2a2a2b");
    t.border = QColor("#3c3c3c");
    t.control = QColor("#323233");
    t.controlBorder = QColor("#4b5563");
    t.controlHover = QColor("#6b7280");
    t.controlDisabled = QColor("#2a2a2b");
    t.textPrimary = QColor("#e5e7eb");
    t.textLabel = QColor("#d4d4d4");
    t.textSecondary = QColor("#9ca3af");
    t.textDisabled = QColor("#6b7280");
    t.accent = QColor("#3b82f6");
    t.accentHover = QColor("#60a5fa");
    t.accentSoft = QColor(59, 130, 246, 41);
    t.accentBorder = QColor("#2f5f9e");
    t.onAccent = QColor("#0b1220");
    t.focusRing = QColor("#93c5fd");
    t.segmentChecked = QColor("#3a3a3c");
    t.scrollHandle = QColor("#4a4a4a");
    t.scrollHandleHover = QColor("#5a5a5a");
    t.swatchBorder = QColor(255, 255, 255, 45);
    t.dark = true;
    return t;
}

// QSS 不接受 #AARRGGBB，半透明颜色必须写成 rgba(r, g, b, a)。
inline QString css(const QColor& c) {
    if (c.alpha() >= 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

inline QString styleSheet(const Tokens& t) {
    static const char* kTemplate = R"QSS(
QDialog { background: @BG; }
QLabel { color: @TEXT_LABEL; background: transparent; }
QLabel#pageTitle { color: @TEXT_PRIMARY; font-size: 20px; font-weight: 600; }
QLabel#pageSubtitle { color: @TEXT_SECONDARY; font-size: 13px; }
QLabel#groupTitle { color: @TEXT_SECONDARY; font-size: 12px; font-weight: 600; }
QLabel#hintLabel { color: @TEXT_SECONDARY; font-size: 12px; }
QLabel#previewTitle { color: @TEXT_SECONDARY; font-size: 12px; font-weight: 600; }
QLabel#previewLevel { color: @ACCENT; font-size: 12px; font-weight: 600; }

QListWidget#categoryList {
    background: transparent;
    border: 0;
    padding: 14px 6px 14px 10px;
    outline: 0;
}
QListWidget#categoryList::item {
    border: 0;
    border-radius: 8px;
    padding: 10px 12px;
    color: @TEXT_LABEL;
}
QListWidget#categoryList::item:hover { background: @NAV_HOVER; }
QListWidget#categoryList::item:selected { background: @NAV_SELECTED; color: @ACCENT; font-weight: 600; }

QFrame#groupCard { background: @CARD; border: 1px solid @BORDER; border-radius: 10px; }
QFrame#previewPanel { background: @BG; border: 1px solid @BORDER; border-radius: 10px; }
QFrame#previewHeader {
    background: @CARD;
    border: 0;
    border-bottom: 1px solid @BORDER;
    border-top-left-radius: 9px;
    border-top-right-radius: 9px;
}
QFrame#footerBar { background: @CARD; border: 0; border-top: 1px solid @BORDER; }

QComboBox, QFontComboBox, QSpinBox, QDoubleSpinBox {
    min-height: 32px;
    padding: 0 8px;
    background: @CONTROL;
    color: @TEXT_PRIMARY;
    border: 1px solid @CONTROL_BORDER;
    border-radius: 6px;
    font-size: 14px;
    selection-background-color: @ACCENT;
    selection-color: @ON_ACCENT;
}
/* 下拉框要给右侧箭头让位，否则文字会压到箭头上。
   微调框不用：Qt 会按 ::up-button/::down-button 的宽度自己算输入区，
   再额外加 padding 会把 " pt" 这类后缀挤没。 */
QComboBox, QFontComboBox { padding-right: 28px; }
QComboBox:hover, QFontComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @CONTROL_HOVER; }
QComboBox:focus, QFontComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: @ACCENT; }
QComboBox:disabled, QFontComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color: @TEXT_DISABLED;
    background: @CONTROL_DISABLED;
    border-color: @BORDER;
}

/* 箭头用自绘的 chevron SVG：Qt 自带的 up-16.png 之类是 Fusion 风格的深色位图，
   在深色主题下几乎看不见，而且位图在高 DPI 下会发糊。 */
QComboBox::drop-down, QFontComboBox::drop-down {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 26px;
    border: 0;
    border-left: 1px solid @CONTROL_BORDER;
    border-top-right-radius: 6px;
    border-bottom-right-radius: 6px;
    background: transparent;
}
QComboBox::down-arrow, QFontComboBox::down-arrow {
    image: @COMBO_ARROW;
    width: 12px;
    height: 8px;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 22px;
    border: 0;
    border-left: 1px solid @CONTROL_BORDER;
    border-bottom: 1px solid @CONTROL_BORDER;
    border-top-right-radius: 6px;
    background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 22px;
    border: 0;
    border-left: 1px solid @CONTROL_BORDER;
    border-bottom-right-radius: 6px;
    background: transparent;
}
QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
    background: @ACCENT_SOFT;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: @SPIN_UP; width: 12px; height: 8px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: @SPIN_DOWN; width: 12px; height: 8px; }
QComboBox QAbstractItemView {
    background: @CARD;
    color: @TEXT_PRIMARY;
    border: 1px solid @BORDER;
    padding: 4px;
    outline: 0;
    selection-background-color: @ACCENT_SOFT;
    selection-color: @ACCENT;
}

QPushButton {
    min-height: 32px;
    padding: 0 16px;
    background: @CONTROL;
    color: @TEXT_PRIMARY;
    border: 1px solid @CONTROL_BORDER;
    border-radius: 6px;
    font-size: 14px;
}
QPushButton:hover { background: @ACCENT_SOFT; border-color: @ACCENT; color: @ACCENT; }
QPushButton:pressed { background: @ACCENT_SOFT; }
QPushButton:focus { border: 2px solid @FOCUS_RING; padding: 0 15px; }
QPushButton:disabled { background: @CONTROL_DISABLED; border-color: @BORDER; color: @TEXT_DISABLED; }
QPushButton#primaryButton {
    min-width: 88px;
    background: @ACCENT;
    border-color: @ACCENT;
    color: @ON_ACCENT;
    font-weight: 600;
}
QPushButton#primaryButton:hover { background: @ACCENT_HOVER; border-color: @ACCENT_HOVER; color: @ON_ACCENT; }
QPushButton#primaryButton:focus { border: 2px solid @ON_ACCENT; padding: 0 15px; }
QPushButton#ghostButton {
    background: transparent;
    border-color: transparent;
    color: @TEXT_SECONDARY;
    padding: 0 10px;
}
QPushButton#ghostButton:hover { background: @NAV_HOVER; color: @TEXT_PRIMARY; }
QPushButton#ghostButton:focus { border: 2px solid @FOCUS_RING; padding: 0 9px; }

QWidget#segmentedControl { background: @NAV_BG; border: 1px solid @BORDER; border-radius: 8px; }
QToolButton#segmentButton {
    border: 1px solid transparent;
    border-radius: 6px;
    background: transparent;
    color: @TEXT_SECONDARY;
    padding: 0 12px;
    min-height: 26px;
    font-size: 13px;
}
QToolButton#segmentButton:hover { color: @TEXT_PRIMARY; }
QToolButton#segmentButton:checked { background: @SEGMENT_CHECKED; color: @ACCENT; font-weight: 600; }
QToolButton#segmentButton:focus { border: 1px solid @ACCENT; }

QToolButton#chipButton {
    background: @CONTROL;
    color: @TEXT_LABEL;
    border: 1px solid @CONTROL_BORDER;
    border-radius: 6px;
    padding: 0 12px;
    min-height: 30px;
    font-size: 13px;
}
QToolButton#chipButton:hover { border-color: @CONTROL_HOVER; }
QToolButton#chipButton:checked { background: @ACCENT_SOFT; border-color: @ACCENT_BORDER; color: @ACCENT; font-weight: 600; }
QToolButton#chipButton:focus { border-color: @ACCENT; }

QToolButton#themeCard {
    background: @CARD;
    color: @TEXT_LABEL;
    border: 2px solid @BORDER;
    border-radius: 10px;
    padding: 8px;
    font-size: 13px;
}
QToolButton#themeCard:hover { border-color: @CONTROL_BORDER; color: @TEXT_PRIMARY; }
QToolButton#themeCard:checked { border-color: @ACCENT; color: @ACCENT; font-weight: 600; }
QToolButton#themeCard:focus { border-color: @ACCENT; }

QWidget#colorField { background: @CONTROL; border: 1px solid @CONTROL_BORDER; border-radius: 6px; }
QWidget#colorField[fieldDisabled="true"] { background: @CONTROL_DISABLED; border-color: @BORDER; }
QWidget#colorField QLabel { color: @TEXT_PRIMARY; }
QWidget#colorField[fieldDisabled="true"] QLabel { color: @TEXT_DISABLED; }
QToolButton#colorReset {
    border: 0;
    background: transparent;
    color: @TEXT_SECONDARY;
    font-size: 12px;
    padding: 0 4px;
    min-height: 22px;
}
QToolButton#colorReset:hover { color: @ACCENT; }
QToolButton#colorReset:disabled { color: @TEXT_DISABLED; }

/* 左栏与内容区都放在滚动区域里，窗口缩小时可以滚动而不是被裁掉。 */
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QScrollArea#navScroll {
    background: @NAV_BG;
    border: 0;
    border-right: 1px solid @BORDER;
}
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 0; }
QScrollBar::handle:vertical { background: @SCROLL_HANDLE; min-height: 28px; border-radius: 5px; }
QScrollBar::handle:vertical:hover { background: @SCROLL_HANDLE_HOVER; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

QToolTip {
    background: @CARD;
    color: @TEXT_PRIMARY;
    border: 1px solid @BORDER;
    padding: 5px 8px;
}
)QSS";

    QVector<QPair<QString, QString>> vars;
    vars << qMakePair(QStringLiteral("BG"), css(t.bg))
         << qMakePair(QStringLiteral("NAV_BG"), css(t.navBg))
         << qMakePair(QStringLiteral("NAV_HOVER"), css(t.navHover))
         << qMakePair(QStringLiteral("NAV_SELECTED"), css(t.navSelected))
         << qMakePair(QStringLiteral("CARD"), css(t.card))
         << qMakePair(QStringLiteral("BORDER"), css(t.border))
         << qMakePair(QStringLiteral("CONTROL"), css(t.control))
         << qMakePair(QStringLiteral("CONTROL_BORDER"), css(t.controlBorder))
         << qMakePair(QStringLiteral("CONTROL_HOVER"), css(t.controlHover))
         << qMakePair(QStringLiteral("CONTROL_DISABLED"), css(t.controlDisabled))
         << qMakePair(QStringLiteral("TEXT_PRIMARY"), css(t.textPrimary))
         << qMakePair(QStringLiteral("TEXT_LABEL"), css(t.textLabel))
         << qMakePair(QStringLiteral("TEXT_SECONDARY"), css(t.textSecondary))
         << qMakePair(QStringLiteral("TEXT_DISABLED"), css(t.textDisabled))
         << qMakePair(QStringLiteral("ACCENT_SOFT"), css(t.accentSoft))
         << qMakePair(QStringLiteral("ACCENT_BORDER"), css(t.accentBorder))
         << qMakePair(QStringLiteral("ACCENT_HOVER"), css(t.accentHover))
         << qMakePair(QStringLiteral("ACCENT"), css(t.accent))
         << qMakePair(QStringLiteral("ON_ACCENT"), css(t.onAccent))
         << qMakePair(QStringLiteral("FOCUS_RING"), css(t.focusRing))
         << qMakePair(QStringLiteral("SEGMENT_CHECKED"), css(t.segmentChecked))
         << qMakePair(QStringLiteral("SCROLL_HANDLE_HOVER"), css(t.scrollHandleHover))
         << qMakePair(QStringLiteral("SCROLL_HANDLE"), css(t.scrollHandle))
         << qMakePair(QStringLiteral("SWATCH_BORDER"), css(t.swatchBorder))
         << qMakePair(QStringLiteral("SPIN_UP"),
                      t.dark ? QStringLiteral("url(:/icons/chevron-up-dark.png)")
                             : QStringLiteral("url(:/icons/chevron-up-light.png)"))
         << qMakePair(QStringLiteral("SPIN_DOWN"),
                      t.dark ? QStringLiteral("url(:/icons/chevron-down-dark.png)")
                             : QStringLiteral("url(:/icons/chevron-down-light.png)"))
         << qMakePair(QStringLiteral("COMBO_ARROW"),
                      t.dark ? QStringLiteral("url(:/icons/chevron-down-dark.png)")
                             : QStringLiteral("url(:/icons/chevron-down-light.png)"));

    // 长键优先替换，否则 @ACCENT 会先把 @ACCENT_SOFT 打断。
    std::sort(vars.begin(), vars.end(), [](const QPair<QString, QString>& a,
                                           const QPair<QString, QString>& b) {
        return a.first.size() > b.first.size();
    });

    QString qss = QString::fromUtf8(kTemplate);
    for (const auto& v : vars) {
        qss.replace(QLatin1Char('@') + v.first, v.second);
    }
    return qss;
}

} // namespace UiTheme
