#include "PluginManager.h"

// 只为了把「当前插件目录」告诉宿主实现（LeapHost::pluginDir）。
// 这里只需要它的类声明，不会把 GUI 实现拖进测试。
#include "LeapHostBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QPluginLoader>
#include <QStringList>
#include <QVector>

namespace {

// 在插件元数据里递归找某个键。Qt 版本不同会把自定义元数据放在顶层、
// 或者放到 "MetaData" 子对象里，写死层级很容易在新版本上失效。
QJsonValue findKey(const QJsonObject& obj, const QString& key, int depth = 0) {
    if (depth > 3) return QJsonValue();
    const QJsonValue direct = obj.value(key);
    if (!direct.isUndefined()) return direct;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isObject()) {
            const QJsonValue nested = findKey(it.value().toObject(), key, depth + 1);
            if (!nested.isUndefined()) return nested;
        }
    }
    return QJsonValue();
}

QString findString(const QJsonObject& obj, const QString& key) {
    return findKey(obj, key).toString();
}

int findInt(const QJsonObject& obj, const QString& key, bool* found) {
    const QJsonValue value = findKey(obj, key);
    *found = !value.isUndefined();
    return value.toInt(1);
}

} // namespace

PluginManager::~PluginManager() {
    shutdown();
    // 故意不 delete loader、不调用 unload()：
    //  dll 一旦被加载，进程内就锁住了；卸载正在使用的代码会直接崩溃。
    //  进程退出时由系统回收，这是 Qt 插件体系的正常做法。
    for (QPluginLoader* loader : m_loaders) {
        loader->setParent(nullptr);
    }
    m_loaders.clear();
}

QString PluginManager::defaultPluginRoot() {
    // 用 applicationDirPath()：插件是相对 exe 定位的，不能用工作目录。
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
}

int PluginManager::loadedCount() const {
    int n = 0;
    for (const auto& record : m_records) {
        if (record.loaded) ++n;
    }
    return n;
}

QStringList PluginManager::loadedNames() const {
    QStringList names;
    for (const auto& record : m_records) {
        if (record.loaded) names.append(record.displayName.isEmpty() ? record.id : record.displayName);
    }
    return names;
}

void PluginManager::loadAll(const QString& rootDir, LeapHost* host) {
    if (!host) return;

    QDir root(rootDir);
    if (!root.exists()) {
        // 没装插件是正常状态，不产生任何记录、不报错。
        return;
    }

    // 两种摆放方式都支持：
    //   plugins/<id>/<id>.dll     （推荐，插件可以带自己的资源文件）
    //   plugins/<id>.dll          （单文件插件）
    QStringList dllPaths;
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                     QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            QDir sub(entry.absoluteFilePath());
            const QFileInfoList subs = sub.entryInfoList({QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
            for (const QFileInfo& dll : subs) {
                dllPaths.append(dll.absoluteFilePath());
            }
        } else if (entry.suffix().compare(QStringLiteral("dll"), Qt::CaseInsensitive) == 0) {
            dllPaths.append(entry.absoluteFilePath());
        }
    }

    for (const QString& dllPath : dllPaths) {
        const QString id = QFileInfo(dllPath).completeBaseName();
        loadOne(dllPath, id, host);
    }
}

void PluginManager::loadOne(const QString& dllPath, const QString& id, LeapHost* host) {
    PluginLoadRecord record;
    record.id = id;
    record.filePath = dllPath;

    auto* loader = new QPluginLoader(dllPath);
    m_loaders.append(loader);

    const QJsonObject meta = loader->metaData();
    if (meta.isEmpty()) {
        record.error = QStringLiteral("不是 Qt 插件（缺少元数据）：%1").arg(loader->errorString());
        m_records.append(record);
        return;
    }

    record.displayName = findString(meta, QStringLiteral("name"));
    if (record.displayName.isEmpty()) record.displayName = id;

    // 用户关掉的插件：保留一条记录让设置页能显示它、并让用户重新打开，
    // 但绝不 instance()/activate()。注意这里不设 error —— 它不是失败。
    if (isPluginDisabled(id)) {
        record.disabled = true;
        m_records.append(record);
        return;
    }

    // 接口版本校验：插件在 plugin.json 里写 "apiVersion": 2 或 3。
    // Qt 不同版本把插件元数据嵌在不同层级（顶层或 "MetaData" 子对象），
    // 所以这里递归找，不依赖具体结构。
    // 缺省视为 1（最早期的手写插件）；落在
    // [kMinSupportedApiVersion, LEAP_PLUGIN_API_VERSION] 区间内都算兼容 ——
    // API 3 的新能力全是独立的可选接口，API 2 插件不需要重新编译。
    bool foundApi = false;
    const int apiVersion = findInt(meta, QStringLiteral("apiVersion"), &foundApi);
    const int effectiveApi = foundApi ? apiVersion : 1;
    if (effectiveApi < kMinSupportedApiVersion || effectiveApi > LEAP_PLUGIN_API_VERSION) {
        record.error = QStringLiteral("接口版本不兼容：插件要求 %1，当前 leap 支持 %2~%3")
                           .arg(effectiveApi)
                           .arg(kMinSupportedApiVersion)
                           .arg(LEAP_PLUGIN_API_VERSION);
        m_records.append(record);
        return;
    }

    QObject* instance = loader->instance();
    if (!instance) {
        record.error = QStringLiteral("无法加载 dll：%1").arg(loader->errorString());
        m_records.append(record);
        return;
    }

    LeapPlugin* plugin = qobject_cast<LeapPlugin*>(instance);
    if (!plugin) {
        record.error = QStringLiteral("dll 未实现 LeapPlugin 接口"
                                      "（请检查是否用同版本 MinGW 11.2.0 + Qt 6.5.3 编译）");
        m_records.append(record);
        return;
    }

    QString errorMessage;
    // 让宿主知道「现在正在被激活的是哪个插件」，插件在 activate 里就能用
    // pluginDir() 定位自己的图标等资源。这里用 dynamic_cast 而不是往 LeapHost
    // 里加 setter：LeapHost 是对外 ABI，不该为了宿主内部需要而膨胀。
    if (auto* bridge = dynamic_cast<LeapHostBridge*>(host)) {
        bridge->setCurrentPlugin(QFileInfo(dllPath).absolutePath(), record.id, record.displayName);
    }
    if (!plugin->activate(host, &errorMessage)) {
        record.error = errorMessage.isEmpty() ? QStringLiteral("插件 activate() 返回 false")
                                              : errorMessage;
        m_records.append(record);
        return;
    }

    record.loaded = true;
    m_records.append(record);
    m_instances.append(instance);
    m_plugins.append(plugin);
    // activate() 已经返回：此时才知道这个插件到底登记了几个入口。
    if (auto* bridge = dynamic_cast<LeapHostBridge*>(host)) {
        bridge->finishCurrentPlugin();
    }

    // 可选扩展点：插件额外实现了「设置整页」才登记。探测失败属于正常情况，
    // 不写错误、不算加载失败。
    if (auto* pageProvider = qobject_cast<LeapSettingsPageProvider*>(instance)) {
        const QString title = pageProvider->settingsPageTitle();
        if (!title.isEmpty()) {
            PluginSettingsPage page;
            page.pluginId = record.id;
            page.displayName = record.displayName;
            page.title = title;
            page.provider = pageProvider;
            m_settingsPages.append(page);
        }
    }

    // 可选扩展点：导出器。
    if (auto* exporter = qobject_cast<LeapExporter*>(instance)) {
        const QString menuText = exporter->menuText();
        if (!menuText.isEmpty()) {
            PluginExporterEntry entry;
            entry.pluginId = record.id;
            entry.displayName = record.displayName;
            entry.exporter = exporter;
            m_exporters.append(entry);
        }
    }

    // 可选扩展点：事件接收。没有界面也能工作，所以不加额外条件。
    if (auto* sink = qobject_cast<LeapEventSink*>(instance)) {
        m_eventSinks.append(sink);
    }
}

void PluginManager::shutdown() {    // 逆序调用：后加载的插件可能依赖先加载的插件建好的东西。
    for (int i = m_plugins.size() - 1; i >= 0; --i) {
        m_plugins[i]->deactivate();
    }
    m_plugins.clear();
    m_instances.clear();
    // 设置页提供者 / 导出器 / 事件接收器指向的正是这些已 deactivate 的插件对象，
    // 清掉引用，避免设置窗口或事件广播拿到悬空指针。
    m_settingsPages.clear();
    m_exporters.clear();
    m_eventSinks.clear();
}
