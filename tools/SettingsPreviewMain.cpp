// settings_preview - 开发者工具
//
// 把“设置”对话框按 100% 缩放离屏渲染成 PNG，用于检查浅色 / 深色两套令牌、
// 控件高度与预览绘制，避免只靠读代码判断视觉效果。
//
// 用法：settings_preview <输出目录>

#include "MindMapSettingsDialog.h"
#include "SettingsWidgets.h"
#include "UiTheme.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

void renderTheme(const QString& outputDir, const QString& mode, const QString& suffix,
                 const QSize& size) {
    QSettings settings;
    settings.setValue(QStringLiteral("themeMode"), mode);
    settings.setValue(QStringLiteral("darkTheme"), mode == QLatin1String("dark"));
    settings.sync();

    MindMapSettingsDialog dialog(nullptr);
    dialog.resize(size);
    dialog.show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* nav = dialog.findChild<QListWidget*>(QStringLiteral("categoryList"));
    if (!nav) return;

    const QStringList pageNames = {QStringLiteral("node"), QStringLiteral("paragraph"),
                                   QStringLiteral("theme"), QStringLiteral("operation")};

    if (qEnvironmentVariableIsSet("SETTINGS_PREVIEW_DEBUG")) {
        nav->setCurrentRow(0);
        QApplication::processEvents();
        if (auto* pages = dialog.findChild<QStackedWidget*>()) {
            qDebug() << "pages" << pages->geometry() << "min" << pages->minimumSizeHint();
            if (auto* cur = qobject_cast<QScrollArea*>(pages->currentWidget()))
                qDebug() << "pageScroll" << cur->geometry() << "vp" << cur->viewport()->size()
                         << "widget" << cur->widget()->geometry();
        }
    }

    for (int row = 0; row < pageNames.size() && row < nav->count(); ++row) {
        nav->setCurrentRow(row);
        QApplication::processEvents();
        const QString path = QDir(outputDir).filePath(
            QStringLiteral("settings-%1-%2.png").arg(suffix, pageNames.at(row)));
        dialog.grab().save(path);
    }

    // 设置页内容比窗口高时要滚动才能看全，这里再出一组滚到底的图。
    if (qEnvironmentVariableIsSet("SETTINGS_PREVIEW_SCROLL")) {
        for (int row = 0; row < pageNames.size() && row < nav->count(); ++row) {
            nav->setCurrentRow(row);
            QApplication::processEvents();
            auto* pages = dialog.findChild<QStackedWidget*>();
            if (!pages) continue;
            auto* scroll = qobject_cast<QScrollArea*>(pages->currentWidget());
            if (!scroll) continue;
            QScrollBar* bar = scroll->verticalScrollBar();
            bar->setValue(bar->maximum());
            QApplication::processEvents();
            const QString path = QDir(outputDir).filePath(
                QStringLiteral("settings-%1-%2-bottom.png").arg(suffix, pageNames.at(row)));
            dialog.grab().save(path);
            bar->setValue(0);
            QApplication::processEvents();
        }
    }

    // 彩虹分支的预览与画布是两套绘制路径，必须单独出图检查（对齐、节点大小、留白）。
    if (qEnvironmentVariableIsSet("SETTINGS_PREVIEW_RAINBOW")) {
        for (int row = 0; row < pageNames.size() && row < nav->count(); ++row) {
            if (pageNames.at(row) != QLatin1String("node")) continue;
            nav->setCurrentRow(row);
            QApplication::processEvents();
            // 开关现在自绘成 ToggleSwitch，不再是带文字的 chip 按钮。
            auto* toggle = dialog.findChild<ToggleSwitch*>(QStringLiteral("rainbowToggle"));
            if (!toggle) return;
            toggle->click();
            QApplication::processEvents();
            // 开关会在两张卡片之间搬动颜色字段，一次事件循环不够布局收敛，
            // 否则会拍到字段还停在中途的样子。
            QApplication::processEvents();
            const QString path = QDir(outputDir).filePath(
                QStringLiteral("settings-%1-node-rainbow.png").arg(suffix));
            dialog.grab().save(path);
            toggle->click();
            QApplication::processEvents();
            QApplication::processEvents();
            // 关掉开关后再拍一张：用来确认三个颜色字段被搬回来时显示的是层级色，
            // 而不是上一个分支的派生色。
            const QString restoredPath = QDir(outputDir).filePath(
                QStringLiteral("settings-%1-node-rainbow-off.png").arg(suffix));
            dialog.grab().save(restoredPath);
        }
    }
}

// 对话框在预览工具里拿不到“非默认样式”（构造需要真实节点），
// 因此单独渲染一屏控件样本，检查颜色字段的重置按钮、禁用态与分段选择器。
void renderWidgetGallery(const QString& outputDir, const QString& mode) {
    const UiTheme::Tokens tokens = mode == QLatin1String("dark") ? UiTheme::dark() : UiTheme::light();

    QWidget panel;
    panel.setStyleSheet(UiTheme::styleSheet(tokens));
    auto* layout = new QVBoxLayout(&panel);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* changed = new ColorField(&panel);
    changed->setDefaultColor(QColor(5, 56, 96));
    changed->setColor(QColor(0xb9, 0x1c, 0x1c));
    layout->addWidget(changed);

    auto* disabled = new ColorField(&panel);
    disabled->setDefaultColor(QColor(211, 234, 253));
    disabled->setColor(QColor(211, 234, 253));
    disabled->setEnabled(false);
    layout->addWidget(disabled);

    auto* chipRow = new QHBoxLayout;
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(8);
    auto* chip = new QToolButton(&panel);
    chip->setObjectName(QStringLiteral("chipButton"));
    chip->setText(QStringLiteral("跟随填充"));
    chip->setCheckable(true);
    chip->setChecked(true);
    chip->setCursor(Qt::PointingHandCursor);
    chipRow->addWidget(chip);
    chipRow->addStretch(1);
    layout->addLayout(chipRow);

    auto* segments = new SegmentedControl(&panel);
    segments->addItem(QStringLiteral("左"), 1);
    segments->addItem(QStringLiteral("居中"), 2);
    segments->addItem(QStringLiteral("右"), 3);
    layout->addWidget(segments);
    layout->addStretch(1);

    panel.resize(360, 250);
    panel.show();
    QApplication::processEvents();
    panel.grab().save(
        QDir(outputDir).filePath(QStringLiteral("settings-widgets-%1.png").arg(mode)));
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("leap-preview"));
    QCoreApplication::setApplicationName(QStringLiteral("settings-preview"));

    const QString outputDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    // 注册表写入在受限环境里会被拒绝，浅色/深色就渲染成同一张图。
    // 这里改用 ini 格式并把存储位置放到输出目录，保证两套主题都能出图。
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outputDir);
    QSize size(880, 580);
    if (argc > 2) {
        const QStringList parts = QString::fromLocal8Bit(argv[2]).split(QLatin1Char('x'));
        if (parts.size() == 2) size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
    }
    renderTheme(outputDir, QStringLiteral("light"), QStringLiteral("light"), size);
    renderTheme(outputDir, QStringLiteral("dark"), QStringLiteral("dark"), size);
    renderWidgetGallery(outputDir, QStringLiteral("light"));
    renderWidgetGallery(outputDir, QStringLiteral("dark"));
    return 0;
}
