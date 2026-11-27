#pragma once

#include <QToolButton>
#include <QColor>
#include <QLabel>
#include <QVBoxLayout>

// ============================================================
// 带大图标和下方文字的自定义按钮（无背景、更紧凑）
// ============================================================

#include "UiColors.h"

class IconTextButton : public QToolButton {
    Q_OBJECT
public:
    explicit IconTextButton(const QString& iconName, const QString& text,
                   int iconFontSize = 22, int textFontSize = 10,
                   QWidget* parent = nullptr);

    void applyStyleSheet();
    void setIconEmoji(const QString& iconName);
    // 【新增】用外部文件（png / svg）作为图标，供插件按钮使用。
    // SVG 里的 currentColor 仍会按主题替换；PNG 按当前主题决定是否反色。
    void setIconFromFile(const QString& iconFilePath);
    void setAccentColor(const QColor& color);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateIconPixmap(const QString& color);

    QString m_iconName;
    // 非空时优先用这个外部文件，忽略 m_iconName（插件图标走这条路）。
    QString m_iconPath;
    QString m_text;
    int m_iconFontSize;
    int m_textFontSize;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_textLabel = nullptr;
    QColor m_accentColor;
};
