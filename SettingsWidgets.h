#pragma once

// ============================================================
// SettingsWidgets - 设置窗口专用的自绘控件
//
//   ColorField         颜色字段（色块 + 十六进制值 + 重置）
//   SegmentedControl   分段选择器（互斥按钮组）
//   StylePreviewWidget 三级节点实时预览
//   makeThemeThumbnail 主题卡片缩略图
// ============================================================

#include "MindMapStyle.h"

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

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MindMapLevelStyles m_styles;
    int m_activeLevel = 0;
    QColor m_accent = QColor("#2563eb");
    QColor m_connector = QColor("#cbd5e1");
};

// 主题卡片缩略图：跟随系统时用对角线拼出浅 / 深两种面貌。
QPixmap makeThemeThumbnail(const QSize& size, bool dark, bool system, qreal dpr = 1.0);
