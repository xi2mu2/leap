#pragma once

#include "MindMapStyle.h"

#include <QDialog>
#include <QString>

class MindMapNode;
class ColorField;
class SegmentedControl;
class StylePreviewWidget;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QStackedWidget;
class QToolButton;
class QFontComboBox;
class QGridLayout;
class QWidget;

// 设置窗口：左侧分类导航 + 中间分页表单 + 右侧实时预览。
// 视觉规范集中在 UiTheme.h（浅色 / 深色两套令牌），本类只负责组织控件与状态。
class MindMapSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MindMapSettingsDialog(MindMapNode* root, QWidget* parent = nullptr);

    MindMapLevelStyles appliedStyles() const { return m_appliedStyles; }

public slots:
    void reject() override;

signals:
    void themeChanged(bool dark);
    void stylesApplied(const MindMapLevelStyles& before, const MindMapLevelStyles& after);
    void layoutRefreshRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    // ===== 构建 =====
    void buildUi();
    QWidget* buildFontPage();
    QWidget* buildParagraphPage();
    QWidget* buildNodePage();
    QWidget* buildThemePage();
    QWidget* buildOperationPage();
    QWidget* buildPreviewPanel();
    QWidget* buildFooter();
    // 把页面内容塞进一个无边框滚动区，窗口变矮时可以滚动而不是被压扁。
    QWidget* wrapPage(QWidget* content);
    void addNavItem(const QString& text, int pageIndex);

    // 表单行的统一写法：标签在左，控件次之，提示在右。
    QWidget* createGroupCard(const QString& title, QGridLayout** outGrid);
    void addFormRow(QGridLayout* grid, int row, const QString& label,
                    QWidget* field, const QString& hint = QString());
    QString levelKey() const;
    int levelIndex() const;

    // ===== 状态 =====
    MindMapStyle& currentStyle();
    const MindMapStyle& currentStyle() const;
    void loadCurrentStyle();
    void updateStyleFromControls();
    void refreshPreview();
    void refreshLevelChrome(int pageIndex);
    void restoreLevelDefaults();
    void applyChanges();

    // ===== 主题 =====
    bool effectiveDark() const;
    void setThemeMode(const QString& mode, bool notify);
    void commitThemeMode();
    void rollbackThemeMode();
    void applyThemeToDialog();
    void applyDarkTitleBar();
    void syncThemeCards();

    MindMapNode* m_root = nullptr;
    MindMapLevelStyles m_originalStyles;
    MindMapLevelStyles m_editStyles;
    MindMapLevelStyles m_appliedStyles;

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_pageTitle = nullptr;
    QLabel* m_pageSubtitle = nullptr;
    SegmentedControl* m_levelScope = nullptr;

    // 字体页
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSize = nullptr;
    ColorField* m_textColor = nullptr;

    // 段落页
    SegmentedControl* m_alignment = nullptr;
    QDoubleSpinBox* m_lineSpacing = nullptr;

    // 节点页
    ColorField* m_fillColor = nullptr;
    ColorField* m_borderColor = nullptr;
    QToolButton* m_borderFollow = nullptr;
    QSpinBox* m_borderWidth = nullptr;
    SegmentedControl* m_shape = nullptr;
    QSpinBox* m_textLineWidth = nullptr;

    // 主题页
    QToolButton* m_themeLight = nullptr;
    QToolButton* m_themeDark = nullptr;
    QToolButton* m_themeSystem = nullptr;

    // 操作页
    QDoubleSpinBox* m_viewportMoveDuration = nullptr;

    // 预览与底部
    QWidget* m_previewPanel = nullptr;
    QLabel* m_previewLevelLabel = nullptr;
    StylePreviewWidget* m_preview = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_applyButton = nullptr;

    QString m_themeMode;
    QString m_committedMode;
    bool m_updating = false;
};
