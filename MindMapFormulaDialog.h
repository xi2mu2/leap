#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QTimer>
#include <QScrollArea>
#include <QListWidget>
#include <QStackedWidget>

class MindMapFormulaDialog : public QDialog {
    Q_OBJECT
public:
    explicit MindMapFormulaDialog(QWidget* parent = nullptr);
    void setFormula(const QString& latexCode);
    QString formula() const;
    static QImage renderLatexToImage(const QString& latexCode, int fontSize = 16);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTextChanged();
    void onConfirm();
    void onCancel();
    void toggleCheatSheet();
    void onCategoryChanged(int index);
    void onFormulaItemClicked();

private:
    void updatePreview();
    static QRect findContentRect(const QImage& image);
    static bool isLatexSafe(const QString& latex);
    void buildCheatSheet();

    QPlainTextEdit* m_inputEdit;
    QLabel* m_previewLabel;
    QPushButton* m_confirmBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_cheatBtn;
    QString m_currentLatex;
    QTimer* m_previewTimer;

    QWidget* m_cheatPanel = nullptr;
    QListWidget* m_categoryList = nullptr;
    QStackedWidget* m_formulaStack = nullptr;
    bool m_cheatVisible = false;
};
