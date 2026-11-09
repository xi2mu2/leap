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

    const QStringList pageNames = {QStringLiteral("font"), QStringLiteral("paragraph"),
                                   QStringLiteral("node"), QStringLiteral("theme"),
                                   QStringLiteral("operation")};

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
