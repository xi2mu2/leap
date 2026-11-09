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
    void setAccentColor(const QColor& color);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateIconPixmap(const QString& color);

    QString m_iconName;
    QString m_text;
    int m_iconFontSize;
    int m_textFontSize;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_textLabel = nullptr;
    QColor m_accentColor;
};
