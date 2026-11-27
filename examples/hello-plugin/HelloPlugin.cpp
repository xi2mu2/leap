#include "LeapPluginApi.h"

#include <QAction>
#include <QColor>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

// 最小的 leap 插件示例。
//
// 它刻意把 plugin.json 的 apiVersion 声明为 2（而不是当前的 3）：
// 宿主接受 2~3，所以这个插件同时证明了「API 2 的插件不用重新编译也能在
// API 3 的宿主上工作」。要用 API 3 的新能力就参考 examples/minimap_scale。
//
// 四件事必须做对，否则宿主加载不到：
//  1. 继承 QObject + LeapPlugin，QObject 必须写在第一个；
//  2. Q_PLUGIN_METADATA 指向一个 json 文件（文件名必须与下面写的字符串一致）；
//  3. Q_INTERFACES(LeapPlugin) 让 qobject_cast 能认出主接口；
//  4. 想额外提供「设置」整页，再实现 LeapSettingsPageProvider 并
//     Q_INTERFACES 里也写上它（不写的话宿主探测不到）。
//
// 编译要求：与 leap 本体完全相同的一套工具链（MinGW 11.2.0 + Qt 6.5.3）。
// 其它版本编译出来的 dll 即使能通过 apiVersion 检查，也可能在调用时崩溃。
class HelloPlugin : public QObject, public LeapPlugin, public LeapSettingsPageProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin LeapSettingsPageProvider)

public:
    bool activate(LeapHost* host, QString* errorMessage) override {
        if (!host) {
            if (errorMessage) *errorMessage = QStringLiteral("宿主接口为空");
            return false;
        }
        m_host = host;

        // 1. 在插件侧边栏加一个入口。
        LeapToolbarButtonSpec spec;
        spec.text = QStringLiteral("问候");
        spec.tooltip = QStringLiteral("示例插件：统计当前文档的节点数");
        // 图标留空即可，宿主会显示占位图标；
        // 要自定义图标就填绝对路径，例如 host->pluginDir() + "/icon.png"。
        QAction* toolbarAction = host->addToolbarButton(spec);
        if (toolbarAction) {
            connect(toolbarAction, &QAction::triggered, this, &HelloPlugin::onGreet);
        }

        // 2. 在「文件」菜单里也加一项，演示第二个扩展点。
        QAction* menuAction = host->addFileMenuAction(QStringLiteral("示例插件：节点统计"));
        if (menuAction) {
            connect(menuAction, &QAction::triggered, this, &HelloPlugin::onGreet);
        }

        // 3. 读一次上次保存的配置，演示 pluginSetting()。
        m_greeting = host->pluginSetting(QStringLiteral("greeting"),
                                         QStringLiteral("你好")).toString();
        return true;
    }

    void deactivate() override { m_host = nullptr; }

    // ---- LeapSettingsPageProvider：在「设置」里占一整页 ----
    QString settingsPageTitle() const override { return QStringLiteral("示例插件"); }

    QWidget* createSettingsPage(QWidget* parent) override {
        // 注意：每次打开设置窗口都会调用这里一次，必须新建控件，
        // 不要缓存上一次返回的 QWidget*（它已经被销毁了）。
        auto* page = new QWidget(parent);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        // 颜色从这里取，和宿主当前主题一致；不要复制粘贴十六进制色值。
        const QColor textColor = m_host ? m_host->textColor() : QColor(Qt::black);
        const QColor secondary = m_host ? m_host->secondaryTextColor() : QColor(Qt::gray);

        auto* title = new QLabel(QStringLiteral("问候语"), page);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setStyleSheet(QStringLiteral("color: %1;").arg(textColor.name()));
        layout->addWidget(title);

        auto* edit = new QLineEdit(page);
        edit->setText(m_greeting);
        edit->setPlaceholderText(QStringLiteral("问候时使用的称呼"));
        edit->setMaximumWidth(300);
        layout->addWidget(edit);

        auto* hint = new QLabel(QStringLiteral("这一页由插件自己绘制，不参与设置窗口底部的"
                                               "「应用 / 取消」——改完立即生效并保存。"),
                                page);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: %1;").arg(secondary.name()));
        layout->addWidget(hint);

        layout->addStretch(1);

        // 立即写入：插件自己的设置由插件自己负责持久化。
        connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_greeting = text;
            if (m_host) m_host->setPluginSetting(QStringLiteral("greeting"), text);
        });
        return page;
    }

private slots:
    void onGreet() {
        if (!m_host) return;
        m_host->showInfo(QStringLiteral("%1，leap %2！\n当前文档共 %3 个节点。")
                             .arg(m_greeting, m_host->appVersion())
                             .arg(m_host->nodeCount()));
    }

private:
    LeapHost* m_host = nullptr;
    QString m_greeting = QStringLiteral("你好");
};

#include "HelloPlugin.moc"
