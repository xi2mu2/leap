#pragma once

#include "LeapPluginApi.h"

#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>

class QObject;
class QPluginLoader;

// 一条加载记录：无论成功还是失败都保留，供「帮助」窗口展示诊断信息。
struct PluginLoadRecord {
    QString id;          // 目录名或 dll 文件名（不含扩展名）
    QString displayName; // 插件自报名字，失败时为空
    QString filePath;    // dll 绝对路径
    bool loaded = false;
    // 用户是否在「设置 → 插件」里关掉了它。这类插件根本没被加载，
    // loaded 保持 false，但 error 为空 —— 它是「主动关闭」而不是「加载失败」。
    bool disabled = false;
    QString error;       // 为空表示没有错误
};

// 已加载插件提供的「设置」整页信息。
// 插件只有额外实现了 LeapSettingsPageProvider 才会出现在这里。
struct PluginSettingsPage {
    QString pluginId;
    QString displayName;
    QString title;                                   // 左侧导航项文字
    LeapSettingsPageProvider* provider = nullptr;     // 归插件所有，不要 delete
};

// 已加载插件提供的导出器。插件只有额外实现了 LeapExporter 才会出现在这里。
struct PluginExporterEntry {
    QString pluginId;
    QString displayName;
    LeapExporter* exporter = nullptr;                 // 归插件所有，不要 delete
};

// 插件加载器。
//
// 约束（因为它会被编进 4 个测试可执行文件，见 CMakeLists.txt 的
// LEAP_CACHE_TEST_SOURCES）：
//  - 构造不扫描、不碰 GUI、不读写 QSettings；
//  - 只有显式调用 loadAll() 才访问磁盘；
//  - 插件目录不存在时安静返回空列表。
// （isPluginDisabled / setPluginDisabled 是静态工具函数，只有被显式调用时才碰
//  QSettings，构造与析构路径上都不会触发。）
class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // 默认的插件根目录：<exe 所在目录>/plugins。
    static QString defaultPluginRoot();

    // 本版宿主能接受的最低接口版本。
    // 插件声明的 apiVersion 落在 [kMinSupportedApiVersion, LEAP_PLUGIN_API_VERSION]
    // 区间内就算兼容 —— 新接口全是独立的可选接口，旧插件不需要重新编译。
    static constexpr int kMinSupportedApiVersion = 2;

    // 插件是否被用户在「设置 → 插件」里关掉。
    // 键是 PluginsDisabled/<id>，值为 true 才表示关闭；启用是默认状态，不留键。
    // 写成 inline 是因为设置窗口所在的 settings_preview 目标没有链接
    // PluginManager.cpp，但同样要读写这个键，实现放在头文件里两边共享。
    static bool isPluginDisabled(const QString& id) {
        QSettings settings;
        return settings.value(QStringLiteral("PluginsDisabled/") + id, false).toBool();
    }
    static void setPluginDisabled(const QString& id, bool disabled) {
        if (id.isEmpty()) return;
        QSettings settings;
        const QString key = QStringLiteral("PluginsDisabled/") + id;
        if (disabled) settings.setValue(key, true);
        else settings.remove(key);
    }

    // 扫描 rootDir 并激活全部插件。host 由调用方持有，必须活得比本对象久。
    // 任何单个插件失败都只记录在 records() 里，绝不弹窗、绝不中断加载。
    void loadAll(const QString& rootDir, LeapHost* host);

    // 逐个通知插件 deactivate()。不会（也无法）卸载 dll。
    void shutdown();

    const QList<PluginLoadRecord>& records() const { return m_records; }
    int loadedCount() const;
    QStringList loadedNames() const;

    // 提供了设置页的插件（按加载顺序）。没有插件提供时返回空列表。
    const QList<PluginSettingsPage>& settingsPages() const { return m_settingsPages; }

    // 提供了导出器的插件（按加载顺序）。
    const QList<PluginExporterEntry>& exporters() const { return m_exporters; }

    // 实现了 LeapEventSink 的插件，交给 LeapHostBridge 广播事件用。
    const QList<LeapEventSink*>& eventSinks() const { return m_eventSinks; }

private:
    void loadOne(const QString& dllPath, const QString& id, LeapHost* host);

    QList<PluginLoadRecord> m_records;
    QList<PluginSettingsPage> m_settingsPages;
    QList<PluginExporterEntry> m_exporters;
    QList<LeapEventSink*> m_eventSinks;
    QList<QObject*> m_instances;      // 已激活的插件对象，仅用于 shutdown
    QList<LeapPlugin*> m_plugins;     // 对应的接口指针
    // 故意不释放：一旦 delete QPluginLoader，dll 就可能被卸载，
    // 而插件注册的按钮 / 菜单项还指向其中的代码，会直接崩。
    QList<QPluginLoader*> m_loaders;
};
