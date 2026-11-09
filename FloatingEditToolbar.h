#pragma once

#include <QFrame>
#include <QColor>

class QToolButton;

class FloatingEditToolbar : public QFrame {
    Q_OBJECT

public:
    explicit FloatingEditToolbar(QWidget* parent = nullptr);

    void applyTheme(bool dark);
    void setHighlightColor(const QColor& color);

signals:
    void boldRequested();
    void italicRequested();
    void underlineRequested();
    void highlightRequested();
    void highlightColorRequested();
    void alignLeftRequested();
    void alignRightRequested();

private:
    QToolButton* addButton(const QString& iconName, const QString& tooltip);
    void updateButtonIcons(const QString& color);
    QToolButton* m_highlightButton = nullptr;
    QToolButton* m_highlightDropButton = nullptr;
    QColor m_highlightColor{255, 255, 0};
};
