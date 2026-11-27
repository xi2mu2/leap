#pragma once

// ============================================================
// leap 插件 ABI 契约（唯一对外接口，除提升 API_VERSION 外不再改动）
//
// 设计约束（改动前务必先读）：
//  1. leap 是 exe，没有可链接的库。插件**不能**链接 leap 的任何目标文件，
//     因此这里只能出现跨模块安全的纯虚接口 + Qt 类型（QString / QJsonObject /
//     QAction* 等），禁止出现 STL 容器、inline 实现或 leap 内部类型。
//  2. 绝不暴露 MindMapNode* / QGraphicsScene* 之类内部指针：插件要拿到它们就
//     必须 include 并链接 leap 的内部实现，这在纯 dll 方案下不成立。
//     文档能力改为「最小操作集 + JSON 交换」。
//  3. 插件必须用与 leap 相同的工具链编译（MinGW 11.2.0 + Qt 6.5.3）。
//     apiVersion 只能挡住「接口版本不一致」，挡不住编译器 / Qt 版本不一致：
//     后者会让 qobject_cast 返回空或直接崩溃。
// ============================================================

#include <QAction>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QtPlugin>

// 插件头文件只依赖这几个 Qt 类型；QWidget 用前置声明就够，
// 免得插件作者漏 include <QWidget> 就编译失败。
class QWidget;

#define LEAP_PLUGIN_IID "com.xi2mu2.leap.plugin/1.0"

// 可选的「设置页」扩展接口。插件想往「设置」里加一整页时额外实现它，
// 宿主用 qobject_cast 探测；不实现也不影响主接口。
#define LEAP_SETTINGS_PAGE_IID "com.xi2mu2.leap.settingspage/1.0"

// 以下四个是 API 3 新增的**可选**能力接口。
//
// 设计要点：新能力一律做成「独立的可选接口」，而不是往 LeapHost / LeapPlugin
// 里追加虚函数。这样：
//   - 已有接口的 vtable 一个字节都不动，API 2 的插件在 API 3 宿主上仍然可用；
//   - 以后再加扩展点也只是多一个 IID，不必再升全局版本号。
// 插件想用哪个，就额外 public 继承哪个，并在 Q_INTERFACES 里列出来。

// 事件通知。插件实现它就能从「被点才动」变成「持续工作」。
#define LEAP_EVENT_SINK_IID "com.xi2mu2.leap.eventsink/1.0"
// 文档编辑（写操作）。全部走 leap 的撤销栈，用户可 Ctrl+Z。
#define LEAP_DOCUMENT_EDITOR_IID "com.xi2mu2.leap.documenteditor/1.0"
// 视图控制：缩放、小地图尺寸、居中、适应窗口。
#define LEAP_VIEW_CONTROL_IID "com.xi2mu2.leap.viewcontrol/1.0"
// 导出器：往「文件 → 导出」子菜单里注册自己的格式。
#define LEAP_EXPORTER_IID "com.xi2mu2.leap.exporter/1.0"
#define LEAP_DOCUMENT_WRITER_IID "com.xi2mu2.leap.documentwriter/1.0"

// 接口版本：LeapHost / LeapPlugin 有不兼容改动时 +1。
//
// 2：LeapHost 末尾追加了颜色令牌与插件配置读写；新增 LeapSettingsPageProvider。
//    追加虚函数在 vtable 上是兼容的，但仍然 +1 —— 因为宿主无法分辨
//    「按新头文件编译的插件」和「旧宿主」，只有版本号完全相等才能保证安全。
// 3：新增四个**独立**的可选接口（事件/文档编辑/视图控制/导出器），
//    LeapHost / LeapPlugin / LeapSettingsPageProvider 均未改动。
//    宿主接受 2 与 3（见 PluginManager::kMinSupportedApiVersion）。
// 4：新增 LeapDocumentWriter（批量改写文档）。同样没有改动任何已有签名。
//    宿主接受 2~4。
#define LEAP_PLUGIN_API_VERSION 4

// 工具栏按钮的外观描述。宿主统一用它创建 IconTextButton，插件不需要
// 也不应该自己去构造 leap 的控件类。
struct LeapToolbarButtonSpec {
    // 插件目录内的图标文件绝对路径（png / svg）。
    // 留空时宿主使用内置的通用图标。
    QString iconPath;
    // 按钮下方文字，建议两个字（与宿主现有按钮一致）。
    QString text;
    QString tooltip;
    // 可空的快捷键字符串，例如 "Ctrl+Alt+H"。由宿主注册到主窗口。
    QString shortcut;
};

// 宿主向插件暴露的能力集合。所有实现归 leap 所有，插件只做虚调用。
class LeapHost {
public:
    virtual ~LeapHost() = default;

    // ---------- UI 扩展点（P0 只有这两个） ----------
    // 在主工具栏注册一个按钮。返回的 QAction 归宿主所有，
    // 插件只需 connect(action, &QAction::triggered, ...)；
    // 插件不得 delete 它，也不得把它挂到别的父对象上。
    // 返回 nullptr 表示注册失败（插件应把这视为 activate 失败）。
    virtual QAction* addToolbarButton(const LeapToolbarButtonSpec& spec) = 0;

    // 在「文件」下拉菜单末尾追加一项，返回的 QAction 同样归宿主。
    virtual QAction* addFileMenuAction(const QString& text) = 0;

    // ---------- 文档能力（全部走 leap 的撤销栈，可撤销） ----------
    virtual bool hasSelectedNode() const = 0;
    // 选中节点的纯文本（多个文本块以换行连接）；无选中节点时返回空串。
    virtual QString selectedNodeText() const = 0;
    // 用纯文本覆盖选中节点的全部文本块。成功返回 true。
    virtual bool setSelectedNodeText(const QString& text) = 0;
    // 给选中节点添加一个子节点并填入文本。成功返回 true。
    virtual bool addChildToSelected(const QString& text) = 0;
    // 当前文档的完整 JSON（与 .leap 文件同构），供插件只读分析。
    virtual QJsonObject documentToJson() const = 0;
    virtual int nodeCount() const = 0;

    // ---------- 通用 ----------
    virtual bool isDarkMode() const = 0;
    virtual void showInfo(const QString& message) = 0;
    virtual void showError(const QString& message) = 0;
    // 当前正在被 activate 的插件所在目录（绝对路径）。
    // 插件应从这里解析自己的图标等资源，不要假设工作目录。
    virtual QString pluginDir() const = 0;
    virtual QString appVersion() const = 0;

    // ---------- 只读颜色令牌（API 2） ----------
    // 让插件按当前主题自己画控件。宿主只承诺「给你这几个颜色」，
    // **不承诺**任何 QSS objectName / 样式表结构 —— 那些属于内部实现。
    virtual QColor accentColor() const = 0;
    virtual QColor textColor() const = 0;
    virtual QColor secondaryTextColor() const = 0;
    virtual QColor borderColor() const = 0;
    virtual QColor cardColor() const = 0;
    virtual QColor windowColor() const = 0;

    // ---------- 插件配置存储（API 2） ----------
    // 按插件隔离（键前缀是当前插件 id），卸载插件不会在注册表里留垃圾
    // （宿主用 id 分段，删除插件目录后这一段就再也读不到）。
    // 插件不需要自己拼 QSettings 的组织名/应用名。
    virtual QVariant pluginSetting(const QString& key, const QVariant& fallback = QVariant()) const = 0;
    virtual void setPluginSetting(const QString& key, const QVariant& value) = 0;
};

// 插件实现类。写法见 examples/hello-plugin/HelloPlugin.cpp。
class LeapPlugin {
public:
    virtual ~LeapPlugin() = default;

    // 启动时调用一次。返回 false 时把原因写进 errorMessage，
    // 宿主会把它列在「帮助」里，不会弹窗、不会影响其他插件。
    virtual bool activate(LeapHost* host, QString* errorMessage) = 0;

    // leap 退出时调用。dll 被进程锁住无法卸载，因此这里只做清理，不要指望
    // 之后还能重新加载。
    virtual void deactivate() {}
};

// 可选的设置页提供者。插件同时实现它，才能在「设置」里多出一整页。
//
// 生命周期约定（务必遵守）：
//  - createSettingsPage() 每次打开设置窗口都会被调用一次，必须**新建**一个
//    控件并 return；不得缓存上一次的 QWidget*（那个对象已被销毁）。
//  - 返回的控件归宿主所有，插件在 deactivate() 里不要 delete 它。
//  - 这一页不参与设置窗口底部的「应用 / 取消」事务：插件改了什么就立即生效，
//    插件自己负责保存（用 setPluginSetting）。
class LeapSettingsPageProvider {
public:
    virtual ~LeapSettingsPageProvider() = default;

    // 左侧导航与页头显示的文字。返回空串表示不提供设置页。
    virtual QString settingsPageTitle() const = 0;

    // 页面内容。parent 由宿主传入，插件把控件建在它下面即可。
    virtual QWidget* createSettingsPage(QWidget* parent) = 0;
};

// ============================================================
// API 3：四个独立的可选能力接口
// ============================================================

// 事件通知。宿主在下列时机回调（全部在主线程、同步调用）。
//
// 重入规则（务必遵守，否则会死循环）：
//  - 回调里**不要**调用任何修改文档的方法（setSelectedNodeText /
//    addChildToSelected / LeapDocumentEditor 的任何写操作）。
//    宿主有保护位会直接丢弃嵌套通知，你改的东西也不会进撤销栈。
//  - 回调里只做「读 + 更新自己的界面」。要改文档，用 QTimer::singleShot(0, ...)
//    推迟到通知结束之后。
//  - 实现要快：它在 UI 线程上同步执行，卡住就是整个界面卡住。
class LeapEventSink {
public:
    virtual ~LeapEventSink() = default;

    // 文档内容变了：编辑文字、撤销、重做、导入、新建。宿主已做防抖。
    virtual void onDocumentChanged() {}

    // 选中状态变了（选中/取消选中/切换到别的节点）。
    virtual void onSelectionChanged(bool hasSelection) {}

    // 明暗主题切换。
    virtual void onThemeChanged(bool dark) {}

    // 文件被打开 / 新建 / 另存为后。path 为空表示新建的未命名文档。
    virtual void onDocumentLoaded(const QString& filePath) {}
};

// 文档编辑。全部经过 leap 的撤销栈，用户可 Ctrl+Z 撤销。
//
// 注意：这一组只对「当前选中的节点」操作 —— 宿主不提供节点句柄，
// 因为节点会因为撤销/删除而失效，句柄的生命周期没法安全约定。
// 需要操作任意节点时，请用 LeapDocumentEditor 配合
// LeapHost::documentToJson() 做只读分析，再让用户选中目标节点。
class LeapDocumentEditor {
public:
    virtual ~LeapDocumentEditor() = default;

    // 给选中节点加一个同级节点（加在它后面）。成功返回 true。
    virtual bool addSiblingToSelected(const QString& text) = 0;

    // 删除选中节点（含子树）。成功返回 true。
    virtual bool removeSelectedNode() = 0;

    // 注意：没有「设置节点颜色」。
    // leap 的颜色不保存在节点上，而是由「层级样式 + 彩虹分支序号」推导出来
    // （见 MindMapStyle.h 的 MindMapLevelStyles 与 MindMapNode::styleBranchIndex()），
    // 文件里也只存一份 styles，不存每个节点的颜色。所以按节点改色既无法持久化、
    // 也会在下次重排/换主题时被覆盖掉。真要支持得先改文件格式。

    // 折叠/展开选中节点。返回操作后的展开状态；无选中节点时返回 false。
    virtual bool toggleSelectedNodeExpanded() = 0;

    // 选中节点是否是折叠状态。
    virtual bool selectedNodeExpanded() const = 0;
};

// 批量改写文档（API 4 新增，独立接口）。
//
// 为什么不直接往 LeapDocumentEditor 里加方法：那个接口已经随 API 3 发布，
// 再加纯虚函数会让「API 3 插件 + API 4 宿主」的 vtable 错位。按本头文件的
// 一贯原则，新能力一律另开接口。
class LeapDocumentWriter {
public:
    virtual ~LeapDocumentWriter() = default;

    // 用 doc 整体替换当前文档。
    //
    // doc 的格式与 LeapHost::documentToJson() 返回的完全一样（也与 .leap 文件里的
    // "root" 对象一样）。最省事的写法是先读出来、改完再传回去：
    //
    //   QJsonObject doc = host->documentToJson();
    //   QJsonArray children = doc["children"].toArray();
    //   // ...改 children...
    //   doc["children"] = children;
    //   writer->replaceDocument(doc);
    //
    // 语义与约束：
    //  - 整个替换是**一条撤销命令**，用户按一次 Ctrl+Z 就能整体回退；
    //    替换前的文档对象被完整保留，撤销回来的是原来那批节点（含选中/折叠状态）。
    //  - 会清空重做栈里的后续步骤（撤销栈的固有行为，不是本接口的选择）。
    //  - doc 必须是一个节点对象（顶层对象本身就是根）。空对象或解析失败返回 false，
    //    此时文档不动。
    //  - 不需要也不应该带 "_copyright" / "_signature" 之类文件头字段，保存时由宿主生成。
    //  - 图片块要带 base64 的 data 字段，公式块要带 latex，否则该块会被丢掉。
    virtual bool replaceDocument(const QJsonObject& doc) = 0;

    // 当前文档的文件路径；未保存过的新文档返回空字符串。
    // 配合 LeapEventSink::onDocumentLoaded() 使用：后激活的插件只能靠这个
    // 知道用户当前打开的是哪个文件。
    virtual QString currentFilePath() const = 0;
};

// 视图控制。
class LeapViewControl {
public:
    virtual ~LeapViewControl() = default;

    // 当前缩放百分比（100 = 原始大小）。
    virtual int zoomPercent() const = 0;
    // 设置缩放百分比。宿主会夹到自己的上下限内。
    virtual void setZoomPercent(int percent) = 0;

    // 小地图尺寸百分比（100 = 默认 180x120）。
    virtual int miniMapSizePercent() const = 0;
    // 设置小地图尺寸百分比，宿主夹到 [50, 200]。设置后立即生效并持久化。
    // 返回 false 表示当前没有小地图（例如用户在小地图设置里选了「关闭」时
    // 小地图控件仍然存在，这种情况会返回 true；只有控件不存在才返回 false）。
    virtual bool setMiniMapSizePercent(int percent) = 0;

    // 把视野中心移到场景坐标 (x, y)。
    virtual void centerOnScenePoint(qreal x, qreal y) = 0;

    // 缩放到适应窗口。
    virtual void fitToWindow() = 0;
};

// 导出器。插件实现它，就会在「文件 → 导出」子菜单里多一项。
//
// 宿主负责：弹保存对话框、处理路径冲突、捕获异常、给失败提示。
// 插件负责：把 doc 转成目标格式写进 filePath。写失败时把原因填进 error。
class LeapExporter {
public:
    virtual ~LeapExporter() = default;

    // 菜单项文字，例如「导出为 Markdown」。
    virtual QString menuText() const = 0;
    // 保存对话框的文件过滤器，例如「Markdown (*.md)」。
    virtual QString fileFilter() const = 0;
    // 默认扩展名，不带点，例如「md」。
    virtual QString fileSuffix() const = 0;

    // doc 与 .leap 文件同构（见 LeapHost::documentToJson）。
    // 返回 false 时把失败原因写进 error，宿主会弹出来。
    virtual bool exportDocument(const QJsonObject& doc, const QString& filePath,
                                QString* error) = 0;
};

Q_DECLARE_INTERFACE(LeapPlugin, LEAP_PLUGIN_IID)
Q_DECLARE_INTERFACE(LeapSettingsPageProvider, LEAP_SETTINGS_PAGE_IID)
Q_DECLARE_INTERFACE(LeapEventSink, LEAP_EVENT_SINK_IID)
Q_DECLARE_INTERFACE(LeapDocumentEditor, LEAP_DOCUMENT_EDITOR_IID)
Q_DECLARE_INTERFACE(LeapViewControl, LEAP_VIEW_CONTROL_IID)
Q_DECLARE_INTERFACE(LeapExporter, LEAP_EXPORTER_IID)
Q_DECLARE_INTERFACE(LeapDocumentWriter, LEAP_DOCUMENT_WRITER_IID)
