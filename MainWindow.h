#pragma once

#include "IconTextButton.h"

#include <QWidget>
#include <QResizeEvent>
#include <QShortcut>
#include <QStringList>

class QLabel;
class QHBoxLayout;
class QFrame;
class QToolButton;
class QScrollArea;
class QPushButton;
class QVBoxLayout;
class QGraphicsScene;
class QUndoStack;
class QTimer;
class QMenu;
class MindMapView;
class MindMapNode;
class FloatingEditToolbar;
class NewUserGuideController;
class PluginManager;
class LeapHostBridge;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    bool openFile(const QString& filePath);
    void openFileAtStartup(const QString& filePath);
    static QStringList recentFiles();
    static void addRecentFile(const QString& filePath);
    Q_INVOKABLE void startNewUserGuide();

    // 【新增】保存确认对话框的返回结果
    enum class SaveChoice { Saved, Discarded, Cancelled };

private slots:
    void onNew();
    void onSave();
    void onSaveAs();
    void onOpen();
    void onInsertImage();
    void onInsertFormula();
    void onExportPng();
    void onExportPdf();
    void onExportSvg();
    void onExportPpt();
    void onExportOutline();
    // 【新增】Mermaid 导入 / 导出
    void onImportMermaid();
    void onExportMermaid();
    // 【新增】Markdown 大纲导入
    void onImportOutline();
    void onHelp();
    void onAbout();
    void onBold();
    void onItalic();
    void onTextColor();
    // 【新增】对齐方式槽函数
    void onAlignLeft();
    void onAlignCenter();
    void onAlignRight();
    void onAlignJustify();
    // 【新增】高亮（背景色）槽函数
    void onHighlight();
    void onHighlightColorRequested();
    // 【新增】下划线槽函数
    void onUnderline();
    // 【新增】节点操作槽函数
    void onAddSibling();
    void onAddChild();
    void onArrange();
    void onSettings();
    // 打开设置并直接停在某一页（插件侧边栏的「管理插件…」用这个）。
    void openSettingsAtPage(int pageIndex);
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QColor m_highlightColor{255, 255, 0};
    void checkEULA();
    SaveChoice askSaveChanges();
    bool performSave(const QString& filePath);
    bool performOpen(const QString& filePath);
    // 把 Mermaid 文本解析成文档并替换当前场景（失败时保留原文档）
    bool performImportMermaid(const QString& filePath);
    // 把 Markdown 大纲解析成文档并替换当前场景（失败时保留原文档）
    bool performImportOutline(const QString& filePath);
    // 导出/导入时询问「是否保留图片」；返回 0 取消、1 仅文字、2 保留图片
    int askImageExportChoice(int imageCount, qint64 imageBytes, bool* remember);
    int askImageImportChoice(int imageCount, bool* remember);
    // 用一份 MindMapIO 根节点 JSON 替换当前文档（失败时回滚到原文档）
    bool replaceDocumentWithJson(const QJsonObject& rootJson, const QString& sourceFileName,
                                 const QString& actionLabel, const QStringList& warnings);
    void performAutoBackup();
    bool shouldPromptForBackup(const QString& filePath) const;
    bool confirmBackupRestore(const QString& filePath, QString* pathToOpen);
    QString backupFilePathFor(const QString& filePath) const;
    bool deleteBackupForFile(const QString& filePath);
    void clearScene();
    MindMapNode* findRoot() const;
    void updateTitle();
    void setupDefaultTree();
    void setupToolbar();

    // 【优化】响应式工具栏
    void setupResponsiveToolbar();
    QHBoxLayout* m_toolbarLayout = nullptr;
    QToolButton* m_moreBtn = nullptr;

    // 【新增】插件系统：扫描 plugins/ 目录并激活插件
    void setupPlugins();
    // 左侧插件侧边栏：骨架在 setupToolbar() 里建好，插件按钮由 LeapHostBridge 填。
    void setupPluginSidebar();
    // 开关侧边栏（工具栏上的「插件」按钮和底部「管理插件…」都走这里）。
    void setPluginSidebarVisible(bool visible);
    // 侧边栏自身的配色（背景、边框、滚动条、分组标题）。
    // 插件分组是在 applyTheme() 之后才创建的，所以加载完插件要再刷一次。
    void applyPluginSidebarTheme(bool dark);
    // 侧边栏本体（固定宽度：可滚动区 + 底部常驻入口），没有插件时整体隐藏。
    QWidget* m_pluginSidebar = nullptr;
    // 把插件提供的导出器挂到「文件 → 导出」子菜单里（加载完插件后调用）。
    void appendPluginExporters();
    // 「文件 → 导出」子菜单（插件导出器追加到这里）。
    QMenu* m_exportSubMenu = nullptr;
    // 侧边栏里的滚动区。
    QScrollArea* m_pluginSidebarScroll = nullptr;
    // 侧边栏里真正容纳插件分组的容器，交给 LeapHostBridge。
    QWidget* m_pluginSidebarBody = nullptr;
    // 侧边栏根布局（分组追加到这里）。
    QVBoxLayout* m_pluginSidebarLayout = nullptr;
    // 工具栏上的「插件」开关按钮。
    IconTextButton* m_pluginToggleBtn = nullptr;
    bool m_pluginSidebarVisible = false;
    // 「文件」下拉菜单，提成成员后插件才能往里加菜单项
    QMenu* m_fileMenu = nullptr;
    PluginManager* m_pluginManager = nullptr;
    LeapHostBridge* m_pluginHost = nullptr;

    // 【优化】缩放指示器
    void updateZoomLabel();
    QLabel* m_zoomLabel = nullptr;

    // 【优化】主题切换
    void applyTheme(bool dark);
    // 【新增】更新所有工具栏按钮样式
    void updateToolbarButtonStyles();
    // 【新增】更新编辑按钮显示状态
    void updateEditButtonsVisibility();
    void updateFloatingEditToolbar();
    // 【新增】设置Windows标题栏深色模式
    void setWindowsTitleBarDark(bool dark);


    QGraphicsScene* m_scene;
    MindMapView* m_view;
    QUndoStack* m_undoStack;
    QTimer* m_autoBackupTimer = nullptr;

    // 【重构】工具栏相关
    QFrame* m_toolbar = nullptr;

    // 【新增】编辑按钮组（动态插入到主工具栏右侧）
    QFrame* m_editBtnSeparator = nullptr;
    IconTextButton* m_boldBtn = nullptr;
    IconTextButton* m_italicBtn = nullptr;
    IconTextButton* m_colorBtn = nullptr;
    IconTextButton* m_doneBtn = nullptr;
    // 【新增】对齐按钮
    IconTextButton* m_alignLeftBtn = nullptr;
    IconTextButton* m_alignCenterBtn = nullptr;
    IconTextButton* m_alignRightBtn = nullptr;
    IconTextButton* m_alignJustifyBtn = nullptr;
    // 【新增】高亮按钮
    IconTextButton* m_highlightBtn = nullptr;
    QToolButton* m_highlightDropBtn = nullptr;
    // 【新增】下划线按钮
    IconTextButton* m_underlineBtn = nullptr;
    // 【新增】节点操作按钮（始终显示，在公式按钮和编辑按钮之间）
    IconTextButton* m_addSiblingBtn = nullptr;
    IconTextButton* m_addChildBtn = nullptr;
    // 【新增】全局快捷键
    QShortcut* m_insertImageShortcut = nullptr;
    QShortcut* m_insertFormulaShortcut = nullptr;
    QShortcut* m_arrangeShortcut = nullptr;
    bool m_editButtonsVisible = false;

    // 【修复】撤销/恢复按钮改为 IconTextButton，支持主题切换
    IconTextButton* m_undoBtn = nullptr;
    IconTextButton* m_redoBtn = nullptr;

    // 【修复】公式按钮指针，用于主题切换时更新图标颜色
    IconTextButton* m_formulaBtn = nullptr;
    FloatingEditToolbar* m_floatingEditToolbar = nullptr;
    NewUserGuideController* m_newUserGuide = nullptr;

protected:
    void resizeEvent(QResizeEvent* event) override;
};
