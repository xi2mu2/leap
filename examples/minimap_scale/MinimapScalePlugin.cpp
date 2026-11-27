#include "LeapPluginApi.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

// minimap_scale：把「小地图缩放」做成一个插件，用来验证 API 3 的 LeapViewControl。
//
// 它演示三件事：
//  1. 侧边栏入口按钮（addToolbarButton）；
//  2. 从插件里读写宿主视图状态（zoomPercent / miniMapSizePercent）；
//  3. 用宿主给的颜色令牌画自己的界面（textColor / secondaryTextColor），
//     这样浅色/深色主题下都不用自己判断。
//
// 注意：本插件完全不需要 include leap 的任何内部头文件，只依赖 LeapPluginApi.h。
class MinimapScalePlugin : public QObject, public LeapPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin)

public:
    bool activate(LeapHost* host, QString* errorMessage) override {
        if (!host) {
            if (errorMessage) *errorMessage = QStringLiteral("宿主接口为空");
            return false;
        }
        m_host = host;

        // 视图能力是独立可选接口：宿主没实现就说明版本太老，直接拒绝激活，
        // 比激活之后按钮点了没反应要好排查。
        m_view = dynamic_cast<LeapViewControl*>(host);
        if (!m_view) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("宿主没有提供 LeapViewControl，需要 leap 2.17 或更新版本");
            }
            return false;
        }

        LeapToolbarButtonSpec spec;
        spec.text = QStringLiteral("小地图缩放");
        spec.tooltip = QStringLiteral("调整小地图的大小（%1%~%2%）")
                           .arg(kMinPercent)
                           .arg(kMaxPercent);
        QAction* action = host->addToolbarButton(spec);
        if (action) {
            connect(action, &QAction::triggered, this, &MinimapScalePlugin::showDialog);
        }
        return true;
    }

    void deactivate() override {
        m_host = nullptr;
        m_view = nullptr;
    }

private:
    // 下限 50%：小地图基准尺寸是 180x120，再小就只剩十几个像素，
    // 既看不清也点不到。上限 200% 已经占掉屏幕右下角一大块。
    static constexpr int kMinPercent = 50;
    static constexpr int kMaxPercent = 200;

    void showDialog() {
        if (!m_host || !m_view) return;

        // 插件的 QObject 没有父控件，对话框挂到当前活动窗口上，
        // 这样它会居中在 leap 主窗口而不是屏幕中央。
        QDialog dialog(QApplication::activeWindow());
        dialog.setWindowTitle(QStringLiteral("小地图缩放"));
        dialog.setModal(true);
        dialog.setMinimumWidth(340);

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 20, 24, 18);
        layout->setSpacing(14);

        // 标题与说明都用宿主给的颜色令牌，自动跟随浅色/深色主题。
        auto* title = new QLabel(QStringLiteral("请输入小地图的大小（%1到%2）")
                                     .arg(kMinPercent)
                                     .arg(kMaxPercent),
                                 &dialog);
        QFont titleFont = title->font();
        titleFont.setPixelSize(15);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("color: %1;").arg(m_host->textColor().name()));
        layout->addWidget(title);

        auto* inputRow = new QHBoxLayout();
        inputRow->setSpacing(6);
        auto* edit = new QLineEdit(&dialog);
        edit->setValidator(new QIntValidator(kMinPercent, kMaxPercent, edit));
        edit->setText(QString::number(m_view->miniMapSizePercent()));
        edit->setAlignment(Qt::AlignRight);
        edit->setFixedWidth(90);
        auto* percentLabel = new QLabel(QStringLiteral("%"), &dialog);
        percentLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(m_host->secondaryTextColor().name()));
        inputRow->addWidget(edit);
        inputRow->addWidget(percentLabel);
        inputRow->addStretch(1);
        layout->addLayout(inputRow);

        auto* hint = new QLabel(QStringLiteral("输入数字后立即生效，并会记住到下次启动。"), &dialog);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: %1;").arg(m_host->secondaryTextColor().name()));
        layout->addWidget(hint);

        auto* buttons = new QHBoxLayout();
        buttons->addStretch(1);
        auto* closeButton = new QPushButton(QStringLiteral("关闭"), &dialog);
        closeButton->setObjectName(QStringLiteral("primaryButton"));
        connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);

        // 边输入边生效：用户不用先按确定再看效果。
        connect(edit, &QLineEdit::textChanged, this, [this, edit](const QString& text) {
            if (!m_view || text.isEmpty()) return;
            bool ok = false;
            const int value = text.toInt(&ok);
            if (!ok || value < kMinPercent || value > kMaxPercent) return;
            m_view->setMiniMapSizePercent(value);
        });

        edit->selectAll();
        edit->setFocus();
        dialog.exec();
    }

    LeapHost* m_host = nullptr;
    LeapViewControl* m_view = nullptr;
};

#include "MinimapScalePlugin.moc"
