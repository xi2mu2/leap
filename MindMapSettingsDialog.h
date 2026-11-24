#pragma once

#include "MindMapStyle.h"

#include <QDialog>
#include <QString>
#include <QVector>

class MindMapNode;
class ColorField;
class SegmentedControl;
class StylePreviewWidget;
class ToggleSwitch;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QStackedWidget;
class QToolButton;
class QFontComboBox;
class QComboBox;
class QGridLayout;
class QScrollArea;
class QWidget;

// 设置窗口：左侧分类导航 + 中间分页表单 + 右侧实时预览。
// 视觉规范集中在 UiTheme.h（浅色 / 深色两套令牌），本类只负责组织控件与状态。
class MindMapSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MindMapSettingsDialog(MindMapNode* root, QWidget* parent = nullptr);
    ~MindMapSettingsDialog() override;

    MindMapLevelStyles appliedStyles() const { return m_appliedStyles; }

public slots:
    void reject() override;

signals:
    void themeChanged(bool dark);
    void stylesApplied(const MindMapLevelStyles& before, const MindMapLevelStyles& after);
    void layoutRefreshRequested();
    // 小地图显示方式变化，需要立刻生效（不必等对话框关闭）。
    void miniMapModeChanged();

protected:
    void showEvent(QShowEvent* event) override;
    // 把下拉框 / 数字框上的滚轮转给所在页面的滚动区，见 .cpp 里的说明。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // ===== 构建 =====
    void buildUi();
    QWidget* buildNodePage();
    QWidget* buildParagraphPage();
    QWidget* buildRainbowCard(QWidget* page);
    QWidget* buildThemePage();
    QWidget* buildOperationPage();
    QWidget* buildPreviewPanel();
    QWidget* buildFooter();
    // 把页面内容塞进一个无边框滚动区，窗口变矮时可以滚动而不是被压扁。
    QWidget* wrapPage(QWidget* content);
    // 找到控件所在页面的滚动区（下拉框 / 数字框把滚轮吃掉时要用）。
    QScrollArea* pageScrollFor(QWidget* widget) const;
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

    // ===== 彩虹分支 =====
    void setRainbowEnabled(bool enabled);
    void refreshRainbowChrome();
    void refreshRainbowSwatches();
    void refreshRainbowPreset();
    // 彩虹开启时，三色字段编辑的是"当前选中分支"的派生色，这里按选中项回填。
    void refreshRainbowStyleFields();
    // 三个颜色角色在「节点外观 / 文字」卡与彩虹卡之间搬家。
    void moveColorRolesIntoRainbow(bool intoRainbow);

    // ===== 分支三色覆盖 =====
    // 读/写"当前选中分支 + 当前层级"的那一份覆盖。
    MindMapRainbowBranchStyle currentBranchOverride() const;
    void writeBranchOverride(const MindMapRainbowBranchStyle& style);

    // ===== 自定义色卡库 =====
    // 重建下拉列表（内置预设 + 用户存的色卡 + 「自定义」）。
    void rebuildRainbowPresetItems();
    void refreshRainbowPaletteButtons();
    // 保存当前配色：命名 → 查重 → 写入 QSettings。
    bool saveCurrentPalette();
    // 删除当前选中的用户色卡（内置预设无此行按钮）。
    void deleteCurrentPalette();
    // 离开「自定义」之前询问是否保存；返回 false 表示用户取消了本次切换。
    bool confirmLeaveCustomPalette();
    // 「自定义」还有未保存改动时离开，统一走这里。
    bool m_customDirty = false;
    QPushButton* m_rainbowSaveButton = nullptr;
    QPushButton* m_rainbowDeleteButton = nullptr;

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

    // 三个颜色角色共用的容器：彩虹开启时整体搬进彩虹卡片，关闭时搬回各自的卡片。
    // 搬家的是同一个容器而不是两套控件，控件状态（焦点、颜色值）才不会分叉。
    QWidget* m_nodeColorBox = nullptr;   // 填充 + 边框 + 跟随填充
    QWidget* m_textColorBox = nullptr;   // 字体颜色
    QWidget* m_nodeColorSlot = nullptr;  // 「节点外观」卡里的空位
    QWidget* m_textColorSlot = nullptr;  // 「文字」卡里的空位
    QWidget* m_rainbowStyleSlot = nullptr;  // 彩虹卡里的空位

    // 节点页 —— 彩虹分支
    ToggleSwitch* m_rainbowToggle = nullptr;
    QWidget* m_rainbowBody = nullptr;
    QComboBox* m_rainbowPreset = nullptr;
    QVector<QToolButton*> m_rainbowSwatches;
    QLabel* m_rainbowHint = nullptr;
    int m_rainbowSelectedBranch = 0;
    QColor m_accentColor = QColor("#2563eb");

    // 段落页也有一份层级选择器，两份互相同步
    SegmentedControl* m_levelScopeParagraph = nullptr;

    // 主题页
    QToolButton* m_themeLight = nullptr;
    QToolButton* m_themeDark = nullptr;
    QToolButton* m_themeSystem = nullptr;

    // 操作页
    QDoubleSpinBox* m_viewportMoveDuration = nullptr;
    SegmentedControl* m_miniMapMode = nullptr;

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
