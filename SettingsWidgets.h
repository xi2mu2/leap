#pragma once

// ============================================================
// SettingsWidgets - 设置窗口专用的自绘控件
//
//   ColorField         颜色字段（色块 + 十六进制值 + 重置）
//   SegmentedControl   分段选择器（互斥按钮组）
//   ToggleSwitch       开关（自绘轨道 + 圆点）
//   StylePreviewWidget 三级节点实时预览
//   makeThemeThumbnail 主题卡片缩略图
// ============================================================

#include "MindMapStyle.h"

#include <QAbstractButton>
#include <QColor>
#include <QPixmap>
#include <QVariant>
#include <QVector>
#include <QWidget>
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QButtonGroup;
class QVariantAnimation;

// 颜色字段：左侧色块，中间十六进制值，右侧“重置”。
// 相比裸色块按钮，它同时给出可读的颜色值，接近白色的填充色也不会看起来像禁用。
class ColorField : public QWidget {
    Q_OBJECT
public:
    explicit ColorField(QWidget* parent = nullptr);

    void setColor(const QColor& color);
    QColor color() const { return m_color; }

    // “重置”按钮的回落目标，同时用于判断是否需要显示该按钮。
    void setDefaultColor(const QColor& color);
    // 设置无障碍名称与色块提示（避免与 QWidget::setAccessibleName 同名）。
    void setFieldLabel(const QString& name);

    void setEnabled(bool enabled);

signals:
    void colorChanged(const QColor& color);

private:
    void chooseColor();
    void resetToDefault();
    void refresh();

    QPushButton* m_swatch = nullptr;
    QLabel* m_hex = nullptr;
    QToolButton* m_reset = nullptr;
    QColor m_color;
    QColor m_defaultColor;
    bool m_fieldEnabled = true;
};

// 分段选择器：与 QComboBox 相比，2~4 个固定选项能一眼看全，也少一次点击。
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(QWidget* parent = nullptr);

    void addItem(const QString& text, const QVariant& data);
    int count() const { return m_buttons.size(); }

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    QVariant currentData() const;
    void setCurrentData(const QVariant& data);

signals:
    void currentChanged(int index);

private:
    QHBoxLayout* m_layout = nullptr;
    QButtonGroup* m_group = nullptr;
    QVector<QToolButton*> m_buttons;
    int m_current = -1;
    bool m_updating = false;
};

// 开关：Qt 没有内置 switch，QCheckBox 的勾选框在设置面板里又太"表单化"。
// 继承 QAbstractButton 后 checkable / clicked / toggled / 键盘空格 / 焦点环全部现成，
// 只需要自己画轨道和圆点。颜色跟随主题 accent，切换带一段短动画。
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    void setAccentColor(const QColor& color);
    // 不带过渡地同步状态：用于打开设置窗口、切换层级这类"程序设置"的场合，
    // 否则每次加载都会看到一次无意义的滑动动画。
    void setCheckedInstant(bool checked);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    QColor m_accent = QColor("#2563eb");
    qreal m_position = 0.0;  // 0 = 关，1 = 开；动画期间取中间值
    QVariantAnimation* m_animation = nullptr;
};

// 三级节点预览：按 MindMapNode::paint() 的规则复刻圆角、边框内缩与行距，
// 让“字号 / 行距 / 对齐 / 边框粗细”这些参数立刻可见。
class StylePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit StylePreviewWidget(QWidget* parent = nullptr);

    void setStyles(const MindMapLevelStyles& styles);
    void setActiveLevel(int level); // 0 根节点，1 一级，2 二级及更深
    void setAccentColor(const QColor& color);
    void setConnectorColor(const QColor& color);
    // 彩虹分支预览：每个分支一行，左格是 1 级饱和色，右格是同色系的 2 级浅色。
    // 小尺寸节点是刻意的——面板只有 280 px 宽，按真实字号排版会溢出。
    // 传整个配置而不是只传六色：用户逐分支微调过的三色也要在预览里体现出来。
    void setRainbowPreview(const MindMapRainbowStyle& rainbow);
    // 彩虹模式下高亮某一行（对应设置页里被选中的那个色块）。
    void setRainbowSelectedBranch(int branch);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintLevelPreview(QPainter* painter);
    void paintRainbowPreview(QPainter* painter);

    MindMapLevelStyles m_styles;
    int m_activeLevel = 0;
    QColor m_accent = QColor("#2563eb");
    QColor m_connector = QColor("#cbd5e1");
    bool m_rainbowEnabled = false;
    // 整份彩虹配置（六色 + 每分支的微调），预览据此复刻画布上的派生结果。
    MindMapRainbowStyle m_rainbow;
    int m_rainbowSelectedBranch = 0;
};

// 主题卡片缩略图：跟随系统时用对角线拼出浅 / 深两种面貌。
QPixmap makeThemeThumbnail(const QSize& size, bool dark, bool system, qreal dpr = 1.0);
