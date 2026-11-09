#include "MainWindow.h"
#include "UiColors.h"
#include <QQueue>
#include <functional>
#include <utility>

// UiColors 全局状态定义
bool UiColors::s_isDark = false;
#include "IconTextButton.h"
#include "MindMapView.h"
#include "MindMapNode.h"
#include "MindMapBlock.h"
#include "MindMapIO.h"
#include "PerformanceMetrics.h"
#include "MindMapImageItem.h"
#include "MindMapFormulaDialog.h"
#include "MindMapCommands.h"
#include "MindMapPptExporter.h"
#include "FloatingEditToolbar.h"
#include "MindMapSettingsDialog.h"
#include "NewUserGuide.h"
#include <QGraphicsScene>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QAction>
#include <QKeySequence>
#include <QUndoStack>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileInfo>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QToolButton>
#include <QMenu>
#include <QPainter>
#include <QImage>
#include <QBuffer>
#include <QPdfWriter>
#include <QPageSize>
#include <QSvgGenerator>
#include <QSettings>
#include <QStyleHints>
#include <QGuiApplication>
#include <QTimer>
#include <QDialog>
#include <QPlainTextEdit>
#include <QLabel>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFrame>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QProgressDialog>
#include <QColorDialog>
#include <QUndoCommand>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_20H1 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_22H2
#define DWMWA_USE_IMMERSIVE_DARK_MODE_22H2 38
#endif
#endif

static const QString AUTHOR_NAME = QString("刘羽林");
static const QString AUTHOR_EMAIL = QString("1022645285@qq.com");
static const QString AUTHOR_URL = QString("https://github.com/xi2mu2/leap");
static const QString ORG_NAME = QString("刘羽林");
static const QString APP_VERSION = QString("v1.20");
static const QString RECENT_FILES_KEY = QString("recentFiles");
static const int MAX_RECENT_FILES = 10;
static const int AUTO_BACKUP_INTERVAL_MS = 120000;

class ApplyStylesCommand final : public QUndoCommand {
public:
    ApplyStylesCommand(MindMapNode* root, const MindMapLevelStyles& before,
                       const MindMapLevelStyles& after, std::function<void()> refresh)
        : m_root(root), m_before(before), m_after(after), m_refresh(std::move(refresh)) {
        setText(QStringLiteral("修改节点样式"));
    }
    void undo() override { if (m_root) m_root->applyLevelStyles(m_before); if (m_refresh) m_refresh(); }
    void redo() override { if (m_root) m_root->applyLevelStyles(m_after); if (m_refresh) m_refresh(); }
private:
    MindMapNode* m_root;
    MindMapLevelStyles m_before;
    MindMapLevelStyles m_after;
    std::function<void()> m_refresh;
};


static QString buildEulaText() {
    QStringList lines;
    lines << QString("leap 思维导图 以 GNU General Public License v3.0（GPL-3.0）发布。");
    lines << QString("");
    lines << QString("您享有以下自由：");
    lines << QString("   - 出于任何目的运行本软件；");
    lines << QString("   - 学习并修改本软件的源代码；");
    lines << QString("   - 复制并分发本软件的副本；");
    lines << QString("   - 分发您修改后的版本。");
    lines << QString("");
    lines << QString("分发本软件或其衍生作品时，您必须：");
    lines << QString("   - 同样以 GPL-3.0 许可并开放源代码；");
    lines << QString("   - 保留原有的著作权声明和许可证声明；");
    lines << QString("   - 向接收者提供完整的许可证文本（详见 LICENSE 文件）。");
    lines << QString("");
    lines << QString("本软件按\"原样\"提供，不附带任何担保。完整条款见 LICENSE 文件，");
    lines << QString("或访问 https://www.gnu.org/licenses/gpl-3.0.html");
    lines << QString("");
    lines << QString("如需在闭源商业产品中使用本软件，请联系 %1 获取商业授权。").arg(AUTHOR_EMAIL);
    lines << QString("");
    lines << QString("版权所有 (c) 2026 %1").arg(AUTHOR_NAME);
    lines << QString("");
    lines << QString("点击\"同意\"继续使用。");
    return lines.join(QString("\n"));
}

static QString buildAboutText() {
    QString urlLine;
    if (!AUTHOR_URL.isEmpty()) {
        urlLine = QString("<p><b>项目主页：</b>%1</p>").arg(AUTHOR_URL);
    }
    return QString(
               "<h2>leap 思维导图 v%1</h2>"
               "<p>一个简洁、高效的思维导图工具</p>"
               "<hr>"
               "<p><b>作者：</b>%2</p>"
               "<p><b>联系方式：</b>%3</p>"
               "%4"
               "<hr>"
               "<p>版权所有 &copy; 2026 %2，以 GPL-3.0 发布。</p>"
               "<p>本软件是自由软件，你可以在 GPL-3.0 条款下使用、修改和分发。</p>"
               "<p>完整条款见随附的 LICENSE 文件。</p>"
               "<hr>"
               "<p>本软件按\"原样\"提供，不作任何明示或暗示的担保。</p>"
               ).arg(APP_VERSION, AUTHOR_NAME, AUTHOR_EMAIL, urlLine);
}

// 【新增】创建垂直分隔线（支持主题色）
static QFrame* createVSeparator() {
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(30);
    return line;
}

// 【新增】更新分隔线颜色
static void updateSeparatorStyle(QFrame* line) {
    if (!line) return;
    QString color = UiColors::isDarkMode() ? "#444444" : "#e2e8f0";
    line->setStyleSheet(QString("color: %1;").arg(color));
}

static QRectF subtreeSceneRect(MindMapNode* node) {
    if (!node) return QRectF();

    QRectF result = node->mapRectToScene(node->boundingRect());
    if (node->isExpanded()) {
        for (auto* child : node->childNodes()) {
            result = result.united(subtreeSceneRect(child));
        }
    }
    return result;
}

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), m_undoStack(new QUndoStack(this)) {

    MindMapNode::setUndoStack(m_undoStack);

    m_scene = new QGraphicsScene(this);
    // 使用空间索引降低大图视口绘制和命中查询的成本。
    m_scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);

    m_view = new MindMapView(m_scene, this);
    connect(m_view, &MindMapView::saveRequested, this, &MainWindow::onSave);
    connect(m_view, &MindMapView::openRequested, this, &MainWindow::onOpen);
    connect(m_view, &MindMapView::newRequested, this, &MainWindow::onNew);
    m_newUserGuide = new NewUserGuideController(this, this);
    connect(m_view, &MindMapView::guideNodeSelected, m_newUserGuide, &NewUserGuideController::nodeSelected);
    connect(m_view, &MindMapView::guideChildCreated, m_newUserGuide, &NewUserGuideController::childCreated);
    connect(m_view, &MindMapView::guideSiblingCreated, m_newUserGuide, &NewUserGuideController::siblingCreated);
    connect(m_view, &MindMapView::guideEditingStarted, m_newUserGuide, &NewUserGuideController::editingStarted);
    connect(m_view, &MindMapView::guideEditingExitedByKey, m_newUserGuide, &NewUserGuideController::editingExitedByKey);
    connect(m_view, &MindMapView::guideViewportDragged, m_newUserGuide, &NewUserGuideController::viewportDragged);

    // ========== 主布局：垂直排列，零边距 ==========
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 精致化工具栏 ==========
    m_toolbar = new QFrame(this);
    m_toolbar->setFixedHeight(58);
    m_toolbar->setStyleSheet(
        "QFrame {"
        "  background-color: " + UiColors::BG_TOOLBAR().name() + ";"
        "  border-bottom: 1px solid " + UiColors::BORDER_BOTTOM().name() + ";"
        "}"
        );

    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(14, 4, 14, 4);
    m_toolbarLayout->setSpacing(6);

    // --- 1. 未保存状态指示器（橙色圆点）---
    QLabel* modifiedIndicator = new QLabel();
    modifiedIndicator->setFixedSize(10, 10);
    modifiedIndicator->setStyleSheet(
        "QLabel {"
        "  background-color: " + UiColors::MODIFIED_INDICATOR().name() + ";"
        "  border-radius: 5px;"
        "}"
        );
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [modifiedIndicator](bool clean) {
        modifiedIndicator->setVisible(!clean);
    });
    modifiedIndicator->setVisible(!m_undoStack->isClean());
    m_toolbarLayout->addWidget(modifiedIndicator);

    // --- 2. leap 品牌标识（可点击，显示关于）---
    QToolButton* brandBtn = new QToolButton();
    brandBtn->setText("leap");
    brandBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    brandBtn->setFixedHeight(36);
    brandBtn->setCursor(Qt::PointingHandCursor);
    brandBtn->setStyleSheet(
        "QToolButton {"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 9px;"
        "  color: #1a73e8;"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;"
        "  padding: 5px 10px;"
        "}"
        "QToolButton:hover {"
        "  background-color: #eff6ff;"
        "  border-color: #bfdbfe;"
        "}"
        );
    connect(brandBtn, &QToolButton::clicked, this, &MainWindow::onAbout);
    m_toolbarLayout->addWidget(brandBtn);
    m_toolbarLayout->addSpacing(4);

    // --- 3. 保存按钮 ---
    IconTextButton* saveBtn = new IconTextButton("save", "保存", 22, 10);
    connect(saveBtn, &QToolButton::clicked, this, &MainWindow::onSave);
    m_toolbarLayout->addWidget(saveBtn);

    // --- 4. 文件下拉 ---
    IconTextButton* fileBtn = new IconTextButton("file", "文件", 22, 10);
    fileBtn->setPopupMode(QToolButton::InstantPopup);

    QMenu* fileMenu = new QMenu(fileBtn);
    fileMenu->setStyleSheet(
        "QMenu {"
        "  background-color: #ffffff;"
        "  border: 1px solid #dadce0;"
        "  border-radius: 8px;"
        "  padding: 6px;"
        "}"
        "QMenu::item {"
        "  padding: 8px 24px;"
        "  border-radius: 4px;"
        "  color: #3c4043;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #e8f0fe;"
        "  color: #1a73e8;"
        "}"
        );
    fileMenu->addAction("新建", this, &MainWindow::onNew);
    fileMenu->addAction("打开", this, &MainWindow::onOpen);
    fileMenu->addAction("另存为", this, &MainWindow::onSaveAs);
    fileMenu->addSeparator();
    QMenu* exportSubMenu = fileMenu->addMenu("导出");
    exportSubMenu->setStyleSheet(fileMenu->styleSheet());
    exportSubMenu->addAction("导出 PNG", this, &MainWindow::onExportPng);
    exportSubMenu->addAction("导出 PDF", this, &MainWindow::onExportPdf);
    exportSubMenu->addAction("导出 SVG", this, &MainWindow::onExportSvg);
    exportSubMenu->addAction("导出 PPT", this, &MainWindow::onExportPpt);
    exportSubMenu->addAction(QStringLiteral("导出大纲"), this, &MainWindow::onExportOutline);
    fileBtn->setMenu(fileMenu);

    m_toolbarLayout->addWidget(fileBtn);

    QFrame* sep1 = createVSeparator();
    m_toolbarLayout->addWidget(sep1);

    // --- 5. 撤销/恢复（修复：改为 IconTextButton，支持主题切换和文字标签）---
    m_undoBtn = new IconTextButton("undo", "撤销", 22, 10);
    m_redoBtn = new IconTextButton("redo", "恢复", 22, 10);
    m_undoBtn->setToolTip("撤销 (Ctrl+Z)");
    m_redoBtn->setToolTip("恢复 (Ctrl+Y)");
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);

    connect(m_undoStack, &QUndoStack::canUndoChanged, m_undoBtn, &QToolButton::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_redoBtn, &QToolButton::setEnabled);

    connect(m_undoBtn, &QToolButton::clicked, this, [this]() {
        m_undoStack->undo();
        m_view->setFocus();
    });
    connect(m_redoBtn, &QToolButton::clicked, this, [this]() {
        m_undoStack->redo();
        m_view->setFocus();
    });

    // 统一快捷键与顶部按钮：避免文本编辑控件接管 Ctrl+Z/Ctrl+Y，
    // 导致只能撤销 QTextDocument 的局部修改而不能撤销全局命令栈。
    auto* undoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this);
    auto* redoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this);
    undoShortcut->setContext(Qt::ApplicationShortcut);
    redoShortcut->setContext(Qt::ApplicationShortcut);
    connect(undoShortcut, &QShortcut::activated, this, [this]() {
        if (!m_undoStack || !m_undoStack->canUndo()) return;
        m_undoStack->undo();
        if (m_view) m_view->setFocus();
    });
    connect(redoShortcut, &QShortcut::activated, this, [this]() {
        if (!m_undoStack || !m_undoStack->canRedo()) return;
        m_undoStack->redo();
        if (m_view) m_view->setFocus();
    });

    m_toolbarLayout->addWidget(m_undoBtn);
    m_toolbarLayout->addWidget(m_redoBtn);

    QFrame* sep2 = createVSeparator();
    m_toolbarLayout->addWidget(sep2);

    // --- 6. 插入图片/公式 ---
    QVBoxLayout* insertGroupLayout = new QVBoxLayout();
    insertGroupLayout->setContentsMargins(0, 0, 0, 0);
    insertGroupLayout->setSpacing(0);
    insertGroupLayout->setAlignment(Qt::AlignCenter);

    QHBoxLayout* insertBtnsLayout = new QHBoxLayout();
    insertBtnsLayout->setContentsMargins(0, 0, 0, 0);
    insertBtnsLayout->setSpacing(2);

    IconTextButton* insertImgBtn = new IconTextButton("image", "图片", 22, 10);
    // 【修复】保存公式按钮指针，用于主题切换时更新图标
    m_formulaBtn = new IconTextButton("formula", "公式", 22, 10);
    insertImgBtn->setToolTip("插入图片 (Ctrl+Shift+I)");
    m_formulaBtn->setToolTip("插入公式 (Ctrl+Shift+F)");

    QPushButton* hiddenImgBtn = new QPushButton(this);
    hiddenImgBtn->hide();
    m_view->setInsertImageButton(hiddenImgBtn);

    connect(insertImgBtn, &QToolButton::clicked, this, &MainWindow::onInsertImage);
    connect(m_formulaBtn, &QToolButton::clicked, this, &MainWindow::onInsertFormula);

    insertBtnsLayout->addWidget(insertImgBtn);
    insertBtnsLayout->addWidget(m_formulaBtn);
    insertGroupLayout->addLayout(insertBtnsLayout);

    QWidget* insertGroupWidget = new QWidget();
    insertGroupWidget->setLayout(insertGroupLayout);
    m_toolbarLayout->addWidget(insertGroupWidget);

    QFrame* sepNode = createVSeparator();
    m_toolbarLayout->addWidget(sepNode);

    // --- 【新增】同级节点 / 子节点按钮（始终显示）---
    m_addSiblingBtn = new IconTextButton("sibling", "同级", 22, 10);
    m_addChildBtn = new IconTextButton("child", "子节点", 22, 10);
    m_addSiblingBtn->setToolTip("添加同级节点 (Enter)");
    m_addChildBtn->setToolTip("添加子节点 (Tab)");
    connect(m_addSiblingBtn, &QToolButton::clicked, this, &MainWindow::onAddSibling);
    connect(m_addChildBtn, &QToolButton::clicked, this, &MainWindow::onAddChild);
    m_toolbarLayout->addWidget(m_addSiblingBtn);
    m_toolbarLayout->addWidget(m_addChildBtn);

    // --- 【新增】整理按钮 ---
    // 整理功能保留，但入口仅使用 Ctrl+Shift+A。

    m_toolbarLayout->addStretch();

    // --- 7. 帮助 ---
    IconTextButton* helpBtn = new IconTextButton("help", "帮助", 22, 10);
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::onHelp);

    // --- 8. 深色模式切换 ---
    // 主题切换移入设置中心。

    // ========== 【新增】编辑按钮组（初始隐藏，插入到stretch之前）==========
    // 移除之前添加的helpBtn和themeBtn，稍后重新添加

    // 创建编辑按钮
    m_editBtnSeparator = createVSeparator();
    m_editBtnSeparator->setVisible(false);

    m_boldBtn = new IconTextButton("bold", "加粗", 18, 9);
    m_italicBtn = new IconTextButton("italic", "斜体", 18, 9);
    m_underlineBtn = new IconTextButton("underline", "下划线", 18, 9);
    m_colorBtn = new IconTextButton("color", "颜色", 18, 9);
    m_highlightBtn = new IconTextButton("highlight", "高亮", 18, 9);
    m_highlightDropBtn = new QToolButton(this);
    auto* highlightDropBtn = m_highlightDropBtn;
    highlightDropBtn->setArrowType(Qt::DownArrow);
    highlightDropBtn->setFixedSize(16, 28);
    highlightDropBtn->setToolTip("选择高亮颜色");
    highlightDropBtn->setVisible(false);
    m_alignLeftBtn = new IconTextButton("align-left", "左对齐", 18, 9);
    m_alignCenterBtn = new IconTextButton("align-center", "居中", 18, 9);
    m_alignRightBtn = new IconTextButton("align-right", "右对齐", 18, 9);
    m_alignJustifyBtn = new IconTextButton("align-justify", "两端对齐", 18, 9);
    m_doneBtn = new IconTextButton("check", "完成", 18, 9);

    m_boldBtn->setVisible(false);
    m_italicBtn->setVisible(false);
    m_underlineBtn->setVisible(false);
    m_colorBtn->setVisible(false);
    m_highlightBtn->setVisible(false);
    m_alignLeftBtn->setVisible(false);
    m_alignCenterBtn->setVisible(false);
    m_alignRightBtn->setVisible(false);
    m_alignJustifyBtn->setVisible(false);
    m_doneBtn->setVisible(false);

    m_boldBtn->setToolTip("加粗 (Ctrl+B)");
    m_italicBtn->setToolTip("斜体 (Ctrl+I)");
    m_underlineBtn->setToolTip("下划线 (Ctrl+U)");
    m_colorBtn->setToolTip("文字颜色");
    m_highlightBtn->setToolTip("文字高亮");
    m_alignLeftBtn->setToolTip("左对齐 (Ctrl+L)");
    m_alignCenterBtn->setToolTip("居中对齐 (Ctrl+E)");
    m_alignRightBtn->setToolTip("右对齐 (Ctrl+R)");
    m_alignJustifyBtn->setToolTip("两端对齐 (Ctrl+J)");
    m_doneBtn->setToolTip("完成编辑 (Enter)");

    connect(m_boldBtn, &QToolButton::clicked, this, &MainWindow::onBold);
    connect(m_italicBtn, &QToolButton::clicked, this, &MainWindow::onItalic);
    connect(m_underlineBtn, &QToolButton::clicked, this, &MainWindow::onUnderline);
    connect(m_colorBtn, &QToolButton::clicked, this, &MainWindow::onTextColor);
    connect(m_highlightBtn, &QToolButton::clicked, this, &MainWindow::onHighlight);
    connect(highlightDropBtn, &QToolButton::clicked, this, &MainWindow::onHighlightColorRequested);
    m_highlightBtn->setAccentColor(m_highlightColor);
    connect(m_alignLeftBtn, &QToolButton::clicked, this, &MainWindow::onAlignLeft);
    connect(m_alignCenterBtn, &QToolButton::clicked, this, &MainWindow::onAlignCenter);
    connect(m_alignRightBtn, &QToolButton::clicked, this, &MainWindow::onAlignRight);
    connect(m_alignJustifyBtn, &QToolButton::clicked, this, &MainWindow::onAlignJustify);
    connect(m_doneBtn, &QToolButton::clicked, this, [this]() {
        if (auto* editingNode = MindMapNode::findEditingNode(m_scene)) {
            editingNode->finishEdit();
        }
    });

    // 添加编辑按钮到布局
    m_toolbarLayout->addWidget(m_editBtnSeparator);
    m_toolbarLayout->addWidget(m_boldBtn);
    m_toolbarLayout->addWidget(m_italicBtn);
    m_toolbarLayout->addWidget(m_underlineBtn);
    m_toolbarLayout->addWidget(m_highlightBtn);
    m_toolbarLayout->addWidget(highlightDropBtn);
    m_toolbarLayout->addWidget(m_doneBtn);

    // 重新添加help和theme按钮
    m_toolbarLayout->addWidget(helpBtn);
    auto* settingsBtn = new IconTextButton("settings", "设置", 22, 10);
    settingsBtn->setToolTip("打开设置");
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::onSettings);
    m_toolbarLayout->addWidget(settingsBtn);

    // ========== 组装主布局 ==========
    mainLayout->addWidget(m_toolbar);
    mainLayout->addWidget(m_view, 1);
    setLayout(mainLayout);

    m_floatingEditToolbar = new FloatingEditToolbar(this);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::boldRequested, this, &MainWindow::onBold);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::italicRequested, this, &MainWindow::onItalic);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::underlineRequested, this, &MainWindow::onUnderline);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::highlightRequested, this, &MainWindow::onHighlight);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::highlightColorRequested, this, &MainWindow::onHighlightColorRequested);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::alignLeftRequested, this, &MainWindow::onAlignLeft);
    connect(m_floatingEditToolbar, &FloatingEditToolbar::alignRightRequested, this, &MainWindow::onAlignRight);

    setAcceptDrops(true);

    checkEULA();

    resize(1400, 900);
    onNew();
    m_view->updateInsertImageButtonState();
    m_view->setFocus();

    // 【优化】初始化响应式工具栏
    setupResponsiveToolbar();

    // 【修复】统一初始化所有 IconTextButton 的样式
    updateToolbarButtonStyles();
    // 【新增】注册全局快捷键（即使焦点在 QGraphicsTextItem 上也能触发）
    m_insertImageShortcut = new QShortcut(QKeySequence("Ctrl+Shift+I"), this);
    m_insertFormulaShortcut = new QShortcut(QKeySequence("Ctrl+Shift+F"), this);
    connect(m_insertImageShortcut, &QShortcut::activated, this, &MainWindow::onInsertImage);
    connect(m_insertFormulaShortcut, &QShortcut::activated, this, &MainWindow::onInsertFormula);

    // 【新增】整理布局快捷键
    m_arrangeShortcut = new QShortcut(QKeySequence("Ctrl+Shift+A"), this);
    connect(m_arrangeShortcut, &QShortcut::activated, this, &MainWindow::onArrange);

    // 【优化】创建缩放指示器（右下角浮动）
    m_zoomLabel = new QLabel("80%", this);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(255, 255, 255, 0.9);"
        "  border: 1px solid #dadce0;"
        "  border-radius: 12px;"
        "  color: #5f6368;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "  padding: 4px 10px;"
        "}"
    );
    m_zoomLabel->setFixedSize(56, 26);
    m_zoomLabel->setCursor(Qt::PointingHandCursor);
    m_zoomLabel->setToolTip("点击重置缩放");
    m_zoomLabel->show();
    updateZoomLabel();
    // 主题以 themeMode 为准（light / dark / system）；没有该键时回退旧的 darkTheme 布尔。
    QSettings startupSettings;
    const QString savedThemeMode = startupSettings.value(QStringLiteral("themeMode")).toString();
    bool savedDarkTheme = startupSettings.value(QStringLiteral("darkTheme"), false).toBool();
    if (savedThemeMode == QLatin1String("system")) {
        savedDarkTheme = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    } else if (savedThemeMode == QLatin1String("light") || savedThemeMode == QLatin1String("dark")) {
        savedDarkTheme = savedThemeMode == QLatin1String("dark");
    }
    UiColors::setDarkMode(savedDarkTheme);
    applyTheme(savedDarkTheme);

    // “跟随系统”时，系统明暗在运行中变化也要生效。
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            QSettings settings;
            if (settings.value(QStringLiteral("themeMode")).toString() != QLatin1String("system"))
                return;
            const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
            UiColors::setDarkMode(dark);
            applyTheme(dark);
        });
    }

    connect(m_view, &MindMapView::zoomChanged, this, &MainWindow::updateZoomLabel);

    // 【优化】定时检测编辑状态，切换编辑按钮显示
    m_autoBackupTimer = new QTimer(this);
    m_autoBackupTimer->setInterval(AUTO_BACKUP_INTERVAL_MS);
    connect(m_autoBackupTimer, &QTimer::timeout, this, &MainWindow::performAutoBackup);
    m_autoBackupTimer->start();

    QTimer* editCheckTimer = new QTimer(this);
    editCheckTimer->setInterval(100);
    connect(editCheckTimer, &QTimer::timeout, this, &MainWindow::updateEditButtonsVisibility);
    editCheckTimer->start();
}

// 【新增】更新编辑按钮显示状态
void MainWindow::updateEditButtonsVisibility() {
    bool currentlyEditing = (MindMapNode::findEditingNode(m_scene) != nullptr);
    if (currentlyEditing != m_editButtonsVisible) {
        m_editButtonsVisible = currentlyEditing;
        m_editBtnSeparator->setVisible(currentlyEditing);
        m_boldBtn->setVisible(currentlyEditing);
        m_italicBtn->setVisible(currentlyEditing);
        m_underlineBtn->setVisible(currentlyEditing);
        m_colorBtn->setVisible(false);
    m_highlightBtn->setVisible(currentlyEditing);
        m_highlightDropBtn->setVisible(currentlyEditing);
        m_alignLeftBtn->setVisible(false);
        m_alignCenterBtn->setVisible(false);
        m_alignRightBtn->setVisible(false);
        m_alignJustifyBtn->setVisible(false);
        m_doneBtn->setVisible(currentlyEditing);
    }
    updateFloatingEditToolbar();
}

void MainWindow::updateFloatingEditToolbar() {
    if (!m_floatingEditToolbar || !m_view || !m_scene || !m_toolbar) return;

    MindMapNode* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->isEditing()) {
        m_floatingEditToolbar->hide();
        return;
    }

    m_floatingEditToolbar->adjustSize();
    QRectF sceneRect = editingNode->mapRectToScene(editingNode->boundingRect());
    QRect viewportRect = m_view->mapFromScene(sceneRect).boundingRect();
    QPoint topLeft = m_view->viewport()->mapTo(this, viewportRect.topLeft());
    QPoint bottomRight = m_view->viewport()->mapTo(this, viewportRect.bottomRight());
    QRect nodeRect(topLeft, bottomRight);

    QSize toolbarSize = m_floatingEditToolbar->sizeHint();
    if (!toolbarSize.isValid() || toolbarSize.isEmpty()) {
        toolbarSize = m_floatingEditToolbar->size();
    }

    const int margin = 10;
    int x = nodeRect.center().x() - toolbarSize.width() / 2;
    int y = nodeRect.top() - toolbarSize.height() - margin;
    const int minY = m_toolbar->geometry().bottom() + margin;
    if (y < minY) {
        y = nodeRect.bottom() + margin;
    }

    x = qBound(margin, x, qMax(margin, width() - toolbarSize.width() - margin));
    y = qBound(minY, y, qMax(minY, height() - toolbarSize.height() - margin));

    m_floatingEditToolbar->move(x, y);
    m_floatingEditToolbar->show();
    m_floatingEditToolbar->raise();
}

// 【新增】加粗
void MainWindow::onBold() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    QTextCursor cursor = textItem->textCursor();
    if (!cursor.hasSelection()) {
        // 如果没有选中文本，设置后续输入为粗体
        QTextCharFormat fmt;
        fmt.setFontWeight(cursor.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        textItem->mergeCurrentCharFormat(fmt);
    } else {
        QTextCharFormat fmt;
        fmt.setFontWeight(cursor.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        cursor.mergeCharFormat(fmt);
        textItem->setTextCursor(cursor);
    }

    // 【修复】将格式变更推入 undo 栈
    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }

    textItem->setFocus();
}

// 【新增】斜体
void MainWindow::onItalic() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    QTextCursor cursor = textItem->textCursor();
    if (!cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setFontItalic(!cursor.charFormat().fontItalic());
        textItem->mergeCurrentCharFormat(fmt);
    } else {
        QTextCharFormat fmt;
        fmt.setFontItalic(!cursor.charFormat().fontItalic());
        cursor.mergeCharFormat(fmt);
        textItem->setTextCursor(cursor);
    }

    // 【修复】将格式变更推入 undo 栈
    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }

    textItem->setFocus();
}

// 【新增】下划线
void MainWindow::onUnderline() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    QTextCursor cursor = textItem->textCursor();
    if (!cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setFontUnderline(!cursor.charFormat().fontUnderline());
        textItem->mergeCurrentCharFormat(fmt);
    } else {
        QTextCharFormat fmt;
        fmt.setFontUnderline(!cursor.charFormat().fontUnderline());
        cursor.mergeCharFormat(fmt);
        textItem->setTextCursor(cursor);
    }

    // 将格式变更推入 undo 栈
    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }

    textItem->setFocus();
}

// 【新增】文字高亮（背景色）
void MainWindow::onHighlight() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    /* Color selection is handled by the toolbar's drop-down button.
    QColor currentBg = textItem->textCursor().charFormat().background().color();
    if (!currentBg.isValid() || currentBg == Qt::transparent) {
        currentBg = QColor(255, 255, 0); // 默认黄色高亮
    }

    QColor color = QColorDialog::getColor(currentBg, this, "选择高亮颜色");
    if (!color.isValid()) return;

    */
    const QColor color = m_highlightColor;
    QTextCursor cursor = textItem->textCursor();
    bool removeHighlight = false;
    if (cursor.hasSelection()) {
        QTextDocument* document = textItem->document();
        bool hasTextCharacter = false;
        bool allCharactersMatch = true;
        const int selectionStart = cursor.selectionStart();
        const int selectionEnd = cursor.selectionEnd();
        for (QTextBlock block = document->begin(); block.isValid() && allCharactersMatch; block = block.next()) {
            for (QTextBlock::iterator iterator = block.begin(); iterator != block.end(); ++iterator) {
                const QTextFragment fragment = iterator.fragment();
                if (!fragment.isValid()) continue;

                const int fragmentStart = fragment.position();
                const int fragmentEnd = fragmentStart + fragment.length();
                if (fragmentEnd <= selectionStart || fragmentStart >= selectionEnd) continue;

                hasTextCharacter = true;
                const QBrush background = fragment.charFormat().background();
                if (background.style() == Qt::NoBrush || background.color() != color) {
                    allCharactersMatch = false;
                    break;
                }
            }
        }
        removeHighlight = hasTextCharacter && allCharactersMatch;
    }
    if (!cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setBackground(removeHighlight ? Qt::NoBrush : QBrush(color));
        textItem->mergeCurrentCharFormat(fmt);
    } else {
        QTextCharFormat fmt;
        fmt.setBackground(removeHighlight ? Qt::NoBrush : QBrush(color));
        cursor.mergeCharFormat(fmt);
        textItem->setTextCursor(cursor);
    }

    // 将格式变更推入 undo 栈
    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }

    textItem->setFocus();
}

// 【新增】添加同级节点
void MainWindow::onHighlightColorRequested() {
    const QColor color = QColorDialog::getColor(m_highlightColor, this, QStringLiteral("选择高亮颜色"));
    if (!color.isValid()) return;
    m_highlightColor = color;
    if (m_highlightBtn) m_highlightBtn->setAccentColor(color);
    if (m_floatingEditToolbar) m_floatingEditToolbar->setHighlightColor(color);
}

void MainWindow::onAddSibling() {
    if (m_view && m_view->selectedNodeCount() > 1) return;
    // 查找当前选中的节点
    MindMapNode* selectedNode = nullptr;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected()) {
                selectedNode = node;
                break;
            }
        }
    }
    if (!selectedNode) return;

    // 如果正在编辑，先结束编辑
    if (selectedNode->isEditing()) {
        selectedNode->finishEdit();
    }

    // 模拟 Enter 键行为
    if (m_undoStack) {
        m_undoStack->push(new AddSiblingCommand(selectedNode, m_scene));
    } else {
        MindMapNode* newNode = nullptr;
        if (selectedNode->parentNode()) {
            auto siblings = selectedNode->parentNode()->childNodes();
            int idx = siblings.indexOf(selectedNode);
            newNode = new MindMapNode("", nullptr);
            if (idx >= 0) {
                selectedNode->parentNode()->insertChild(idx + 1, newNode);
            } else {
                selectedNode->parentNode()->addChild(newNode);
            }
        } else {
            newNode = new MindMapNode("", selectedNode);
        }
        if (newNode) {
            m_scene->addItem(newNode);
            MindMapNode* root = selectedNode->getRoot();
            root->clearEdges();
            root->layoutTree(0, 0);
            root->adjustLayout();
            root->updateAllEdges();
            selectedNode->setNodeSelected(false);
            newNode->setNodeSelected(true);
        }
    }
}

// 【新增】添加子节点
void MainWindow::onAddChild() {
    if (m_view && m_view->selectedNodeCount() > 1) return;
    // 查找当前选中的节点
    MindMapNode* selectedNode = nullptr;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected()) {
                selectedNode = node;
                break;
            }
        }
    }
    if (!selectedNode) return;

    // 如果正在编辑，先结束编辑
    if (selectedNode->isEditing()) {
        selectedNode->finishEdit();
    }

    // 模拟 Tab 键行为
    if (m_undoStack) {
        m_undoStack->push(new AddChildCommand(selectedNode, m_scene));
    } else {
        auto* newNode = new MindMapNode("", selectedNode);
        m_scene->addItem(newNode);
        MindMapNode* root = selectedNode->getRoot();
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
        selectedNode->setNodeSelected(false);
        newNode->setNodeSelected(true);
    }
}

// 【新增】整理布局（全局重排）
void MainWindow::onArrange() {
    MindMapNode* root = findRoot();
    if (!root) return;

    // 【调试】打印当前根节点位置
    qDebug() << "[onArrange] Before: root pos =" << root->pos();

    root->clearEdges();
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();

    // 【调试】打印重排后根节点位置
    qDebug() << "[onArrange] After: root pos =" << root->pos();

    m_view->centerOn(root);
    m_view->ensureSceneRectContains();
    if (m_scene) m_scene->update();
}

void MainWindow::onSettings() {
    MindMapNode* root = findRoot();
    if (!root) return;
    MindMapSettingsDialog dialog(root, this);
    connect(&dialog, &MindMapSettingsDialog::themeChanged, this, [this](bool dark) {
        UiColors::setDarkMode(dark);
        applyTheme(dark);
    });
    auto refreshAfterStyleChange = [this, root]() {
        const QRectF previousSceneRect = m_scene ? m_scene->sceneRect() : QRectF();
        const bool canRestoreView = m_view && m_view->viewport();
        const QPoint viewportCenter = canRestoreView
            ? m_view->viewport()->rect().center() : QPoint();
        const QPointF oldRootCenter = root->mapToScene(root->boundingRect().center());
        const QPointF oldSceneCenter = canRestoreView
            ? m_view->mapToScene(viewportCenter) : QPointF();
        // 保存根节点相对视口中心的场景偏移，而不是保存绝对场景中心。
        // 节点尺寸变化会触发布局，绝对场景坐标不再对应原来的视觉内容。
        const QPointF rootOffset = oldRootCenter - oldSceneCenter;
        root->clearEdges();
        root->layoutTree(root->pos().x(), root->pos().y());
        root->adjustLayout();
        // adjustLayout 可能再次移动整棵子树，必须在最终位置上重建连线。
        root->clearEdges();
        root->updateAllEdges();
        if (m_scene && previousSceneRect.isValid()) {
            QRectF contentRect = subtreeSceneRect(root);
            if (contentRect.isValid()) {
                contentRect.adjust(-200, -200, 200, 200);
                QRectF nextSceneRect = previousSceneRect.united(contentRect);
                // 应用样式不能让已有画布缩小，避免视口跳回初始范围。
                nextSceneRect.setWidth(qMax(nextSceneRect.width(), previousSceneRect.width()));
                nextSceneRect.setHeight(qMax(nextSceneRect.height(), previousSceneRect.height()));
                m_scene->setSceneRect(nextSceneRect);
            }
        }
        if (canRestoreView) {
            const QPointF newRootCenter = root->mapToScene(root->boundingRect().center());
            m_view->centerOn(newRootCenter - rootOffset);
        }
        if (m_scene) m_scene->update();
    };
    connect(&dialog, &MindMapSettingsDialog::stylesApplied, this,
            [this, root, refreshAfterStyleChange](const MindMapLevelStyles& before,
                                                   const MindMapLevelStyles& after) {
        if (m_undoStack) {
            m_undoStack->push(new ApplyStylesCommand(root, before, after,
                                                     refreshAfterStyleChange));
        } else {
            refreshAfterStyleChange();
        }
    });
    connect(&dialog, &MindMapSettingsDialog::layoutRefreshRequested, this,
            refreshAfterStyleChange);
    dialog.exec();
}

// 【新增】对齐方式辅助函数
static void applyAlignment(MindMapTextItem* textItem, Qt::Alignment alignment) {
    QTextCursor cursor = textItem->textCursor();
    if (cursor.hasSelection()) {
        // 对选中的所有段落应用对齐
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.setPosition(start);
        do {
            QTextBlockFormat blockFmt = cursor.blockFormat();
            blockFmt.setAlignment(alignment);
            cursor.setBlockFormat(blockFmt);
            if (!cursor.movePosition(QTextCursor::NextBlock)) break;
        } while (cursor.position() < end && cursor.position() > start);
    } else {
        // 对当前段落应用对齐
        QTextBlockFormat blockFmt = cursor.blockFormat();
        blockFmt.setAlignment(alignment);
        cursor.setBlockFormat(blockFmt);
    }
    textItem->setTextCursor(cursor);
    // 【关键】触发重绘以确保对齐效果立即显示
    textItem->update();
}

// 【新增】左对齐
void MainWindow::onAlignLeft() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    applyAlignment(textItem, Qt::AlignLeft);

    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }
    textItem->setFocus();
}

// 【新增】居中对齐
void MainWindow::onAlignCenter() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    applyAlignment(textItem, Qt::AlignCenter);

    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }
    textItem->setFocus();
}

// 【新增】右对齐
void MainWindow::onAlignRight() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    applyAlignment(textItem, Qt::AlignRight);

    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }
    textItem->setFocus();
}

// 【新增】两端对齐
void MainWindow::onAlignJustify() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    applyAlignment(textItem, Qt::AlignJustify);

    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }
    textItem->setFocus();
}

// 【新增】文字颜色
void MainWindow::onTextColor() {
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (!editingNode || !editingNode->editingTextBlock()) return;

    auto* textItem = editingNode->editingTextBlock()->textItem();
    int blockIdx = editingNode->blockIndex(editingNode->editingTextBlock());
    QString oldHtml = textItem->toHtml();
    int cursorPos = textItem->textCursor().position();

    QColor currentColor = textItem->textCursor().charFormat().foreground().color();
    if (!currentColor.isValid()) currentColor = Qt::black;

    QColor color = QColorDialog::getColor(currentColor, this, "选择文字颜色");
    if (!color.isValid()) return;

    QTextCursor cursor = textItem->textCursor();
    if (!cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        textItem->mergeCurrentCharFormat(fmt);
    } else {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        cursor.mergeCharFormat(fmt);
        textItem->setTextCursor(cursor);
    }

    // 【修复】将格式变更推入 undo 栈
    QString newHtml = textItem->toHtml();
    if (oldHtml != newHtml && m_undoStack && blockIdx >= 0) {
        m_undoStack->push(new FormatTextCommand(editingNode, blockIdx, oldHtml, newHtml, cursorPos, m_scene));
    }

    textItem->setFocus();
}

// 【新增】更新工具栏按钮样式（主题切换时调用）
void MainWindow::updateToolbarButtonStyles() {
    if (!m_toolbarLayout) {
        return;
    }

    // 【修复】递归查找所有 IconTextButton（包括嵌套在容器中的，如图片/公式按钮）
    QQueue<QObject*> queue;
    for (int i = 0; i < m_toolbarLayout->count(); ++i) {
        if (QLayoutItem* item = m_toolbarLayout->itemAt(i)) {
            if (QWidget* widget = item->widget()) {
                queue.enqueue(widget);
            }
        }
    }

    while (!queue.isEmpty()) {
        QObject* obj = queue.dequeue();
        if (auto* btn = qobject_cast<IconTextButton*>(obj)) {
            btn->applyStyleSheet();
        }
        // 递归查找子对象
        for (QObject* child : obj->children()) {
            queue.enqueue(child);
        }
    }

}

void MainWindow::checkEULA() {
    QSettings settings(ORG_NAME, QString("leap"));
    if (settings.value(QString("eulaAccepted"), false).toBool()) return;

    QDialog eulaDlg(this);
    eulaDlg.setWindowTitle(QString("许可证"));
    eulaDlg.setMinimumSize(620, 480);

    QVBoxLayout* dlgLayout = new QVBoxLayout(&eulaDlg);

    QLabel* titleLbl = new QLabel(QString("欢迎使用 leap 思维导图！"), &eulaDlg);
    QFont tf = titleLbl->font();
    tf.setBold(true);
    tf.setPointSize(12);
    titleLbl->setFont(tf);
    dlgLayout->addWidget(titleLbl);

    QPlainTextEdit* textEdit = new QPlainTextEdit(&eulaDlg);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(buildEulaText());
    dlgLayout->addWidget(textEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* agreeBtn = new QPushButton(QString("同意"), &eulaDlg);
    QPushButton* rejectBtn = new QPushButton(QString("拒绝"), &eulaDlg);
    btnLayout->addWidget(agreeBtn);
    btnLayout->addWidget(rejectBtn);
    dlgLayout->addLayout(btnLayout);

    connect(agreeBtn, &QPushButton::clicked, &eulaDlg, &QDialog::accept);
    connect(rejectBtn, &QPushButton::clicked, &eulaDlg, &QDialog::reject);

    if (eulaDlg.exec() != QDialog::Accepted) {
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
        return;
    }
    settings.setValue(QString("eulaAccepted"), true);
}

void MainWindow::onNew() {
    auto choice = askSaveChanges();
    if (choice == MainWindow::SaveChoice::Cancelled) return;

    if (choice == MainWindow::SaveChoice::Discarded) {
        deleteBackupForFile(m_view->currentFilePath());
    }

    m_undoStack->clear();
    clearScene();
    setupDefaultTree();
    m_view->setCurrentFilePath(QString(""));
    updateTitle();
}

void MainWindow::onSave() {
    QString filePath = m_view->currentFilePath();
    if (filePath.isEmpty()) {
        onSaveAs();
        return;
    }
    if (performSave(filePath)) {
        m_undoStack->setClean();
        updateTitle();
    }
}

void MainWindow::onSaveAs() {
    const QString previousPath = m_view->currentFilePath();
    QString filePath = QFileDialog::getSaveFileName(this, QString("保存思维导图"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    MindMapIO::fileFilter());
    if (filePath.isEmpty()) return;

    filePath = MindMapIO::ensureExtension(filePath);
    if (performSave(filePath)) {
        if (!previousPath.isEmpty() && QString::compare(previousPath, filePath, Qt::CaseInsensitive) != 0) {
            deleteBackupForFile(previousPath);
        }
        m_view->setCurrentFilePath(filePath);
        m_undoStack->setClean();
        updateTitle();
    }
}

bool MainWindow::performSave(const QString& filePath) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::SavePerform);
    MindMapNode* root = findRoot();
    if (!root) {
        QMessageBox::warning(this, QString("保存失败"), QString("找不到根节点"));
        return false;
    }
    if (MindMapIO::saveToFile(root, filePath)) {
        addRecentFile(filePath);
        deleteBackupForFile(filePath);
        return true;
    } else {
        QMessageBox::warning(this, QString("保存失败"), QString("无法写入文件"));
        return false;
    }
}

bool MainWindow::openFile(const QString& filePath) {
    return performOpen(filePath);
}

void MainWindow::openFileAtStartup(const QString& filePath) {
    if (filePath.isEmpty()) return;
    performOpen(filePath);
}

bool MainWindow::performOpen(const QString& filePath) {
    QString pathToOpen = filePath;
    if (!confirmBackupRestore(filePath, &pathToOpen)) {
        return false;
    }

    QJsonObject previousDocument;
    const QString previousFilePath = m_view->currentFilePath();
    if (MindMapNode* previousRoot = findRoot()) {
        previousDocument = MindMapIO::serialize(previousRoot);
    }
    m_undoStack->clear();
    clearScene();

    auto restorePreviousDocument = [&]() {
        clearScene();
        m_undoStack->clear();
        if (!previousDocument.isEmpty()) {
            MindMapNode* previousRoot = MindMapIO::deserialize(previousDocument, m_scene);
            if (previousRoot) {
                previousRoot->layoutTree(0, 0);
                previousRoot->adjustLayout();
                previousRoot->updateAllEdges();
                previousRoot->setNodeSelected(true);
            }
        } else {
            setupDefaultTree();
        }
        m_view->setCurrentFilePath(previousFilePath);
        updateTitle();
    };

    QProgressDialog progress(QStringLiteral("正在载入思维导图…"), QStringLiteral("取消"), 0, 100, this);
    progress.setWindowTitle(QStringLiteral("载入思维导图"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    bool cancelled = false;
    auto lastUiUpdate = QDateTime::currentMSecsSinceEpoch();
    auto reportProgress = [&](const MindMapIO::LoadProgress& update) {
        if (progress.wasCanceled()) {
            cancelled = true;
            return false;
        }
        int value = 0;
        QString label;
        switch (update.stage) {
        case MindMapIO::LoadProgress::Stage::Parsing:
            value = update.total > 0 ? 5 + update.current * 5 / update.total : 5;
            label = QStringLiteral("正在读取思维导图文件…");
            break;
        case MindMapIO::LoadProgress::Stage::BuildingNodes:
            value = update.total > 0 ? 10 + update.current * 75 / update.total : 10;
            label = QStringLiteral("一大批节点正在赶来（%1 / %2）…").arg(update.current).arg(update.total);
            break;
        case MindMapIO::LoadProgress::Stage::Finalizing:
            value = 90;
            label = update.detail;
            break;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (update.current <= 1 || update.current == update.total || now - lastUiUpdate >= 50) {
            progress.setLabelText(label);
            progress.setValue(value);
            lastUiUpdate = now;
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (progress.wasCanceled()) {
                cancelled = true;
                return false;
            }
        }
        return true;
    };
    MindMapNode* root = MindMapIO::loadFromFile(pathToOpen, m_scene, reportProgress);
    if (cancelled) {
        restorePreviousDocument();
        progress.close();
        return false;
    }
    if (!root) {
        QMessageBox::warning(this, QString("打开失败"), QString("无法解析文件或格式不正确"));
        restorePreviousDocument();
        return false;
    }

    reportProgress({MindMapIO::LoadProgress::Stage::Finalizing, 0, 1,
                    QStringLiteral("正在计算布局…")});
    progress.setLabelText(QStringLiteral("正在计算布局…"));
    progress.setValue(90);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
    root->setNodeSelected(true);
    reportProgress({MindMapIO::LoadProgress::Stage::Finalizing, 1, 1,
                    QStringLiteral("载入完成")});
    progress.setValue(100);
    progress.close();

    // Rebuild the scene bounds from the loaded map. The constructor's
    // default scene rect is only a fallback for new/small maps.
    QRectF contentRect = m_scene->itemsBoundingRect();
    if (!contentRect.isEmpty()) {
        constexpr qreal margin = 200.0;
        constexpr qreal minimumSize = 4000.0;
        contentRect.adjust(-margin, -margin, margin, margin);
        if (contentRect.width() < minimumSize) {
            const qreal extra = (minimumSize - contentRect.width()) / 2.0;
            contentRect.adjust(-extra, 0, extra, 0);
        }
        if (contentRect.height() < minimumSize) {
            const qreal extra = (minimumSize - contentRect.height()) / 2.0;
            contentRect.adjust(0, -extra, 0, extra);
        }
        m_scene->setSceneRect(contentRect);
    }
    m_view->centerOn(root);

    m_view->setCurrentFilePath(filePath);
    addRecentFile(filePath);
    if (pathToOpen != filePath) {
        m_undoStack->push(new QUndoCommand(QStringLiteral("恢复自动备份")));
    }
    updateTitle();
    return true;
}

QStringList MainWindow::recentFiles() {
    QSettings settings(ORG_NAME, QString("leap"));
    QStringList storedFiles = settings.value(RECENT_FILES_KEY).toStringList();
    QStringList existingFiles;

    for (const QString& filePath : storedFiles) {
        QFileInfo info(filePath);
        if (!info.exists() || !info.isFile()) continue;

        const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
        if (!existingFiles.contains(absolutePath, Qt::CaseInsensitive)) {
            existingFiles.append(absolutePath);
        }
    }

    if (existingFiles != storedFiles) {
        settings.setValue(RECENT_FILES_KEY, existingFiles);
    }
    return existingFiles;
}

void MainWindow::addRecentFile(const QString& filePath) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return;

    const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
    QStringList files = recentFiles();
    files.removeAll(absolutePath);
    for (int i = files.size() - 1; i >= 0; --i) {
        if (QString::compare(files.at(i), absolutePath, Qt::CaseInsensitive) == 0) {
            files.removeAt(i);
        }
    }
    files.prepend(absolutePath);

    while (files.size() > MAX_RECENT_FILES) {
        files.removeLast();
    }

    QSettings settings(ORG_NAME, QString("leap"));
    settings.setValue(RECENT_FILES_KEY, files);
}

QString MainWindow::backupFilePathFor(const QString& filePath) const {
    if (filePath.isEmpty()) return QString();

    const QString absolutePath = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    const QByteArray hash = QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::tempPath() + QStringLiteral("/leap");
    }

    return QDir(basePath).filePath(QStringLiteral("backups/%1.leap").arg(QString::fromLatin1(hash)));
}

bool MainWindow::deleteBackupForFile(const QString& filePath) {
    const QString backupPath = backupFilePathFor(filePath);
    if (backupPath.isEmpty() || !QFileInfo::exists(backupPath)) {
        return true;
    }
    return QFile::remove(backupPath);
}

bool MainWindow::shouldPromptForBackup(const QString& filePath) const {
    const QString backupPath = backupFilePathFor(filePath);
    QFileInfo backupInfo(backupPath);
    if (!backupInfo.exists() || !backupInfo.isFile()) {
        return false;
    }

    QFileInfo originalInfo(filePath);
    if (!originalInfo.exists() || !originalInfo.isFile()) {
        return true;
    }

    return backupInfo.lastModified() > originalInfo.lastModified();
}

bool MainWindow::confirmBackupRestore(const QString& filePath, QString* pathToOpen) {
    if (!pathToOpen) return false;
    *pathToOpen = filePath;
    if (!shouldPromptForBackup(filePath)) {
        return true;
    }

    const QString backupPath = backupFilePathFor(filePath);
    const QString backupTime = QFileInfo(backupPath).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("发现自动备份"));
    msgBox.setText(QStringLiteral("检测到这个文件存在上次异常退出留下的自动备份。"));
    msgBox.setInformativeText(QStringLiteral("备份时间：%1\n原文件：%2").arg(backupTime, QDir::toNativeSeparators(filePath)));

    QPushButton* restoreButton = msgBox.addButton(QStringLiteral("恢复备份"), QMessageBox::AcceptRole);
    QPushButton* originalButton = msgBox.addButton(QStringLiteral("打开原文件"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = msgBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(restoreButton);
    msgBox.exec();

    if (msgBox.clickedButton() == restoreButton) {
        *pathToOpen = backupPath;
        return true;
    }
    if (msgBox.clickedButton() == originalButton) {
        deleteBackupForFile(filePath);
        *pathToOpen = filePath;
        return true;
    }
    if (msgBox.clickedButton() == cancelButton) {
        return false;
    }
    return false;
}

void MainWindow::performAutoBackup() {
    if (!m_undoStack || m_undoStack->isClean()) return;

    const QString filePath = m_view ? m_view->currentFilePath() : QString();
    if (filePath.isEmpty()) return;

    MindMapNode* root = findRoot();
    if (!root) return;

    const QString backupPath = backupFilePathFor(filePath);
    QDir backupDir(QFileInfo(backupPath).absolutePath());
    if (!backupDir.exists() && !backupDir.mkpath(QStringLiteral("."))) {
        qWarning() << "Failed to create auto-backup directory:" << backupDir.absolutePath();
        return;
    }

    if (!MindMapIO::saveToFile(root, backupPath)) {
        qWarning() << "Failed to write auto-backup:" << backupPath;
    }
}

void MainWindow::onOpen() {
    auto choice = askSaveChanges();
    if (choice == MainWindow::SaveChoice::Cancelled) return;
    if (choice == MainWindow::SaveChoice::Discarded) {
        deleteBackupForFile(m_view->currentFilePath());
    }

    QString filePath = QFileDialog::getOpenFileName(this, QString("打开思维导图"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    MindMapIO::fileFilter());
    if (filePath.isEmpty()) return;

    if (performOpen(filePath)) return;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".leap", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        QString filePath = url.toLocalFile();
        if (filePath.endsWith(".leap", Qt::CaseInsensitive)) {
            openFile(filePath);
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void MainWindow::onInsertFormula() {
    if (m_view && m_view->selectedNodeCount() > 1) return;
    MindMapNode* targetNode = nullptr;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected() || node->isEditing()) {
                targetNode = node;
                break;
            }
        }
    }

    if (!targetNode) return;

    if (!targetNode->isEditing()) {
        targetNode->startEdit();
    }

    targetNode->setBlockFocusOut(true);
    MindMapFormulaDialog dialog(this);

    if (dialog.exec() == QDialog::Accepted) {
        QString latexCode = dialog.formula();
        if (!latexCode.trimmed().isEmpty()) {
            QImage image = MindMapFormulaDialog::renderLatexToImage(latexCode, 16);
            if (!image.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(image);
                pixmap.setDevicePixelRatio(image.devicePixelRatio());
                FormulaBlock* formulaBlock = new FormulaBlock(pixmap, latexCode, targetNode);
                formulaBlock->pixmapItem()->setFormulaBlock(formulaBlock);

                int insertIdx = 0;
                if (targetNode->selectedBlock()) {
                    insertIdx = targetNode->blockIndex(targetNode->selectedBlock()) + 1;
                }

                if (m_undoStack) {
                    m_undoStack->push(new InsertBlockCommand(targetNode, formulaBlock, insertIdx, m_scene));
                } else {
                    targetNode->addBlock(formulaBlock, insertIdx);
                    formulaBlock->pixmapItem()->setFormulaBlock(formulaBlock);
                    targetNode->setSelectedBlock(formulaBlock);
                    MindMapNode* root = targetNode->getRoot();
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                    // 【新增】插入公式后，自动调整文本块宽度与节点一致
                    targetNode->autoFitTextBlockWidths();
                    targetNode->calcNodeSize();
                    root->clearEdges();
                    root->layoutTree(0, 0);
                    root->adjustLayout();
                    root->updateAllEdges();
                    if (m_scene) m_scene->update();
                }
            }
        }
    }

    targetNode->setBlockFocusOut(false);
    if (targetNode->editingTextBlock()) {
        targetNode->editingTextBlock()->textItem()->setFocus();
    }
}

void MainWindow::onInsertImage() {
    if (m_view && m_view->selectedNodeCount() > 1) return;
    MindMapNode* targetNode = nullptr;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected() || node->isEditing()) {
                targetNode = node;
                break;
            }
        }
    }

    if (!targetNode) return;

    if (!targetNode->isEditing()) {
        targetNode->startEdit();
    }

    QString filePath = QFileDialog::getOpenFileName(this, QString("插入图片"),
                                                    QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                    QString("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"));
    if (filePath.isEmpty()) return;

    QImage image(filePath);
    if (image.isNull()) {
        QMessageBox::warning(this, QString("插入失败"), QString("无法加载图片"));
        return;
    }

    int insertIdx = 0;
    if (targetNode->selectedBlock()) {
        insertIdx = targetNode->blockIndex(targetNode->selectedBlock()) + 1;
    }
    ImageBlock* imgBlock = new ImageBlock(image, targetNode);
    static qint64 imgCounter = 0;
    imgBlock->setImageName(QString("mmimg_%1").arg(++imgCounter));
    imgBlock->imageItem()->setImageBlock(imgBlock);

    if (m_undoStack) {
        m_undoStack->push(new InsertBlockCommand(targetNode, imgBlock, insertIdx, m_scene));
    } else {
        targetNode->addBlock(imgBlock, insertIdx);
        imgBlock->imageItem()->setImageBlock(imgBlock);
        targetNode->setSelectedBlock(imgBlock);
        MindMapNode* root = targetNode->getRoot();
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
        // 【新增】插入图片后，自动调整文本块宽度与节点一致
        targetNode->autoFitTextBlockWidths();
        targetNode->calcNodeSize();
        root->clearEdges();
        root->layoutTree(0, 0);
        root->adjustLayout();
        root->updateAllEdges();
        if (m_scene) m_scene->update();
    }
}

void MainWindow::onHelp() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("帮助"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* guideButton = new QPushButton(QStringLiteral("入门指引"), &dialog);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), &dialog);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(guideButton);
    buttons->addWidget(closeButton);
    auto* helpText = new QLabel(QStringLiteral(
        "<b>快捷键说明</b><br><br>"
        "<b>Enter</b> - 添加同级节点<br>"
        "<b>Tab</b> - 添加子节点<br>"
        "<b>双击 / 空格</b> - 编辑节点<br>"
        "<b>Shift+Enter</b> - 在光标处拆分文本块<br>"
        "<b>Delete / Backspace</b> - 删除节点/选中块<br>"
        "<b>方向键</b> - 节点导航<br>"
        "<b>Ctrl+S / Ctrl+Shift+S</b> - 保存 / 另存为<br>"
        "<b>Ctrl+O / Ctrl+N</b> - 打开 / 新建<br>"
        "<b>Ctrl+Z / Ctrl+Y</b> - 撤销 / 恢复<br>"
        "<b>Ctrl+C / X / V</b> - 复制 / 剪切 / 粘贴节点<br>"
        "<b>Ctrl+B</b> - 加粗<br><b>Ctrl+I</b> - 斜体<br>"
        "<b>Esc</b> - 退出编辑 / 退出程序"), &dialog);
    helpText->setWordWrap(true);
    layout->addWidget(helpText);
    layout->addLayout(buttons);
    bool replay = false;
    connect(guideButton, &QPushButton::clicked, &dialog, [&] { replay = true; dialog.accept(); });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
    if (replay) QTimer::singleShot(0, this, &MainWindow::startNewUserGuide);
    return;
    QMessageBox::information(this, QString("帮助"),
                             QString(
                                 "<b>快捷键说明</b><br><br>"
                                 "<b>Enter</b> - 添加同级节点<br>"
                                 "<b>Tab</b> - 添加子节点<br>"
                                 "<b>双击 / 空格</b> - 编辑节点<br>"
                                 "<b>Shift+Enter</b> - 在光标处拆分文本块<br>"
                                 "<b>Delete / Backspace</b> - 删除节点/选中块<br>"
                                 "<b>方向键</b> - 节点导航<br>"
                                 "<b>Ctrl+S / Ctrl+Shift+S</b> - 保存 / 另存为<br>"
                                 "<b>Ctrl+O / Ctrl+N</b> - 打开 / 新建<br>"
                                 "<b>Ctrl+Z / Ctrl+Y</b> - 撤销 / 恢复<br>"
                                 "<b>Ctrl+C / X / V</b> - 复制 / 剪切 / 粘贴节点<br>"
                                 "<b>Ctrl+B</b> - 加粗<br>"
                                 "<b>Ctrl+I</b> - 斜体<br>"
                                 "<b>Esc</b> - 退出编辑 / 退出程序"
                                 ));
}

void MainWindow::startNewUserGuide() { if (m_newUserGuide) m_newUserGuide->start(); }

void MainWindow::onAbout() {
    QMessageBox::about(this, QString("关于 leap"), buildAboutText());
}

void MainWindow::clearScene() {
    if (m_view) m_view->resetForSceneChange();
    auto* editingNode = MindMapNode::findEditingNode(m_scene);
    if (editingNode) editingNode->finishEdit();

    // 【修复】安全清理场景，避免 m_scene->clear() 与 MindMapNode destructor 的冲突
    // 问题：m_scene->clear() 遍历 items 列表删除 item
    // 当删除 parent MindMapNode 时，其 destructor 会递归 delete 子节点
    // 但子节点仍在 items 列表中，clear() 后续会访问已释放的指针 -> crash
    //
    // 解决：
    // 1. 收集所有根节点
    // 2. 从 scene 中移除所有 MindMapNode
    // 3. 递归断开所有 parent-child 关系（避免 destructor 中的 removeChild 调用）
    // 4. 安全地 delete 根节点（destructor 递归 delete 子树，不会访问 parent）
    // 5. 清理 scene 中剩余的 edge items 等

    // 1. 收集所有根节点
    QVector<MindMapNode*> rootNodes;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) {
                rootNodes.append(node);
            }
        }
    }

    // 2. 从 scene 中移除所有 MindMapNode（不 delete）
    for (auto* node : rootNodes) {
        m_scene->removeItem(node);
    }

    // 3. 【关键修复】递归断开所有 parent-child 关系
    // 这样每个节点的 destructor 中 m_parent 为 nullptr，不会调用 removeChild
    // parent 的 destructor 遍历 m_children 时，delete 每个 child
    // child 的 destructor 中 m_parent 为 nullptr，安全
    std::function<void(MindMapNode*)> detachTree;
    detachTree = [&](MindMapNode* node) {
        if (!node) return;
        // 断开与 parent 的关系
        node->m_parent = nullptr;
        // 递归处理子节点
        for (auto* child : node->m_children) {
            detachTree(child);
        }
    };
    for (auto* node : rootNodes) {
        detachTree(node);
    }

    // 4. 安全地 delete 根节点
    // 根节点的 destructor 会遍历 m_children 并 delete 每个 child
    // 由于 m_parent 已经被设为 nullptr，child 的 destructor 不会调用 removeChild
    // 由于已经从 scene 中移除，removeItem 也不会被调用
    for (auto* node : rootNodes) {
        delete node;
    }

    // 5. 清理 scene 中剩余的 items（edge items 等）
    m_scene->clear();
}

MindMapNode* MainWindow::findRoot() const {
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) return node;
        }
    }
    return nullptr;
}

void MainWindow::updateTitle() {
    QString fileName = m_view->currentFilePath();
    if (fileName.isEmpty()) fileName = QString("未命名");
    else fileName = QFileInfo(fileName).fileName();

    QString modifiedMark = m_undoStack->isClean() ? QString("") : QString("*");
    setWindowTitle(QString("%1%2").arg(fileName, modifiedMark));
}

void MainWindow::onExportOutline() {
    MindMapNode* root = findRoot();
    if (!root) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("没有可导出的节点"));
        return;
    }
    const QString currentPath = m_view->currentFilePath();
    const QString defaultPath = currentPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/大纲.md")
        : QFileInfo(currentPath).absolutePath() + QStringLiteral("/") +
          QFileInfo(currentPath).completeBaseName() + QStringLiteral(".md");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出大纲"),
                                               defaultPath, QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");

    auto escapeText = [](const QString& plain) {
        QString escaped;
        for (QChar ch : plain) {
            if (QStringLiteral("\\`*_{}[]<>()#+-.!|>$").contains(ch))
                escaped += QLatin1Char('\\');
            escaped += ch;
        }
        return escaped;
    };
    QString markdown;
    std::function<void(MindMapNode*, int)> appendNode;
    appendNode = [&](MindMapNode* node, int depth) {
        QStringList lines;
        for (MindMapBlock* block : node->blocks()) {
            if (block->type() == BlockType::Text) {
                QString text = static_cast<TextBlock*>(block)->toPlainText();
                text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                text.replace(QChar(0x2028), QLatin1Char('\n'));
                text.replace(QChar(0x2029), QLatin1Char('\n'));
                for (const QString& line : text.split(QLatin1Char('\n')))
                    lines.append(escapeText(line));
            } else if (block->type() == BlockType::Formula) {
                lines.append(QStringLiteral("$$"));
                lines.append(static_cast<FormulaBlock*>(block)->latexCode().split(QLatin1Char('\n')));
                lines.append(QStringLiteral("$$"));
            }
        }
        while (!lines.isEmpty() && lines.first().isEmpty()) lines.removeFirst();
        while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        if (lines.isEmpty()) lines.append(QStringLiteral("（空节点）"));
        const QString indent(depth * 4, QLatin1Char(' '));
        markdown += indent + QStringLiteral("- ") + lines.first() + QLatin1Char('\n');
        for (int i = 1; i < lines.size(); ++i)
            markdown += indent + QStringLiteral("  ") + lines[i] + QLatin1Char('\n');
        for (MindMapNode* child : node->childNodes()) appendNode(child, depth + 1);
    };
    appendNode(root, 0);
    QSaveFile file(path);
    const QByteArray data = markdown.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出成功"),
                             QStringLiteral("已保存到：%1").arg(path));
}

void MainWindow::onExportPng() {
    QRectF itemsRect = m_scene->itemsBoundingRect();
    if (itemsRect.isEmpty()) {
        QMessageBox::warning(this, QString("导出失败"), QString("场景为空，没有可导出的内容"));
        return;
    }
    const qreal MARGIN = 40;
    itemsRect.adjust(-MARGIN, -MARGIN, MARGIN, MARGIN);
    QString filePath = QFileDialog::getSaveFileName(this, QString("导出 PNG"),
                                                    QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                    QString("PNG Images (*.png);;All files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QString(".png"), Qt::CaseInsensitive)) filePath += QString(".png");
    int imgWidth = qCeil(itemsRect.width());
    int imgHeight = qCeil(itemsRect.height());
    if (imgWidth <= 0 || imgHeight <= 0) {
        QMessageBox::warning(this, QString("导出失败"), QString("计算出的图片尺寸无效"));
        return;
    }
    QImage image(imgWidth, imgHeight, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, QRectF(), itemsRect);
    painter.end();
    if (image.save(filePath, "PNG")) {
        QMessageBox::information(this, QString("导出成功"), QString("已保存到: %1").arg(filePath));
    } else {
        QMessageBox::warning(this, QString("导出失败"), QString("无法写入文件，请检查路径和权限"));
    }
}

void MainWindow::onExportPdf() {
    QRectF itemsRect = m_scene->itemsBoundingRect();
    if (itemsRect.isEmpty()) {
        QMessageBox::warning(this, QString("导出失败"), QString("场景为空，没有可导出的内容"));
        return;
    }
    const qreal MARGIN = 40;
    itemsRect.adjust(-MARGIN, -MARGIN, MARGIN, MARGIN);
    QString filePath = QFileDialog::getSaveFileName(this, QString("导出 PDF"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    QString("PDF Documents (*.pdf);;All files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QString(".pdf"), Qt::CaseInsensitive)) filePath += QString(".pdf");
    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QSizeF(itemsRect.width(), itemsRect.height()), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, QRectF(), itemsRect);
    painter.end();
    QMessageBox::information(this, QString("导出成功"), QString("已保存到: %1").arg(filePath));
}

void MainWindow::onExportSvg() {
    QRectF itemsRect = m_scene->itemsBoundingRect();
    if (itemsRect.isEmpty()) {
        QMessageBox::warning(this, QString("导出失败"), QString("场景为空，没有可导出的内容"));
        return;
    }
    const qreal MARGIN = 40;
    itemsRect.adjust(-MARGIN, -MARGIN, MARGIN, MARGIN);
    QString filePath = QFileDialog::getSaveFileName(this, QString("导出 SVG"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    QString("SVG Images (*.svg);;All files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QString(".svg"), Qt::CaseInsensitive)) filePath += QString(".svg");
    QSvgGenerator generator;
    generator.setFileName(filePath);
    generator.setSize(QSize(qCeil(itemsRect.width()), qCeil(itemsRect.height())));
    generator.setViewBox(QRectF(0, 0, itemsRect.width(), itemsRect.height()));
    generator.setTitle(QString("MindMap Export"));
    generator.setDescription(QString("Exported from leap v%1 by %2").arg(APP_VERSION, AUTHOR_NAME));
    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, QRectF(), itemsRect);
    painter.end();
    QMessageBox::information(this, QString("导出成功"), QString("已保存到: %1").arg(filePath));
}

void MainWindow::onExportPpt() {
    QRectF itemsRect = m_scene->itemsBoundingRect();
    if (itemsRect.isEmpty()) {
        QMessageBox::warning(this, QString("导出失败"), QString("场景为空，没有可导出的内容"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, QString("导出 PPT"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                    QString("PowerPoint Presentations (*.pptx);;All files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QString(".pptx"), Qt::CaseInsensitive)) filePath += QString(".pptx");

    QString errorMessage;
    if (MindMapPptExporter::exportToPptx(m_scene, filePath, &errorMessage)) {
        QMessageBox::information(this, QString("导出成功"), QString("已保存到: %1").arg(filePath));
    } else {
        QMessageBox::warning(this, QString("导出失败"),
                             errorMessage.isEmpty() ? QString("无法写入 PPT 文件，请检查路径和权限") : errorMessage);
    }
}

void MainWindow::setupDefaultTree() {
    auto* root = new MindMapNode(QString("根节点"));
    auto* child1 = new MindMapNode(QString("子节点1"));
    auto* child2 = new MindMapNode(QString("子节点2"));
    auto* grand1 = new MindMapNode(QString("孙节点1"));
    auto* grand2 = new MindMapNode(QString("孙节点2"));

    root->addChild(child1);
    root->addChild(child2);
    child1->addChild(grand1);
    child1->addChild(grand2);

    m_scene->addItem(root);
    m_scene->addItem(child1);
    m_scene->addItem(child2);
    m_scene->addItem(grand1);
    m_scene->addItem(grand2);

    // 只在“新建文档”这条路径上做一次整树样式刷新：上面这些节点都是先无 parent 创建、
    // 再通过 addChild 挂接，addChild 只刷新子树，根节点永远走不到 applyStyle(updateText=true)，
    // 于是新建后根节点的字体/字色/对齐/行距会停留在 QTextDocument 默认值上。
    // 加载文件不能走这里（文件里的显式格式由 MindMapIO::isLoading() 保护），
    // 所以刷新放在 setupDefaultTree() 内部 —— 它只被 onNew() 和“恢复不了上一个文档”时调用。
    root->applyLevelStyles(root->levelStyles());

    root->layoutTree(0, 0);
    root->adjustLayout();
    root->updateAllEdges();
    root->setNodeSelected(true);
    m_view->centerOn(root);
}

void MainWindow::setupResponsiveToolbar() {
    m_moreBtn = new QToolButton(this);
    m_moreBtn->setText("⋮");
    m_moreBtn->setFixedSize(36, 36);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setAutoRaise(false);
    m_moreBtn->setStyleSheet(
        "QToolButton {"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 8px;"
        "  color: #5f6368;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "QToolButton:hover {"
        "  background-color: rgba(241, 245, 249, 0.96);"
        "  border-color: #cbd5e1;"
        "}"
    );
    m_moreBtn->setVisible(false);

    QMenu* moreMenu = new QMenu(m_moreBtn);
    moreMenu->setStyleSheet(
        "QMenu {"
        "  background-color: #ffffff;"
        "  border: 1px solid #dadce0;"
        "  border-radius: 8px;"
        "  padding: 6px;"
        "}"
        "QMenu::item {"
        "  padding: 8px 24px;"
        "  border-radius: 4px;"
        "  color: #3c4043;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #e8f0fe;"
        "  color: #1a73e8;"
        "}"
    );
    m_moreBtn->setMenu(moreMenu);
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
}

void MainWindow::updateZoomLabel() {
    if (!m_view || !m_zoomLabel) return;
    QTransform transform = m_view->transform();
    qreal scale = transform.m11() * 100;
    m_zoomLabel->setText(QString("%1%").arg(qRound(scale)));

    int x = width() - m_zoomLabel->width() - 20;
    int y = height() - m_zoomLabel->height() - 20;
    m_zoomLabel->move(x, y);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateZoomLabel();
    updateFloatingEditToolbar();
}

// 【优化】应用主题（深色/浅色）
void MainWindow::applyTheme(bool dark) {
    if (!m_toolbar) return;

    // 1. 设置工具栏背景（使用样式表，但只修改 QFrame 自身背景）
    m_toolbar->setStyleSheet(dark ? 
        "QFrame { background-color: #252526; border-bottom: 1px solid #333333; }" :
        "QFrame { background-color: #ffffff; border-bottom: 1px solid #e2e8f0; }"
    );

    // 2. 设置画布背景
    m_view->setBackgroundBrush(QBrush(dark ? QColor(30, 30, 30) : Qt::white));

    // 3. 设置缩放指示器样式
    m_zoomLabel->setStyleSheet(dark ?
        "QLabel { background-color: rgba(45, 45, 45, 0.9); border: 1px solid #3c3c3c; border-radius: 12px; color: #cccccc; font-size: 11px; font-weight: 500; padding: 4px 10px; }" :
        "QLabel { background-color: rgba(255, 255, 255, 0.9); border: 1px solid #dadce0; border-radius: 12px; color: #5f6368; font-size: 11px; font-weight: 500; padding: 4px 10px; }"
    );

    // 4. 更新按钮样式
    updateToolbarButtonStyles();
    if (m_floatingEditToolbar) {
        m_floatingEditToolbar->applyTheme(dark);
    }

    // 5. 更新分隔线颜色
    auto separators = m_toolbar->findChildren<QFrame*>();
    for (auto* sep : separators) {
        if (sep->frameShape() == QFrame::VLine) {
                sep->setStyleSheet(dark ? "color: #444444;" : "color: #e2e8f0;");
        }
    }
    if (m_editBtnSeparator) {
        m_editBtnSeparator->setStyleSheet(dark ? "color: #444444;" : "color: #e2e8f0;");
    }

    // 6. 设置 MindMapNode 全局深色模式状态
    MindMapNode::setDarkMode(dark);

    // 7. 更新所有节点颜色（调用 applyThemeColors 来更新文本颜色）
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            node->applyThemeColors();
        }
    }

    // 【修复】重新绘制所有连接线，使颜色随主题实时切换
    // 先收集根节点（避免在遍历 items() 时修改 items 导致迭代器失效）
    QVector<MindMapNode*> rootNodes;
    for (auto* item : m_scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) {
                rootNodes.append(node);
            }
        }
    }
    for (auto* root : rootNodes) {
        root->clearEdges();
        root->updateAllEdges();
    }

    // 8. 触发一次场景更新
    if (m_scene) {
        m_scene->update();
    }

    // 9. 滚动条样式
    QString scrollBarStyle = dark ? R"(
        QScrollBar:vertical { background: #2d2d2d; width: 12px; margin: 0px; border-radius: 6px; }
        QScrollBar::handle:vertical { background: #4a4a4a; min-height: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #5a5a5a; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar:horizontal { background: #2d2d2d; height: 12px; margin: 0px; border-radius: 6px; }
        QScrollBar::handle:horizontal { background: #4a4a4a; min-width: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #5a5a5a; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
    )" : R"(
        QScrollBar:vertical { background: #f0f0f0; width: 12px; margin: 0px; border-radius: 6px; }
        QScrollBar::handle:vertical { background: #c0c0c0; min-height: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #a0a0a0; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar:horizontal { background: #f0f0f0; height: 12px; margin: 0px; border-radius: 6px; }
        QScrollBar::handle:horizontal { background: #c0c0c0; min-width: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #a0a0a0; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
    )";
    m_view->setStyleSheet(scrollBarStyle);

    // 10. Windows 标题栏深色模式
    setWindowsTitleBarDark(dark);
}

// 【新增】设置Windows标题栏深色模式
void MainWindow::setWindowsTitleBarDark(bool dark) {
    qDebug() << "[DEBUG] setWindowsTitleBarDark called, dark=" << dark;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;

    BOOL value = dark ? TRUE : FALSE;

    // 【修复】尝试两个不同的 DWM attribute 值（兼容不同 Windows 版本）
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_22H2, &value, sizeof(value));
    if (FAILED(hr)) {
        hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_20H1, &value, sizeof(value));
    }

    // 【关键修复】强制刷新标题栏：通过微调窗口尺寸触发 DWM 重绘
    // 参考: https://stackoverflow.com/questions/74667186/redraw-titlebar-after-setting-dark-mode-in-windows
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        RECT rect = {};
        ::GetWindowRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        // 先缩小1像素再恢复，强制 DWM 重绘非客户区
        SetWindowPos(hwnd, nullptr, 0, 0, width - 1, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        // 窗口不可见时，使用传统方式
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }

    // 额外发送 WM_NCPAINT 强制重绘标题栏
    RedrawWindow(hwnd, nullptr, nullptr, 
                 RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
#endif
}
// 【新增】询问是否保存更改，返回用户的选择结果
// 【新增】保存确认对话框的返回结果
MainWindow::SaveChoice MainWindow::askSaveChanges() {
    if (m_undoStack->isClean()) {
        return MainWindow::SaveChoice::Saved; // 没有更改，视为已保存
    }

    QString fileName = m_view->currentFilePath();
    if (fileName.isEmpty()) fileName = QString("未命名");
    else fileName = QFileInfo(fileName).fileName();

    QMessageBox msgBox(this);
    msgBox.setStyleSheet(QStringLiteral(R"(
        QMessageBox { background: #f8fafc; }
        QLabel { color: #1f2937; font-size: 14px; }
        QPushButton { min-width: 86px; min-height: 24px; max-height: 24px; padding: 3px 12px; font-size: 15px; font-weight: 500; border: 1px solid #cbd5e1; border-radius: 6px; background: #ffffff; color: #1f2937; }
        QPushButton:hover { background: #eff6ff; border-color: #93c5fd; }
    )"));
    msgBox.setWindowTitle(QString("leap"));
    msgBox.setText(QString("是否将更改保存到 %1？").arg(fileName));
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    msgBox.button(QMessageBox::Save)->setText(QString("保存"));
    msgBox.button(QMessageBox::Discard)->setText(QString("不保存"));
    msgBox.button(QMessageBox::Cancel)->setText(QString("取消"));
    msgBox.button(QMessageBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    msgBox.setStyleSheet(msgBox.styleSheet() + QStringLiteral(R"(
        QPushButton#primaryButton { background: #2563eb; border-color: #2563eb; color: #ffffff; font-weight: 600; }
        QPushButton#primaryButton:hover { background: #1d4ed8; border-color: #1d4ed8; }
    )"));

    int ret = msgBox.exec();
    if (ret == QMessageBox::Save) {
        onSave();
        if (!m_undoStack->isClean()) {
            return MainWindow::SaveChoice::Cancelled; // 保存失败或用户取消了保存对话框
        }
        return MainWindow::SaveChoice::Saved;
    } else if (ret == QMessageBox::Discard) {
        return MainWindow::SaveChoice::Discarded;
    } else {
        return MainWindow::SaveChoice::Cancelled;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    auto choice = askSaveChanges();
    if (choice == MainWindow::SaveChoice::Cancelled) {
        event->ignore();
    } else {
        deleteBackupForFile(m_view->currentFilePath());
        event->accept();
    }
}
