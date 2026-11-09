#include "MindMapSettingsDialog.h"

#include "MindMapNode.h"
#include "SettingsWidgets.h"
#include "UiTheme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_22H2
#define DWMWA_USE_IMMERSIVE_DARK_MODE_22H2 38
#endif
#endif

namespace {

const int kNavWidth = 168;
const int kPreviewWidth = 284;
const int kFormLabelWidth = 78;
const int kFieldMaxWidth = 300;

QString themeModeKey() { return QStringLiteral("themeMode"); }
QString darkThemeKey() { return QStringLiteral("darkTheme"); }

// 样式页底部的说明文字：解释“即时预览 + 取消不保存”的交互模型。
QLabel* makePageNote(QWidget* parent) {
    auto* note = new QLabel(QStringLiteral("修改会即时反映在右侧预览中；点击「取消」不会保存本次修改。"),
                            parent);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    return note;
}

bool isValidMode(const QString& mode) {
    return mode == QLatin1String("light") || mode == QLatin1String("dark")
        || mode == QLatin1String("system");
}

QString levelDisplayName(int level) {
    switch (level) {
    case 1: return QStringLiteral("1 级子节点");
    case 2: return QStringLiteral("2 级及更深");
    default: return QStringLiteral("根节点");
    }
}

} // namespace

MindMapSettingsDialog::MindMapSettingsDialog(MindMapNode* root, QWidget* parent)
    : QDialog(parent), m_root(root) {
    setWindowTitle(QStringLiteral("设置"));
    resize(880, 580);
    // 最小宽度要保证“导航 + 内容列 + 预览栏”都放得下（内容不足时页面会横向裁切），
    // 高度方向由滚动区负责，可以比内容矮。
    setMinimumSize(860, 500);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    // 对话框基准字号：15px 能在 100% 缩放的 Windows 中文界面里保持紧凑但可读。
    QFont baseFont = font();
    baseFont.setPixelSize(15);
    setFont(baseFont);

    m_originalStyles = root ? root->levelStyles() : MindMapLevelStyles{};
    m_editStyles = m_originalStyles;
    m_appliedStyles = m_originalStyles;

    QSettings settings;
    const QString storedMode = settings.value(themeModeKey()).toString();
    if (isValidMode(storedMode)) {
        m_themeMode = storedMode;
    } else {
        m_themeMode = settings.value(darkThemeKey(), false).toBool() ? QStringLiteral("dark")
                                                                    : QStringLiteral("light");
    }
    m_committedMode = m_themeMode;

    buildUi();
    applyThemeToDialog();
    loadCurrentStyle();

    // “跟随系统”需要在系统主题变化时实时跟随。
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (m_themeMode == QLatin1String("system")) {
                applyThemeToDialog();
                emit themeChanged(effectiveDark());
            }
        });
    }
}

// ============================================================
// 构建界面
// ============================================================

void MindMapSettingsDialog::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_nav = new QListWidget(body);
    m_nav->setObjectName(QStringLiteral("categoryList"));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    {
        QFont navFont = font();
        navFont.setPixelSize(15);
        m_nav->setFont(navFont);
    }

    // 导航也放进滚动区：以后分类变多时，左栏可以自己滚动而不是把列表压扁。
    auto* navScroll = new QScrollArea(body);
    navScroll->setObjectName(QStringLiteral("navScroll"));
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    navScroll->viewport()->setAutoFillBackground(false);
    navScroll->setFixedWidth(kNavWidth);
    navScroll->setWidget(m_nav);
    bodyLayout->addWidget(navScroll);

    auto* content = new QWidget(body);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(22, 18, 22, 0);
    contentLayout->setSpacing(14);

    // ---- 页头：标题 / 说明 / 层级分段控件 ----
    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(4);
    m_pageTitle = new QLabel(content);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    m_pageSubtitle = new QLabel(content);
    m_pageSubtitle->setObjectName(QStringLiteral("pageSubtitle"));
    m_pageSubtitle->setWordWrap(true);
    titleBlock->addWidget(m_pageTitle);
    titleBlock->addWidget(m_pageSubtitle);
    headerLayout->addLayout(titleBlock, 1);

    m_levelScope = new SegmentedControl(content);
    m_levelScope->addItem(QStringLiteral("根节点"), QStringLiteral("root"));
    m_levelScope->addItem(QStringLiteral("1 级"), QStringLiteral("firstChild"));
    m_levelScope->addItem(QStringLiteral("2 级+"), QStringLiteral("deeper"));
    headerLayout->addWidget(m_levelScope, 0, Qt::AlignTop);
    contentLayout->addLayout(headerLayout);

    // ---- 分页 + 预览 ----
    auto* columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);

    m_pages = new QStackedWidget(content);
    m_pages->addWidget(wrapPage(buildFontPage()));
    m_pages->addWidget(wrapPage(buildParagraphPage()));
    m_pages->addWidget(wrapPage(buildNodePage()));
    m_pages->addWidget(wrapPage(buildThemePage()));
    m_pages->addWidget(wrapPage(buildOperationPage()));
    columns->addWidget(m_pages, 1);

    m_previewPanel = buildPreviewPanel();
    columns->addWidget(m_previewPanel, 0);
    contentLayout->addLayout(columns, 1);

    bodyLayout->addWidget(content, 1);
    rootLayout->addWidget(body, 1);
    rootLayout->addWidget(buildFooter());

    const QStringList categories = {QStringLiteral("字体"), QStringLiteral("段落"),
                                    QStringLiteral("节点"), QStringLiteral("主题"),
                                    QStringLiteral("操作")};
    for (int i = 0; i < categories.size(); ++i) {
        addNavItem(categories.at(i), i);
    }

    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int) {
        const auto* item = m_nav->currentItem();
        if (!item) return;
        const int pageIndex = item->data(Qt::UserRole).toInt();
        m_pages->setCurrentIndex(pageIndex);
        refreshLevelChrome(pageIndex);
    });
    m_nav->setCurrentRow(0);

    // ---- 层级切换 ----
    connect(m_levelScope, &SegmentedControl::currentChanged, this, [this](int) {
        if (m_updating) return;
        loadCurrentStyle();
    });

    // ---- 表单控件 ----
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont&) {
        updateStyleFromControls();
    });
    connect(m_fontSize, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateStyleFromControls();
    });
    connect(m_alignment, &SegmentedControl::currentChanged, this, [this](int) {
        updateStyleFromControls();
    });
    connect(m_lineSpacing, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateStyleFromControls();
    });
    connect(m_shape, &SegmentedControl::currentChanged, this, [this](int) {
        updateStyleFromControls();
    });
    connect(m_borderWidth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateStyleFromControls();
    });

    connect(m_textColor, &ColorField::colorChanged, this, [this](const QColor& color) {
        if (m_updating) return;
        currentStyle().textColor = color;
        refreshPreview();
    });
    connect(m_fillColor, &ColorField::colorChanged, this, [this](const QColor& color) {
        if (m_updating) return;
        auto& style = currentStyle();
        style.fillColor = color;
        if (style.borderFollowsFill) {
            style.borderColor = color;
            m_borderColor->setColor(color);
        }
        refreshPreview();
    });
    connect(m_borderColor, &ColorField::colorChanged, this, [this](const QColor& color) {
        if (m_updating) return;
        auto& style = currentStyle();
        style.borderColor = color;
        if (style.borderFollowsFill) {
            // 跟随模式下边框颜色由填充色决定，用户不应改到这里。
            style.borderFollowsFill = false;
            m_borderFollow->setChecked(false);
            m_borderColor->setEnabled(true);
        }
        refreshPreview();
    });
    connect(m_borderFollow, &QToolButton::toggled, this, [this](bool checked) {
        if (m_updating) return;
        auto& style = currentStyle();
        style.borderFollowsFill = checked;
        if (checked) {
            style.borderColor = style.fillColor;
            m_borderColor->setColor(style.fillColor);
        }
        m_borderColor->setEnabled(!checked);
        refreshPreview();
    });
}

QWidget* MindMapSettingsDialog::wrapPage(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 页面底色由对话框统一绘制，滚动区保持透明，避免出现第二条底色带。
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(content);
    return scroll;
}

QWidget* MindMapSettingsDialog::createGroupCard(const QString& title, QGridLayout** outGrid) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("groupCard"));
    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(18, 16, 18, 18);
    outer->setSpacing(12);

    if (!title.isEmpty()) {
        auto* label = new QLabel(title, card);
        label->setObjectName(QStringLiteral("groupTitle"));
        outer->addWidget(label);
    }

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(12);
    // 输入列吃掉卡片内的剩余宽度；卡片本身限制了最大宽度，
    // 因此不需要再给控件设上限，也不会出现“标签被推到中间”的布局。
    grid->setColumnStretch(1, 1);
    outer->addLayout(grid);

    // 没有预览栏的页面卡片会更宽，限制一下避免输入框被拉成一条。
    card->setMaximumWidth(460);

    if (outGrid) *outGrid = grid;
    return card;
}

void MindMapSettingsDialog::addFormRow(QGridLayout* grid, int row, const QString& label,
                                       QWidget* field, const QString& hint) {
    auto* name = new QLabel(label, this);
    name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->setColumnMinimumWidth(0, kFormLabelWidth);
    grid->addWidget(name, row, 0);

    if (field) {
        grid->addWidget(field, row, 1);
    }

    if (!hint.isEmpty()) {
        auto* hintLabel = new QLabel(hint, this);
        hintLabel->setObjectName(QStringLiteral("hintLabel"));
        hintLabel->setWordWrap(true);
        // 与输入框左对齐，视觉上归属上方的控件而不是整行标签。
        grid->addWidget(hintLabel, row + 1, 1);
    }
}

QWidget* MindMapSettingsDialog::buildFontPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("文字"), &grid);

    m_fontCombo = new QFontComboBox(page);
    m_fontCombo->setEditable(false);
    // 字体下拉默认按"最长字体名"计算最小宽度（可能 300px 以上），会把卡片顶宽。
    // 改成按固定字数计算，卡片就能保持紧凑的尺寸。
    m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontCombo->setMinimumContentsLength(12);
    addFormRow(grid, 0, QStringLiteral("字体"), m_fontCombo);

    m_fontSize = new QSpinBox(page);
    m_fontSize->setRange(6, 96);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    addFormRow(grid, 1, QStringLiteral("字号"), m_fontSize);

    m_textColor = new ColorField(page);
    m_textColor->setDefaultColor(MindMapStyle{}.textColor);
    m_textColor->setFieldLabel(QStringLiteral("字体颜色"));
    addFormRow(grid, 2, QStringLiteral("字体颜色"), m_textColor);

    layout->addWidget(card);
    layout->addWidget(makePageNote(page));
    layout->addStretch(1);
    return page;
}

QWidget* MindMapSettingsDialog::buildParagraphPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("排版"), &grid);

    m_alignment = new SegmentedControl(page);
    m_alignment->addItem(QStringLiteral("左"), int(Qt::AlignLeft));
    m_alignment->addItem(QStringLiteral("居中"), int(Qt::AlignHCenter));
    m_alignment->addItem(QStringLiteral("右"), int(Qt::AlignRight));
    m_alignment->addItem(QStringLiteral("两端"), int(Qt::AlignJustify));
    addFormRow(grid, 0, QStringLiteral("对齐"), m_alignment);

    m_lineSpacing = new QDoubleSpinBox(page);
    m_lineSpacing->setRange(0.5, 4.0);
    m_lineSpacing->setSingleStep(0.1);
    m_lineSpacing->setDecimals(2);
    m_lineSpacing->setSuffix(QStringLiteral(" 倍"));
    addFormRow(grid, 1, QStringLiteral("行距"), m_lineSpacing);

    layout->addWidget(card);
    layout->addWidget(makePageNote(page));
    layout->addStretch(1);
    return page;
}

QWidget* MindMapSettingsDialog::buildNodePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("节点外观"), &grid);

    m_fillColor = new ColorField(page);
    m_fillColor->setDefaultColor(MindMapStyle{}.fillColor);
    m_fillColor->setFieldLabel(QStringLiteral("填充颜色"));
    addFormRow(grid, 0, QStringLiteral("填充颜色"), m_fillColor);

    auto* borderRow = new QWidget(page);
    auto* borderLayout = new QHBoxLayout(borderRow);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    borderLayout->setSpacing(8);
    m_borderColor = new ColorField(borderRow);
    m_borderColor->setDefaultColor(MindMapStyle{}.borderColor);
    m_borderColor->setFieldLabel(QStringLiteral("边框颜色"));
    borderLayout->addWidget(m_borderColor, 1);

    m_borderFollow = new QToolButton(borderRow);
    m_borderFollow->setObjectName(QStringLiteral("chipButton"));
    m_borderFollow->setText(QStringLiteral("跟随填充"));
    m_borderFollow->setCheckable(true);
    m_borderFollow->setCursor(Qt::PointingHandCursor);
    m_borderFollow->setToolTip(QStringLiteral("勾选后边框颜色始终与填充颜色一致"));
    borderLayout->addWidget(m_borderFollow, 0);
    addFormRow(grid, 1, QStringLiteral("边框颜色"), borderRow);

    m_borderWidth = new QSpinBox(page);
    m_borderWidth->setRange(1, 12);
    m_borderWidth->setSuffix(QStringLiteral(" px"));
    addFormRow(grid, 2, QStringLiteral("边框粗细"), m_borderWidth);

    m_shape = new SegmentedControl(page);
    m_shape->addItem(QStringLiteral("圆角矩形"), int(MindMapNodeShape::RoundedRect));
    m_shape->addItem(QStringLiteral("矩形"), int(MindMapNodeShape::Rect));
    addFormRow(grid, 3, QStringLiteral("形状"), m_shape);
    layout->addWidget(card);

    QGridLayout* layoutGrid = nullptr;
    QWidget* layoutCard = createGroupCard(QStringLiteral("文本布局"), &layoutGrid);
    m_textLineWidth = new QSpinBox(page);
    m_textLineWidth->setRange(54, 794);
    m_textLineWidth->setSuffix(QStringLiteral(" px"));
    m_textLineWidth->setValue(QSettings().value(QStringLiteral("textLineWidth"), 200).toInt());
    addFormRow(layoutGrid, 0, QStringLiteral("文本行宽度"), m_textLineWidth,
               QStringLiteral("节点文本超过该宽度后自动换行。"));
    layout->addWidget(layoutCard);

    layout->addWidget(makePageNote(page));
    layout->addStretch(1);
    return page;
}

QWidget* MindMapSettingsDialog::buildThemePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("外观主题"), &grid);

    auto* row = new QWidget(page);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);

    const qreal dpr = devicePixelRatioF();
    const QSize thumbSize(88, 54);
    auto createCard = [&](const QString& text, bool darkThumb, bool systemThumb,
                          const QString& mode, const QString& tip) {
        auto* button = new QToolButton(row);
        button->setObjectName(QStringLiteral("themeCard"));
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(QIcon(makeThemeThumbnail(thumbSize, darkThumb, systemThumb, dpr)));
        button->setIconSize(thumbSize);
        button->setFixedSize(112, 104);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("themeMode", mode);
        connect(button, &QToolButton::clicked, this, [this, mode] {
            setThemeMode(mode, true);
        });
        rowLayout->addWidget(button);
        return button;
    };

    m_themeLight = createCard(QStringLiteral("浅色"), false, false, QStringLiteral("light"),
                              QStringLiteral("始终使用浅色外观"));
    m_themeDark = createCard(QStringLiteral("深色"), true, false, QStringLiteral("dark"),
                             QStringLiteral("始终使用深色外观"));
    m_themeSystem = createCard(QStringLiteral("跟随系统"), false, true, QStringLiteral("system"),
                               QStringLiteral("随 Windows 的明暗设置自动切换"));
    rowLayout->addStretch(1);

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_themeLight);
    group->addButton(m_themeDark);
    group->addButton(m_themeSystem);

    row->setMaximumWidth(3 * 112 + 2 * 12 + 4);
    grid->addWidget(row, 0, 0, 1, 2);

    auto* hint = new QLabel(QStringLiteral("主题同时影响工具栏、画布与浮动编辑框；节点颜色由上面的样式决定，不随主题变化。"),
                            page);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(card);
    layout->addWidget(hint);
    layout->addStretch(1);

    syncThemeCards();
    return page;
}

QWidget* MindMapSettingsDialog::buildOperationPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("画布"), &grid);

    m_viewportMoveDuration = new QDoubleSpinBox(page);
    m_viewportMoveDuration->setRange(0.0, 0.5);
    m_viewportMoveDuration->setSingleStep(0.05);
    m_viewportMoveDuration->setDecimals(2);
    m_viewportMoveDuration->setSuffix(QStringLiteral(" 秒"));
    m_viewportMoveDuration->setValue(
        QSettings().value(QStringLiteral("smoothViewportDurationMs"), 200).toInt() / 1000.0);
    addFormRow(grid, 0, QStringLiteral("平滑视野移动"), m_viewportMoveDuration,
               QStringLiteral("0 表示关闭动画，视野立即跳转。"));

    layout->addWidget(card);
    layout->addStretch(1);
    return page;
}

QWidget* MindMapSettingsDialog::buildPreviewPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("previewPanel"));
    panel->setFixedWidth(kPreviewWidth);
    panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QFrame(panel);
    header->setObjectName(QStringLiteral("previewHeader"));
    header->setFixedHeight(34);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 0, 14, 0);
    headerLayout->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("预览"), header);
    title->setObjectName(QStringLiteral("previewTitle"));
    m_previewLevelLabel = new QLabel(header);
    m_previewLevelLabel->setObjectName(QStringLiteral("previewLevel"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_previewLevelLabel);

    m_preview = new StylePreviewWidget(panel);
    layout->addWidget(header);
    layout->addWidget(m_preview, 1);
    return panel;
}

QWidget* MindMapSettingsDialog::buildFooter() {
    auto* footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("footerBar"));
    footer->setFixedHeight(60);

    auto* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setSpacing(10);

    m_restoreButton = new QPushButton(QStringLiteral("恢复此层级默认值"), footer);
    m_restoreButton->setObjectName(QStringLiteral("ghostButton"));
    m_restoreButton->setCursor(Qt::PointingHandCursor);
    m_restoreButton->setToolTip(QStringLiteral("把当前层级的字体、颜色与边框恢复为内置默认值"));
    layout->addWidget(m_restoreButton);
    layout->addStretch(1);

    auto* cancelButton = new QPushButton(QStringLiteral("取消"), footer);
    m_applyButton = new QPushButton(QStringLiteral("应用"), footer);
    auto* okButton = new QPushButton(QStringLiteral("确定"), footer);
    okButton->setObjectName(QStringLiteral("primaryButton"));
    okButton->setDefault(true);
    for (auto* button : {cancelButton, m_applyButton, okButton}) {
        button->setCursor(Qt::PointingHandCursor);
    }
    layout->addWidget(cancelButton);
    layout->addWidget(m_applyButton);
    layout->addWidget(okButton);

    connect(cancelButton, &QPushButton::clicked, this, &MindMapSettingsDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, [this] { applyChanges(); });
    connect(okButton, &QPushButton::clicked, this, [this] {
        applyChanges();
        accept();
    });
    connect(m_restoreButton, &QPushButton::clicked, this, &MindMapSettingsDialog::restoreLevelDefaults);
    return footer;
}

void MindMapSettingsDialog::addNavItem(const QString& text, int pageIndex) {
    auto* item = new QListWidgetItem(text, m_nav);
    item->setData(Qt::UserRole, pageIndex);
    item->setSizeHint(QSize(0, 40));
}

// ============================================================
// 状态同步
// ============================================================

QString MindMapSettingsDialog::levelKey() const {
    if (!m_levelScope) return QStringLiteral("root");
    const QString key = m_levelScope->currentData().toString();
    return key.isEmpty() ? QStringLiteral("root") : key;
}

int MindMapSettingsDialog::levelIndex() const {
    const QString key = levelKey();
    if (key == QLatin1String("firstChild")) return 1;
    if (key == QLatin1String("deeper")) return 2;
    return 0;
}

MindMapStyle& MindMapSettingsDialog::currentStyle() {
    if (levelKey() == QLatin1String("firstChild")) return m_editStyles.firstChild;
    if (levelKey() == QLatin1String("deeper")) return m_editStyles.deeper;
    return m_editStyles.root;
}

const MindMapStyle& MindMapSettingsDialog::currentStyle() const {
    if (levelKey() == QLatin1String("firstChild")) return m_editStyles.firstChild;
    if (levelKey() == QLatin1String("deeper")) return m_editStyles.deeper;
    return m_editStyles.root;
}

void MindMapSettingsDialog::loadCurrentStyle() {
    if (!m_fontCombo) return;
    m_updating = true;
    const auto& style = currentStyle();

    QFont font(style.fontFamily);
    m_fontCombo->setCurrentFont(font);
    m_fontSize->setValue(style.fontPointSize);
    m_textColor->setColor(style.textColor);
    m_alignment->setCurrentData(int(style.alignment & Qt::AlignHorizontal_Mask));
    m_lineSpacing->setValue(style.lineSpacing);
    m_fillColor->setColor(style.fillColor);
    m_borderColor->setColor(style.effectiveBorderColor());
    m_borderFollow->setChecked(style.borderFollowsFill);
    m_borderColor->setEnabled(!style.borderFollowsFill);
    m_borderWidth->setValue(style.borderWidth);
    m_shape->setCurrentData(int(style.shape));

    m_updating = false;
    refreshPreview();
}

void MindMapSettingsDialog::updateStyleFromControls() {
    if (m_updating) return;
    auto& style = currentStyle();
    style.fontFamily = m_fontCombo->currentFont().family();
    style.fontPointSize = m_fontSize->value();
    style.alignment = Qt::Alignment(m_alignment->currentData().toInt());
    style.lineSpacing = m_lineSpacing->value();
    style.shape = MindMapNodeShape(m_shape->currentData().toInt());
    style.borderWidth = m_borderWidth->value();
    refreshPreview();
}

void MindMapSettingsDialog::refreshPreview() {
    if (!m_preview) return;
    m_preview->setStyles(m_editStyles);
    m_preview->setActiveLevel(levelIndex());
    if (m_previewLevelLabel) {
        m_previewLevelLabel->setText(levelDisplayName(levelIndex()));
    }
}

void MindMapSettingsDialog::refreshLevelChrome(int pageIndex) {
    static const char* titles[] = {"字体", "段落", "节点", "主题", "操作"};
    static const char* subtitles[] = {
        "设置该层级的文本格式。",
        "控制节点内文本的排版。",
        "设置节点的填充、边框与形状。",
        "选择应用的整体明暗外观。",
        "调整画布交互的动画行为。"};

    if (pageIndex >= 0 && pageIndex < 5) {
        m_pageTitle->setText(QString::fromUtf8(titles[pageIndex]));
        m_pageSubtitle->setText(QString::fromUtf8(subtitles[pageIndex]));
    }

    const bool stylePage = pageIndex >= 0 && pageIndex <= 2;
    m_levelScope->setVisible(stylePage);
    m_previewPanel->setVisible(stylePage);
    m_restoreButton->setVisible(stylePage);
    if (stylePage) refreshPreview();
}

void MindMapSettingsDialog::restoreLevelDefaults() {
    currentStyle() = MindMapStyle{};
    loadCurrentStyle();
}

void MindMapSettingsDialog::applyChanges() {
    QSettings settings;
    if (m_viewportMoveDuration) {
        const int durationMs = qRound(m_viewportMoveDuration->value() * 1000.0);
        if (settings.value(QStringLiteral("smoothViewportDurationMs"), 200).toInt() != durationMs) {
            settings.setValue(QStringLiteral("smoothViewportDurationMs"), durationMs);
        }
    }

    bool textLineWidthChanged = false;
    if (m_textLineWidth) {
        const int previousWidth = settings.value(QStringLiteral("textLineWidth"), 200).toInt();
        const int nextWidth = m_textLineWidth->value();
        if (previousWidth != nextWidth) {
            settings.setValue(QStringLiteral("textLineWidth"), nextWidth);
            textLineWidthChanged = true;
            if (m_root) m_root->applyTextLineWidthPreferenceToSubtree();
        }
    }

    commitThemeMode();
    m_appliedStyles = m_editStyles;

    if (!m_root) return;
    const auto before = m_originalStyles;
    m_root->applyLevelStyles(m_editStyles);
    m_originalStyles = m_editStyles;
    if (before.toJson() != m_editStyles.toJson()) {
        emit stylesApplied(before, m_editStyles);
    } else if (textLineWidthChanged) {
        emit layoutRefreshRequested();
    }
}

// ============================================================
// 主题
// ============================================================

bool MindMapSettingsDialog::effectiveDark() const {
    if (m_themeMode == QLatin1String("system")) {
        if (auto* hints = QGuiApplication::styleHints()) {
            return hints->colorScheme() == Qt::ColorScheme::Dark;
        }
        return false;
    }
    return m_themeMode == QLatin1String("dark");
}

void MindMapSettingsDialog::setThemeMode(const QString& mode, bool notify) {
    if (!isValidMode(mode)) return;
    if (m_themeMode == mode) {
        syncThemeCards();
        return;
    }
    m_themeMode = mode;

    QSettings settings;
    settings.setValue(themeModeKey(), mode);
    settings.setValue(darkThemeKey(), effectiveDark());

    applyThemeToDialog();
    syncThemeCards();
    if (notify) emit themeChanged(effectiveDark());
}

void MindMapSettingsDialog::commitThemeMode() {
    m_committedMode = m_themeMode;
    QSettings settings;
    settings.setValue(themeModeKey(), m_themeMode);
    settings.setValue(darkThemeKey(), effectiveDark());
}

void MindMapSettingsDialog::rollbackThemeMode() {
    if (m_themeMode == m_committedMode) return;
    m_themeMode = m_committedMode;

    QSettings settings;
    settings.setValue(themeModeKey(), m_themeMode);
    settings.setValue(darkThemeKey(), effectiveDark());

    applyThemeToDialog();
    syncThemeCards();
    emit themeChanged(effectiveDark());
}

void MindMapSettingsDialog::syncThemeCards() {
    if (!m_themeLight) return;
    const auto update = [this](QToolButton* button, bool checked) {
        if (!button) return;
        const bool blocked = button->blockSignals(true);
        button->setChecked(checked);
        button->blockSignals(blocked);
    };
    update(m_themeLight, m_themeMode == QLatin1String("light"));
    update(m_themeDark, m_themeMode == QLatin1String("dark"));
    update(m_themeSystem, m_themeMode == QLatin1String("system"));
}

void MindMapSettingsDialog::applyThemeToDialog() {
    const UiTheme::Tokens tokens = effectiveDark() ? UiTheme::dark() : UiTheme::light();
    setStyleSheet(UiTheme::styleSheet(tokens));

    if (m_preview) {
        m_preview->setAccentColor(tokens.accent);
        m_preview->setConnectorColor(tokens.controlBorder);
    }
    applyDarkTitleBar();
}

void MindMapSettingsDialog::applyDarkTitleBar() {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;
    const DWORD value = effectiveDark() ? 1 : 0;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_22H2, &value, sizeof(value));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
#endif
}

void MindMapSettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    applyDarkTitleBar();
}

// ============================================================
// 取消
// ============================================================

void MindMapSettingsDialog::reject() {
    // 主题是即时生效的：取消时必须把它退回到打开窗口时的状态。
    rollbackThemeMode();
    QDialog::reject();
}
