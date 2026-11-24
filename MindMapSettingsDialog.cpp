#include "MindMapSettingsDialog.h"

#include "MindMapNode.h"
#include "RainbowPaletteLibrary.h"
#include "SettingsWidgets.h"
#include "UiTheme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
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

    // 下拉框 / 数字框会自己吃掉滚轮改值（Qt 内置行为），装个过滤器把它们上面的
    // 滚轮转给所在页面，见 eventFilter()。挂在 qApp 上是因为事件落点常常是这些
    // 控件内部私有的 QLineEdit / 弹出按钮，逐个安装既不现实也容易漏。
    if (auto* application = QCoreApplication::instance()) {
        application->installEventFilter(this);
    }

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
    // 层级选择器不再放在页头：它只作用于节点/段落两页的样式字段，
    // 而节点页里彩虹分支这类内容与层级无关，摆在页头会把两者混成一件事。
    contentLayout->addLayout(headerLayout);

    // ---- 分页 + 预览 ----
    auto* columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);

    m_pages = new QStackedWidget(content);
    m_pages->addWidget(wrapPage(buildNodePage()));
    m_pages->addWidget(wrapPage(buildParagraphPage()));
    m_pages->addWidget(wrapPage(buildThemePage()));
    m_pages->addWidget(wrapPage(buildOperationPage()));
    columns->addWidget(m_pages, 1);

    m_previewPanel = buildPreviewPanel();
    columns->addWidget(m_previewPanel, 0);
    contentLayout->addLayout(columns, 1);

    bodyLayout->addWidget(content, 1);
    rootLayout->addWidget(body, 1);
    rootLayout->addWidget(buildFooter());

    const QStringList categories = {QStringLiteral("节点"), QStringLiteral("段落"),
                                    QStringLiteral("主题"), QStringLiteral("操作")};
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

    // ---- 层级切换（节点页与段落页各一份，互相同步）----
    connect(m_levelScope, &SegmentedControl::currentChanged, this, [this](int) {
        if (m_updating) return;
        loadCurrentStyle();
    });
    connect(m_levelScopeParagraph, &SegmentedControl::currentChanged, this, [this](int) {
        if (m_updating) return;
        // 段落页那份改动后，先把节点页那份对齐，再按新层级载入样式。
        // 同步期间用 m_updating 挡住节点页那份的回调，避免重复载入。
        m_updating = true;
        m_levelScope->setCurrentData(m_levelScopeParagraph->currentData());
        m_updating = false;
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
        if (m_editStyles.rainbow.enabled) {
            MindMapRainbowBranchStyle override = currentBranchOverride();
            override.hasText = true;
            override.textColor = color;
            writeBranchOverride(override);
            return;
        }
        currentStyle().textColor = color;
        refreshPreview();
    });
    connect(m_fillColor, &ColorField::colorChanged, this, [this](const QColor& color) {
        if (m_updating) return;
        if (m_editStyles.rainbow.enabled) {
            MindMapRainbowBranchStyle override = currentBranchOverride();
            override.hasFill = true;
            override.fillColor = color;
            // 勾着「跟随填充」时边框本来就跟过来了（边框 = 填充），
            // 所以顺手清掉手调过的边框色，避免两处各存一份打架。
            if (m_borderFollow->isChecked()) {
                override.hasBorder = false;
                override.borderColor = QColor();
            }
            writeBranchOverride(override);
            return;
        }
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
        if (m_editStyles.rainbow.enabled) {
            MindMapRainbowBranchStyle override = currentBranchOverride();
            override.hasBorder = true;
            override.borderColor = color;
            // 手动改边框等于放弃"跟随填充"。
            override.hasBorderFollowsFill = false;
            override.borderFollowsFill = false;
            writeBranchOverride(override);
            return;
        }
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
        if (m_editStyles.rainbow.enabled) {
            // 彩虹下"跟随填充"是这条分支自己的一个开关：勾上以后边框就等于该分支的填充色。
            // 它必须作为覆盖存下来，否则界面勾上了、画布上却仍然画派生边框色——
            // 表现出来就是"点了不变蓝、再点也取消不掉"。
            MindMapRainbowBranchStyle override = currentBranchOverride();
            override.hasBorderFollowsFill = checked;
            override.borderFollowsFill = checked;
            if (checked) {
                // 边框改由填充决定，之前手调过的边框色就没意义了，清掉避免两处打架。
                override.hasBorder = false;
                override.borderColor = QColor();
            }
            writeBranchOverride(override);
            return;
        }
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

QScrollArea* MindMapSettingsDialog::pageScrollFor(QWidget* widget) const {
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(current)) {
            if (area->objectName() == QLatin1String("pageScroll")) return area;
        }
        if (current == this) break;
    }
    return nullptr;
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

QWidget* MindMapSettingsDialog::buildNodePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // 彩虹分支是整页的"总开关"，放在最前面：它一旦打开，下面的填充/边框/字体颜色
    // 会由分支色派生，先让用户看到这条因果，再去调外观就不会困惑。
    layout->addWidget(buildRainbowCard(page));

    // 层级按钮紧跟彩虹卡片下方：节点层级管不到分支配色，摆在页头或页尾都会
    // 让人误以为它也在管彩虹；贴在彩虹卡片下面，"下面那些字段按层级编辑"最直观。
    auto* levelRow = new QWidget(page);
    auto* levelRowLayout = new QHBoxLayout(levelRow);
    levelRowLayout->setContentsMargins(0, 0, 0, 0);
    levelRowLayout->setSpacing(8);
    auto* levelLabel = new QLabel(QStringLiteral("编辑层级"), levelRow);
    levelLabel->setObjectName(QStringLiteral("hintLabel"));
    levelRowLayout->addWidget(levelLabel, 0);
    levelRowLayout->addWidget(m_levelScope, 0);
    levelRowLayout->addStretch(1);
    layout->addWidget(levelRow);

    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("节点外观"), &grid);

    m_fillColor = new ColorField(page);
    m_fillColor->setDefaultColor(MindMapStyle{}.fillColor);
    m_fillColor->setFieldLabel(QStringLiteral("填充颜色"));

    m_borderColor = new ColorField(page);
    m_borderColor->setDefaultColor(MindMapStyle{}.borderColor);
    m_borderColor->setFieldLabel(QStringLiteral("边框颜色"));

    m_borderFollow = new QToolButton(page);
    m_borderFollow->setObjectName(QStringLiteral("chipButton"));
    m_borderFollow->setText(QStringLiteral("跟随填充"));
    m_borderFollow->setCheckable(true);
    m_borderFollow->setCursor(Qt::PointingHandCursor);
    m_borderFollow->setToolTip(QStringLiteral("勾选后边框颜色始终与填充颜色一致"));

    // 填充 + 边框 + 跟随填充装进同一个容器：彩虹开启时整块搬进彩虹卡片，
    // 关闭时搬回这里。搬的是同一批控件，状态不会分叉。
    m_nodeColorBox = new QWidget(page);
    auto* nodeColorGrid = new QGridLayout(m_nodeColorBox);
    nodeColorGrid->setContentsMargins(0, 0, 0, 0);
    nodeColorGrid->setHorizontalSpacing(14);
    nodeColorGrid->setVerticalSpacing(12);
    // 让输入列吃掉多余宽度；否则 QGridLayout 会把空白平均分给标签列，
    // 标签被推到卡片中间、字段被顶出右边。
    nodeColorGrid->setColumnStretch(1, 1);
    addFormRow(nodeColorGrid, 0, QStringLiteral("填充颜色"), m_fillColor);
    // 「跟随填充」放在边框颜色下面一行，而不是挤在同一行：
    // 彩虹卡片的内宽只有约 310 px，同一行放"色值 + 重置 + 跟随填充"会把颜色字段压到最小宽度，
    // 色值和重置按钮贴在一起看不出是一组。
    addFormRow(nodeColorGrid, 1, QStringLiteral("边框颜色"), m_borderColor);
    // 「跟随填充」单独占一行、靠字段列左对齐：彩虹卡片的内宽只有约 310 px，
    // 和色值挤同一行会被裁掉；右对齐又会和上面的「重置」上下错位。
    nodeColorGrid->addWidget(m_borderFollow, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);

    m_nodeColorSlot = new QWidget(page);
    auto* nodeColorSlotLayout = new QVBoxLayout(m_nodeColorSlot);
    nodeColorSlotLayout->setContentsMargins(0, 0, 0, 0);
    nodeColorSlotLayout->setSpacing(0);
    nodeColorSlotLayout->addWidget(m_nodeColorBox);
    grid->addWidget(m_nodeColorSlot, 0, 0, 1, 2);

    m_borderWidth = new QSpinBox(page);
    m_borderWidth->setRange(1, 12);
    m_borderWidth->setSuffix(QStringLiteral(" px"));
    addFormRow(grid, 1, QStringLiteral("边框粗细"), m_borderWidth);

    m_shape = new SegmentedControl(page);
    m_shape->addItem(QStringLiteral("圆角矩形"), int(MindMapNodeShape::RoundedRect));
    m_shape->addItem(QStringLiteral("矩形"), int(MindMapNodeShape::Rect));
    addFormRow(grid, 2, QStringLiteral("形状"), m_shape);
    layout->addWidget(card);

    // 原来独立的「字体」页并进这里：文字格式同样是"这一层节点长什么样"的一部分。
    QGridLayout* textGrid = nullptr;
    QWidget* textCard = createGroupCard(QStringLiteral("文字"), &textGrid);

    m_fontCombo = new QFontComboBox(page);
    m_fontCombo->setEditable(false);
    // 字体下拉默认按"最长字体名"计算最小宽度（可能 300px 以上），会把卡片顶宽。
    // 改成按固定字数计算，卡片就能保持紧凑的尺寸。
    m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontCombo->setMinimumContentsLength(12);
    addFormRow(textGrid, 0, QStringLiteral("字体"), m_fontCombo);

    m_fontSize = new QSpinBox(page);
    m_fontSize->setRange(6, 96);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    addFormRow(textGrid, 1, QStringLiteral("字号"), m_fontSize);

    m_textColor = new ColorField(page);
    m_textColor->setDefaultColor(MindMapStyle{}.textColor);
    m_textColor->setFieldLabel(QStringLiteral("字体颜色"));

    // 字体颜色同样要能搬进彩虹卡片，所以也装进独立容器。
    m_textColorBox = new QWidget(page);
    auto* textColorGrid = new QGridLayout(m_textColorBox);
    textColorGrid->setContentsMargins(0, 0, 0, 0);
    textColorGrid->setHorizontalSpacing(14);
    textColorGrid->setVerticalSpacing(12);
    textColorGrid->setColumnStretch(1, 1);
    addFormRow(textColorGrid, 0, QStringLiteral("字体颜色"), m_textColor);

    m_textColorSlot = new QWidget(page);
    auto* textColorSlotLayout = new QVBoxLayout(m_textColorSlot);
    textColorSlotLayout->setContentsMargins(0, 0, 0, 0);
    textColorSlotLayout->setSpacing(0);
    textColorSlotLayout->addWidget(m_textColorBox);
    textGrid->addWidget(m_textColorSlot, 2, 0, 1, 2);
    layout->addWidget(textCard);

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

    // 段落页也放一份层级选择器（与节点页那份互相同步）。
    // 两页本来就是按层级编辑同一套样式，来回切换时总能在手边找到它。
    m_levelScopeParagraph = new SegmentedControl(page);
    m_levelScopeParagraph->addItem(QStringLiteral("根节点"), QStringLiteral("root"));
    m_levelScopeParagraph->addItem(QStringLiteral("1 级"), QStringLiteral("firstChild"));
    m_levelScopeParagraph->addItem(QStringLiteral("2 级+"), QStringLiteral("deeper"));
    auto* levelRow = new QWidget(page);
    auto* levelRowLayout = new QHBoxLayout(levelRow);
    levelRowLayout->setContentsMargins(0, 0, 0, 0);
    levelRowLayout->setSpacing(8);
    auto* levelLabel = new QLabel(QStringLiteral("编辑层级"), levelRow);
    levelLabel->setObjectName(QStringLiteral("hintLabel"));
    levelRowLayout->addWidget(levelLabel, 0);
    levelRowLayout->addWidget(m_levelScopeParagraph, 0);
    levelRowLayout->addStretch(1);
    layout->addWidget(levelRow);

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

    // 「文本布局」从节点页挪过来：它管的是文本在节点里的排布（换行宽度），
    // 和上面的对齐/行距是同一类事。
    QGridLayout* layoutGrid = nullptr;
    QWidget* layoutCard = createGroupCard(QStringLiteral("文本布局"), &layoutGrid);
    m_textLineWidth = new QSpinBox(page);
    m_textLineWidth->setRange(54, 794);
    m_textLineWidth->setSuffix(QStringLiteral(" px"));
    m_textLineWidth->setValue(QSettings().value(QStringLiteral("textLineWidth"), kDefaultTextLineWidth).toInt());
    addFormRow(layoutGrid, 0, QStringLiteral("文本行宽度"), m_textLineWidth,
               QStringLiteral("节点文本超过该宽度后自动换行。"));
    layout->addWidget(layoutCard);

    layout->addWidget(makePageNote(page));
    layout->addStretch(1);
    return page;
}

// 彩虹分支卡片：开关 + 六色色板。
// 开启后画布按“1 级分支序号”派色，这里的六色就是那个循环周期；
// 关闭时六色仍然保留，重新打开无需重新调色。
QWidget* MindMapSettingsDialog::buildRainbowCard(QWidget* page) {
    QGridLayout* grid = nullptr;
    QWidget* card = createGroupCard(QStringLiteral("彩虹分支"), &grid);

    auto* header = new QWidget(page);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    // 开关用自绘的 ToggleSwitch，比勾选式 chip 更像"总开关"，也更容易看出开/关状态。
    m_rainbowToggle = new ToggleSwitch(header);
    m_rainbowToggle->setObjectName(QStringLiteral("rainbowToggle"));
    m_rainbowToggle->setToolTip(
        QStringLiteral("按 1 级子节点的先后顺序循环取色，每个分支的后续节点沿用同一色系"));
    headerLayout->addWidget(m_rainbowToggle, 0);
    headerLayout->addStretch(1);
    // 行标签写「启用彩虹分支」：原来这行左侧是「彩虹分支」、开关按钮自己带「启用彩虹分支」六个字，
    // 现在开关只剩一个 O= 图形，那六个字就挪到标签位置，避免和卡片标题重复。
    addFormRow(grid, 0, QStringLiteral("启用彩虹分支"), header);

    // ---- 六色色板（开关关闭时整体隐藏，但颜色值保留）----
    // 这块内容直接铺满整张卡片，不再塞进表单网格的字段列：字段列只有 250 px 左右，
    // 预设下拉、六个色块和说明文字挤在里面会互相压住。
    auto* body = new QWidget(card);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(8);
    card->layout()->addWidget(body);

    // ---- 预设配色：一次换掉整套六色；逐格改色后会自动落到「自定义」----
    // 十四套配色塞不进一行分段控件（字段区只有 250 px 左右），这里用下拉框，
    // 每一项带一条六色小色带，选之前就能看出整套观感（图标在 rebuildRainbowPresetItems 里画）。

    m_rainbowPreset = new QComboBox(body);
    // 图标按颜色现画，所以列表重建时再填（见 rebuildRainbowPresetItems）。
    m_rainbowPreset->setToolTip(
        QStringLiteral("整套换掉六色；也可以逐格改色，改完这一栏会显示为「自定义」"));
    bodyLayout->addWidget(m_rainbowPreset);

    // 存/删按钮：保存只在「自定义」出现，删除只在选中的是用户自己存的色卡时出现。
    auto* paletteButtonRow = new QWidget(body);
    auto* paletteButtonLayout = new QHBoxLayout(paletteButtonRow);
    paletteButtonLayout->setContentsMargins(0, 0, 0, 0);
    paletteButtonLayout->setSpacing(8);
    m_rainbowSaveButton = new QPushButton(QStringLiteral("保存色卡"), paletteButtonRow);
    m_rainbowSaveButton->setObjectName(QStringLiteral("ghostButton"));
    m_rainbowSaveButton->setCursor(Qt::PointingHandCursor);
    m_rainbowSaveButton->setToolTip(QStringLiteral("给当前这套颜色起个名字，之后会一直出现在下拉列表里"));
    paletteButtonLayout->addWidget(m_rainbowSaveButton, 0);
    m_rainbowDeleteButton = new QPushButton(QStringLiteral("删除色卡"), paletteButtonRow);
    m_rainbowDeleteButton->setObjectName(QStringLiteral("ghostButton"));
    m_rainbowDeleteButton->setCursor(Qt::PointingHandCursor);
    m_rainbowDeleteButton->setToolTip(QStringLiteral("把当前这套自定义色卡从列表里删掉"));
    paletteButtonLayout->addWidget(m_rainbowDeleteButton, 0);
    paletteButtonLayout->addStretch(1);
    bodyLayout->addWidget(paletteButtonRow);

    connect(m_rainbowSaveButton, &QPushButton::clicked, this, [this] {
        if (m_updating) return;
        saveCurrentPalette();
    });
    connect(m_rainbowDeleteButton, &QPushButton::clicked, this, [this] {
        if (m_updating) return;
        deleteCurrentPalette();
    });

    connect(m_rainbowPreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating) return;
        const QString name = m_rainbowPreset->itemData(index).toString();

        // 「自定义」只是状态展示，选它不改变任何颜色。
        if (name == QLatin1String("__custom__")) return;
        if (name.isEmpty()) return;

        // 离开「自定义」且改过东西，先问要不要存。
        if (m_customDirty && !confirmLeaveCustomPalette()) {
            refreshRainbowPreset();
            return;
        }

        const MindMapRainbowPalette palette = RainbowPaletteLibrary::findPalette(name);
        if (!palette.isValid()) {
            refreshRainbowPreset();
            return;
        }
        m_editStyles.rainbow.colors = palette.colors;
        m_editStyles.rainbow.paletteName = palette.name;
        // 色卡自带的分支覆盖一起套用：用户存色卡时改过的三色要跟着回来。
        m_editStyles.rainbow.branchStyles.clear();
        if (!palette.branchStyles.isEmpty()) {
            m_editStyles.rainbow.branchStyles = palette.branchStyles;
        }
        m_customDirty = false;
        refreshRainbowSwatches();
        refreshRainbowPaletteButtons();
        if (m_editStyles.rainbow.enabled) refreshRainbowStyleFields();
        refreshPreview();
    });

    auto* swatchRow = new QWidget(body);
    auto* swatchLayout = new QHBoxLayout(swatchRow);
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    swatchLayout->setSpacing(8);

    m_rainbowSwatches.clear();
    for (int i = 0; i < kRainbowBranchPeriod; ++i) {
        auto* swatch = new QToolButton(swatchRow);
        swatch->setObjectName(QStringLiteral("rainbowSwatch"));
        swatch->setFixedSize(30, 30);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(QStringLiteral("第 %1 个分支的颜色").arg(i + 1));
        swatch->setAccessibleName(QStringLiteral("第 %1 个分支的颜色").arg(i + 1));
        connect(swatch, &QToolButton::clicked, this, [this, i] {
            if (m_updating) return;
            // 单击只是"选中这个分支"，改色交给下面三个颜色字段，
            // 这样"选哪个分支"和"调什么颜色"是两件分开的事，不会一点就弹取色器。
            m_rainbowSelectedBranch = i;
            refreshRainbowSwatches();
            refreshRainbowStyleFields();
            refreshPreview();
        });
        swatchLayout->addWidget(swatch, 0);
        m_rainbowSwatches.append(swatch);
    }
    swatchLayout->addStretch(1);
    bodyLayout->addWidget(swatchRow);

    m_rainbowHint = new QLabel(body);
    m_rainbowHint->setObjectName(QStringLiteral("hintLabel"));
    m_rainbowHint->setWordWrap(true);
    bodyLayout->addWidget(m_rainbowHint);

    // 三个颜色角色在彩虹开启时会搬进这里（关闭时空出来）。
    // 这个槽位挂在卡片外层而不是 body 里：body 落在表单网格的字段列（约 250 px），
    // 而搬进来的三行各自还有 78 px 的标签列，挤在字段列里会把「跟随填充」和色值截断。
    m_rainbowStyleSlot = new QWidget(card);
    auto* rainbowStyleSlotLayout = new QVBoxLayout(m_rainbowStyleSlot);
    rainbowStyleSlotLayout->setContentsMargins(0, 0, 0, 0);
    // 间距与表单行距一致：填充/边框是一组，字体颜色是另一组，不留缝会粘成一坨。
    rainbowStyleSlotLayout->setSpacing(12);
    card->layout()->addWidget(m_rainbowStyleSlot);

    m_rainbowBody = body;

    connect(m_rainbowToggle, &QAbstractButton::toggled, this, [this](bool checked) {
        if (m_updating) return;
        setRainbowEnabled(checked);
    });

    // 卡片由 buildNodePage() 挂到页面布局上。
    return card;
}

void MindMapSettingsDialog::setRainbowEnabled(bool enabled) {
    auto& rainbow = m_editStyles.rainbow;
    rainbow.enabled = enabled;
    if (rainbow.colors.size() != kRainbowBranchPeriod) {
        rainbow.colors = MindMapRainbowStyle::defaultColors();
        rainbow.paletteName = QStringLiteral("宝石");
        // 配色都不成立了，挂在旧六色上的分支覆盖也没有意义。
        rainbow.branchStyles.clear();
    }
    refreshRainbowChrome();
    refreshPreview();
}

void MindMapSettingsDialog::refreshRainbowChrome() {
    if (!m_rainbowToggle) return;
    const bool enabled = m_editStyles.rainbow.enabled;

    // setCheckedInstant 不做动画，避免打开对话框时圆点从一端滑到另一端。
    m_rainbowToggle->setCheckedInstant(enabled);

    // 关闭时整块折叠：预设、色板、提示都收起来，卡片高度随之收缩。
    if (m_rainbowBody) m_rainbowBody->setVisible(enabled);

    // 三个颜色角色跟着开关搬家。彩虹开启时它们编辑的是"选中分支"的派生色，
    // 因此必须保持可编辑——灰掉会让人以为改不了。
    moveColorRolesIntoRainbow(enabled);
    if (m_fillColor) m_fillColor->setEnabled(true);
    if (m_borderColor) {
        // 彩虹下"跟随填充"是分支自己的开关，判断依据必须是派生后的样式。
        const bool followsFill =
            enabled ? m_editStyles.rainbow
                          .styleFor(currentStyle(), m_rainbowSelectedBranch, levelIndex())
                          .borderFollowsFill
                    : currentStyle().borderFollowsFill;
        m_borderColor->setEnabled(!followsFill);
    }
    if (m_textColor) m_textColor->setEnabled(true);
    if (m_borderFollow) m_borderFollow->setEnabled(true);

    if (m_rainbowHint) {
        m_rainbowHint->setText(
            enabled ? QStringLiteral("点色块选中分支，再用上面的颜色字段调它的填充、边框与字体颜色。")
                    : QStringLiteral("开启后按 1 级子节点的顺序循环取色。关闭时保留上面的层级颜色。"));
    }

    refreshRainbowSwatches();
    refreshRainbowPreset();
    if (enabled) {
        refreshRainbowStyleFields();
    } else if (m_fillColor && m_borderColor && m_textColor) {
        // 搬家只换了父容器，字段里还留着刚才那个分支的派生色。关掉彩虹后必须
        // 重新载入层级样式，否则三个框显示的是分支色、实际生效的却是层级色。
        // 这里不调用 loadCurrentStyle()：它内部会再调本函数，形成递归。
        const MindMapStyle& style = currentStyle();
        const bool previousUpdating = m_updating;
        m_updating = true;
        m_fillColor->setColor(style.fillColor);
        m_borderColor->setColor(style.effectiveBorderColor());
        m_textColor->setColor(style.textColor);
        m_borderFollow->setChecked(style.borderFollowsFill);
        m_updating = previousUpdating;
    }
}

void MindMapSettingsDialog::moveColorRolesIntoRainbow(bool intoRainbow) {
    if (!m_nodeColorBox || !m_textColorBox) return;

    // 两个容器同一时刻只属于一个槽位；先把它们从旧布局摘下，再挂到新槽位。
    auto detach = [this](QWidget* box) {
        for (QWidget* slot : {m_nodeColorSlot, m_textColorSlot, m_rainbowStyleSlot}) {
            if (!slot) continue;
            if (auto* layout = qobject_cast<QVBoxLayout*>(slot->layout())) {
                layout->removeWidget(box);
            }
        }
    };
    detach(m_nodeColorBox);
    detach(m_textColorBox);

    auto attach = [](QWidget* slot, QWidget* box) {
        if (!slot) return;
        if (auto* layout = qobject_cast<QVBoxLayout*>(slot->layout())) {
            if (layout->indexOf(box) < 0) layout->addWidget(box);
        }
    };

    if (intoRainbow && m_rainbowStyleSlot) {
        attach(m_rainbowStyleSlot, m_nodeColorBox);
        attach(m_rainbowStyleSlot, m_textColorBox);
    } else {
        attach(m_nodeColorSlot, m_nodeColorBox);
        attach(m_textColorSlot, m_textColorBox);
    }
}

void MindMapSettingsDialog::refreshRainbowStyleFields() {
    if (!m_editStyles.rainbow.enabled) return;
    if (!m_fillColor || !m_borderColor || !m_textColor) return;

    // 预览与画布用同一套派生规则，这里也照抄一遍，保证字段显示的颜色就是画布上的颜色。
    const MindMapStyle base = currentStyle();
    const MindMapStyle derived =
        m_editStyles.rainbow.styleFor(base, m_rainbowSelectedBranch, levelIndex());

    // 回填必须挡住信号：m_borderFollow 是 QToolButton，setChecked 会 emit toggled，
    // 那个槽在彩虹下会写一条分支覆盖并把这套配色打成「自定义」——
    // 结果就是"一开开关就冒出『保存色卡』按钮"。
    const bool previousUpdating = m_updating;
    m_updating = true;
    m_fillColor->setColor(derived.fillColor);
    m_borderColor->setColor(derived.effectiveBorderColor());
    m_textColor->setColor(derived.textColor);
    m_borderFollow->setChecked(derived.borderFollowsFill);
    m_updating = previousUpdating;

    m_borderColor->setEnabled(!derived.borderFollowsFill);
}

MindMapRainbowBranchStyle MindMapSettingsDialog::currentBranchOverride() const {
    return m_editStyles.rainbow.branchStyle(m_rainbowSelectedBranch, levelIndex());
}

void MindMapSettingsDialog::writeBranchOverride(const MindMapRainbowBranchStyle& style) {
    m_editStyles.rainbow.setBranchStyle(m_rainbowSelectedBranch, levelIndex(), style);

    // 动过任何一个角色，这套配色就不再等于某个预设了，显示为「自定义」。
    if (m_editStyles.rainbow.paletteName != QStringLiteral("自定义")) {
        m_editStyles.rainbow.paletteName = QStringLiteral("自定义");
    }
    // 记一笔"有未保存改动"，离开「自定义」时用它决定要不要问一句。
    m_customDirty = true;

    refreshRainbowSwatches();
    refreshRainbowPreset();
    refreshRainbowStyleFields();
    refreshPreview();
}

void MindMapSettingsDialog::refreshRainbowPreset() {
    if (!m_rainbowPreset) return;
    // 每次刷新都重建列表：用户可能刚存了新色卡或删了一套，条数会变。
    rebuildRainbowPresetItems();
    refreshRainbowPaletteButtons();
}

void MindMapSettingsDialog::rebuildRainbowPresetItems() {
    if (!m_rainbowPreset) return;

    // 六色小色带图标：宽 = 7px * 6 + 2px 边框，选之前就能看出整套观感。
    auto paletteIcon = [](const QVector<QColor>& colors) {
        const int stripe = 7;
        const int width = stripe * qMax(1, colors.size()) + 2;
        const int height = 14;
        QPixmap pixmap(width, height);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(QRectF(0.5, 0.5, width - 1, height - 1), Qt::white);
        for (int i = 0; i < colors.size(); ++i) {
            painter.fillRect(QRectF(1 + i * stripe, 1, stripe, height - 2), colors.at(i));
        }
        painter.setPen(QPen(QColor(15, 23, 42, 45)));
        painter.drawRect(QRectF(0.5, 0.5, width - 1, height - 1));
        return QIcon(pixmap);
    };

    const QSignalBlocker blocker(m_rainbowPreset);
    m_rainbowPreset->clear();
    for (const auto& preset : MindMapRainbowStyle::presets()) {
        m_rainbowPreset->addItem(paletteIcon(preset.colors), preset.name, preset.name);
    }
    // 用户存的色卡排在预设后面、用分隔线隔开，一眼能看出哪些是自己做的。
    const auto saved = RainbowPaletteLibrary::savedPalettes();
    if (!saved.isEmpty()) {
        m_rainbowPreset->insertSeparator(m_rainbowPreset->count());
        for (const auto& palette : saved) {
            m_rainbowPreset->addItem(paletteIcon(palette.colors), palette.name, palette.name);
        }
    }
    m_rainbowPreset->addItem(QIcon(), QStringLiteral("自定义"), QStringLiteral("__custom__"));

    // 当前配色若正好等于某一项（预设或用户存过的），就停在那一项上；否则停在「自定义」。
    int index = -1;
    const QString& currentName = m_editStyles.rainbow.paletteName;
    // 名字是「自定义」就一律停在「自定义」项上：微调的是分支三色而非六色基础色，
    // 六色可能仍与某套预设完全相同，此时不能再按颜色匹配回预设。
    if (currentName == QStringLiteral("自定义")) {
        index = m_rainbowPreset->count() - 1;
    }
    for (int i = 0; index < 0 && i < m_rainbowPreset->count(); ++i) {
        if (m_rainbowPreset->itemData(i).toString() == currentName
            && currentName != QLatin1String("__custom__")) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        // 没有同名项时，再看颜色是否和某个预设一致（例如老存档只存了颜色没存名字）。
        const int presetIndex = m_editStyles.rainbow.presetIndex();
        if (presetIndex >= 0 && presetIndex < m_rainbowPreset->count()) {
            index = presetIndex;
        }
    }
    if (index < 0) index = m_rainbowPreset->count() - 1;
    m_rainbowPreset->setCurrentIndex(index);
}

void MindMapSettingsDialog::refreshRainbowPaletteButtons() {
    const QString& name = m_editStyles.rainbow.paletteName;
    const bool isCustom = (name == QStringLiteral("自定义"))
        || (m_editStyles.rainbow.presetIndex() < 0 && !RainbowPaletteLibrary::findPalette(name).isValid());

    // 只有「自定义」才需要保存动作：预设与已存色卡都已经在库里了。
    if (m_rainbowSaveButton) {
        m_rainbowSaveButton->setVisible(isCustom);
        m_rainbowSaveButton->setEnabled(isCustom);
    }

    // 删除只对"用户自己存过的色卡"开放，内置预设与「自定义」都没有可删的东西。
    if (m_rainbowDeleteButton) {
        const bool deletable = !isCustom && RainbowPaletteLibrary::findPalette(name).isValid()
            && !RainbowPaletteLibrary::findPalette(name).builtIn;
        m_rainbowDeleteButton->setVisible(deletable);
        m_rainbowDeleteButton->setEnabled(deletable);
    }
}

bool MindMapSettingsDialog::saveCurrentPalette() {
    const QString suggested = m_editStyles.rainbow.paletteName == QStringLiteral("自定义")
        ? QString()
        : m_editStyles.rainbow.paletteName;

    QString name;
    while (true) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("保存色卡"),
                                    QStringLiteral("给这套颜色起个名字："), QLineEdit::Normal,
                                    suggested, &ok)
                   .trimmed();
        if (!ok) return false;
        if (name.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("保存色卡"),
                                     QStringLiteral("名字不能为空。"));
            continue;
        }
        if (RainbowPaletteLibrary::nameExists(name)) {
            QMessageBox::warning(this, QStringLiteral("保存色卡"),
                                 QStringLiteral("已经有叫「%1」的色卡了，请换一个名字。").arg(name));
            continue;
        }
        break;
    }

    MindMapRainbowPalette palette;
    palette.name = name;
    palette.colors = m_editStyles.rainbow.colors;
    palette.builtIn = false;
    // 分支三色覆盖一起存进去，否则重新选中这套色卡时微调过的颜色会丢。
    palette.branchStyles = m_editStyles.rainbow.branchStyles;
    if (!RainbowPaletteLibrary::savePalette(palette)) {
        QMessageBox::warning(this, QStringLiteral("保存色卡"),
                             QStringLiteral("保存失败，请换一个名字再试。"));
        return false;
    }

    m_editStyles.rainbow.paletteName = name;
    m_customDirty = false;
    refreshRainbowPreset();
    refreshPreview();
    return true;
}

void MindMapSettingsDialog::deleteCurrentPalette() {
    const QString name = m_editStyles.rainbow.paletteName;
    const MindMapRainbowPalette palette = RainbowPaletteLibrary::findPalette(name);
    if (!palette.isValid() || palette.builtIn) return;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除色卡"),
        QStringLiteral("是否确认删除当前自定义色卡？\n「%1」删掉后无法恢复。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    RainbowPaletteLibrary::removePalette(name);

    // 删掉之后这套颜色就没有名字了，退回「自定义」，六色本身保留在界面上。
    m_editStyles.rainbow.paletteName = QStringLiteral("自定义");
    m_customDirty = true;
    refreshRainbowPreset();
    refreshPreview();
}

bool MindMapSettingsDialog::confirmLeaveCustomPalette() {
    if (!m_customDirty) return true;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("保存色卡"),
        QStringLiteral("当前这套自定义颜色还没有保存，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Yes) {
        // 保存失败（用户取消命名、重名）就当没保存，仍然允许切换。
        saveCurrentPalette();
    }
    m_customDirty = false;
    return true;
}

void MindMapSettingsDialog::refreshRainbowSwatches() {
    if (m_rainbowSwatches.isEmpty()) return;
    const auto colors = m_editStyles.rainbow.colors;
    const QString accent = m_accentColor.name(QColor::HexRgb);
    for (int i = 0; i < m_rainbowSwatches.size(); ++i) {
        auto* swatch = m_rainbowSwatches.at(i);
        if (!swatch) continue;
        const QColor color = i < colors.size() ? colors.at(i) : QColor();
        if (!color.isValid()) {
            swatch->setStyleSheet(QString());
            continue;
        }
        // 选中的色块描一圈 accent：色块本身颜色饱和，仅靠亮度差看不出选中。
        const bool selected = i == m_rainbowSelectedBranch;
        const QString border = selected
                                   ? QStringLiteral("2px solid %1").arg(accent)
                                   : QStringLiteral("1px solid rgba(15, 23, 42, 45)");
        swatch->setStyleSheet(
            QStringLiteral("QToolButton#rainbowSwatch { background: %1; border: %2;"
                           " border-radius: 6px; }"
                           "QToolButton#rainbowSwatch:hover { border-color: %3; }")
                .arg(color.name(QColor::HexRgb), border, QStringLiteral("#64748b")));
    }
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

    // 小地图：显示方式决定它是否接收点击/拖拽传送。
    QGridLayout* miniMapGrid = nullptr;
    QWidget* miniMapCard = createGroupCard(QStringLiteral("小地图"), &miniMapGrid);

    m_miniMapMode = new SegmentedControl(page);
    m_miniMapMode->addItem(QStringLiteral("常亮"), QStringLiteral("always"));
    m_miniMapMode->addItem(QStringLiteral("视野移动时出现"), QStringLiteral("onMove"));
    m_miniMapMode->addItem(QStringLiteral("关闭"), QStringLiteral("off"));
    m_miniMapMode->setCurrentData(
        QSettings().value(QStringLiteral("minimapMode"), QStringLiteral("onMove")).toString());
    addFormRow(miniMapGrid, 0, QStringLiteral("显示方式"), m_miniMapMode,
               QStringLiteral("左键单击小地图可把视野传送到该处，按住拖动则跟随指针。"
                              "常亮：始终显示，随时可用；"
                              "视野移动时出现：滚动或缩放后短暂浮现；"
                              "关闭：不显示小地图。"));

    layout->addWidget(miniMapCard);
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

    // 两份层级选择器始终显示同一层级，避免用户在两页看到不同状态。
    if (m_levelScopeParagraph) {
        m_levelScopeParagraph->setCurrentData(levelKey());
    }

    const auto& style = currentStyle();

    // 颜色选择器里的“默认色”跟随当前层级的默认样式，切换层级时保持一致。
    const MindMapStyle levelDefault = MindMapLevelStyles::defaultFor(levelKey());
    m_textColor->setDefaultColor(levelDefault.textColor);
    m_fillColor->setDefaultColor(levelDefault.fillColor);
    m_borderColor->setDefaultColor(levelDefault.effectiveBorderColor());

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

    refreshRainbowChrome();

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
    const bool rainbow = m_editStyles.rainbow.enabled;
    m_preview->setRainbowPreview(rainbow, m_editStyles.rainbow.colors);
    m_preview->setRainbowSelectedBranch(m_rainbowSelectedBranch);
    if (m_previewLevelLabel) {
        m_previewLevelLabel->setText(rainbow ? QStringLiteral("彩虹分支")
                                             : levelDisplayName(levelIndex()));
    }
}

void MindMapSettingsDialog::refreshLevelChrome(int pageIndex) {
    static const char* titles[] = {"节点", "段落", "主题", "操作"};
    static const char* subtitles[] = {
        "设置节点的外观、文字与分支配色。",
        "控制节点内文本的排版方式。",
        "选择应用的整体明暗外观。",
        "调整画布交互与小地图的行为。"};

    if (pageIndex >= 0 && pageIndex < 4) {
        m_pageTitle->setText(QString::fromUtf8(titles[pageIndex]));
        m_pageSubtitle->setText(QString::fromUtf8(subtitles[pageIndex]));
    }

    // 前两页（节点 / 段落）都是按层级设置样式，共用右侧预览。
    // 层级选择器已经搬进这两页的内容区，不再由这里控制可见性。
    const bool stylePage = pageIndex >= 0 && pageIndex <= 1;
    m_previewPanel->setVisible(stylePage);
    m_restoreButton->setVisible(stylePage);
    if (stylePage) refreshPreview();
}

void MindMapSettingsDialog::restoreLevelDefaults() {
    currentStyle() = MindMapLevelStyles::defaultFor(levelKey());
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

    // 显示方式必须即时生效，所以与时长不同：变化时发信号让主窗口重新应用。
    if (m_miniMapMode) {
        const QString nextMode = m_miniMapMode->currentData().toString();
        if (settings.value(QStringLiteral("minimapMode"), QStringLiteral("onMove")).toString()
            != nextMode) {
            settings.setValue(QStringLiteral("minimapMode"), nextMode);
            emit miniMapModeChanged();
        }
    }

    bool textLineWidthChanged = false;    if (m_textLineWidth) {
        const int previousWidth = settings.value(QStringLiteral("textLineWidth"), kDefaultTextLineWidth).toInt();
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
    const bool changed = before.toJson() != m_editStyles.toJson();
    m_root->applyLevelStyles(m_editStyles);
    m_originalStyles = m_editStyles;
    if (changed) {
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

    // 开关与选中色块也用同一枚强调色，随主题一起换。
    m_accentColor = tokens.accent;
    if (m_rainbowToggle) m_rainbowToggle->setAccentColor(tokens.accent);
    refreshRainbowSwatches();

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

MindMapSettingsDialog::~MindMapSettingsDialog() {
    // 构造函数把过滤器挂在 qApp 上，析构必须摘掉，否则 application 会持有悬空指针。
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }
}

bool MindMapSettingsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Wheel) return QDialog::eventFilter(watched, event);
    if (!isVisible()) return QDialog::eventFilter(watched, event);

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return QDialog::eventFilter(watched, event);
    // 下拉框弹出的列表是独立窗口，滚它应该是滚列表，不是滚页面。
    if (widget->window() != window()) return QDialog::eventFilter(watched, event);

    // 事件常常落在数字框内部私有的 QLineEdit 上，所以要沿父链认控件。
    bool onValueWidget = false;
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<QComboBox*>(current) || qobject_cast<QAbstractSpinBox*>(current)) {
            onValueWidget = true;
            break;
        }
        if (current == this) break;
    }
    if (!onValueWidget) return QDialog::eventFilter(watched, event);

    QScrollArea* area = pageScrollFor(widget);
    if (!area) return QDialog::eventFilter(watched, event);

    // 转给滚动区的 viewport：页面因此正常上下滚，控件值保持不变。
    auto* wheel = static_cast<QWheelEvent*>(event);
    QWheelEvent forwarded(wheel->position(), wheel->globalPosition(), wheel->pixelDelta(),
                          wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
                          wheel->phase(), wheel->inverted());
    QCoreApplication::sendEvent(area->viewport(), &forwarded);
    event->accept();
    return true;
}

// ============================================================
// 取消
// ============================================================

void MindMapSettingsDialog::reject() {
    // 「自定义」配色还没存就关窗口，先问一句要不要保存；用户选取消则继续编辑。
    if (m_editStyles.rainbow.enabled && m_customDirty && !confirmLeaveCustomPalette()) {
        return;
    }
    // 主题是即时生效的：取消时必须把它退回到打开窗口时的状态。
    rollbackThemeMode();
    QDialog::reject();
}
