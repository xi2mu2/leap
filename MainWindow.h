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
class QGraphicsScene;
class QUndoStack;
class QTimer;
class MindMapView;
class MindMapNode;
class FloatingEditToolbar;
class NewUserGuideController;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
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
