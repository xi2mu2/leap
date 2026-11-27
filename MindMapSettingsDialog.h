#pragma once

#include "MindMapStyle.h"
// 只用到 PluginLoadRecord 这个纯数据结构，不调用 PluginManager 的任何函数，
// 因此设置窗口不依赖插件加载器的实现（也不需要在预览工具里链接它）。
#include "PluginManager.h"

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
    // plugins 是插件加载器已经得到的记录（可能为空）。设置窗口只是「查看 + 预留开关」，
    // 传入拷贝而不是 PluginManager*，这样设置窗口既不依赖加载器实现，也不会在
    // 对话框存活期间引用到可能被销毁的对象。
    explicit MindMapSettingsDialog(MindMapNode* root,
                                   const QList<PluginLoadRecord>& plugins = QList<PluginLoadRecord>(),
                                   const QList<PluginSettingsPage>& pluginPages = QList<PluginSettingsPage>(),
                                   QWidget* parent = nullptr);
    ~MindMapSettingsDialog() override;

    // 打开时直接停在某一页（0=节点 1=段落 2=主题 3=操作 4=插件）。
    // 越界时忽略，保持默认的第一页。
    void setCurrentPage(int pageIndex);
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
    // 一页设置的描述。以前这些信息散落在 buildUi() 的分支和
    // refreshLevelChrome() 的静态数组里，加一页要改三处；现在集中成一张表。
    struct SettingsPageDef {
        QString id;
        QString title;
        QString subtitle;
        bool stylePage = false;  // 是否共享右侧「实时预览」面板
        QWidget* widget = nullptr;  // 已套好 wrapPage 的页面控件
    };

    // ===== 构建 =====
    void buildUi();
    QWidget* buildNodePage();
    QWidget* buildParagraphPage();
    QWidget* buildRainbowCard(QWidget* page);
    QWidget* buildThemePage();
    QWidget* buildOperationPage();
    QWidget* buildPluginsPage();
    // 插件页里一行：名称 / 路径 / 状态，右侧预留开关位。
    QWidget* buildPluginRow(const PluginLoadRecord& record);
    // 重建插件列表内容（本轮只在构造时调用一次，留给将来"重新扫描"用）。
    void refreshPluginRows();
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

    // 操作页 —— 导入 / 导出：三个开关（导出保留图片 / 导入保留图片 / 记住我的选择）。
    // 前两个既是「记住」时直接套用的偏好，也是「每次询问」时弹窗的默认按钮。
    ToggleSwitch* m_importExportExportToggle = nullptr;
    ToggleSwitch* m_importExportImportToggle = nullptr;
    ToggleSwitch* m_importExportRememberToggle = nullptr;

    // 预览与底部
    QWidget* m_previewPanel = nullptr;
    QLabel* m_previewLevelLabel = nullptr;
    StylePreviewWidget* m_preview = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_applyButton = nullptr;

    // 插件页
    QList<PluginLoadRecord> m_plugins;
    QWidget* m_pluginListContainer = nullptr;
    // 开关改动后显示「重启生效」提示；平时隐藏。
    QLabel* m_pluginRestartHint = nullptr;
    // 只为跟着主题换强调色；行重建时一起清空。
    QList<ToggleSwitch*> m_pluginToggles;

    // 插件提供的附加设置页（构造时传入，buildUi() 里逐页索取控件）。
    QList<PluginSettingsPage> m_pluginPages;
    // 导航与页头的唯一数据源，下标即页面下标。
    QList<SettingsPageDef> m_pageDefs;

    QString m_themeMode;
    QString m_committedMode;
    bool m_updating = false;
};
