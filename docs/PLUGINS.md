# leap 插件开发指南

leap 支持用**原生 C++ 动态库（dll）**扩展界面与行为。插件可以在左侧插件侧边栏和「文件」菜单里
增加自己的入口，在「设置」里增加独立的一整页配置界面，往「文件 → 导出」里加导出格式，
通过宿主接口读写当前文档（含整份文档的批量改写）、控制视图缩放与小地图，并在文档变化时收到通知。

本版（API 版本 4）仍然保持克制：界面扩展点是固定几处，没有热加载、没有在线安装。
启用 / 关闭是有的，但一样要重启才生效——运行期卸载插件会让已注册的入口变成悬空指针，
所以「重启」是刻意保留的唯一生效边界。装插件就是把文件夹拷到 `plugins/` 目录，**重启 leap 后生效**。

---

## 1. 插件怎么装

打包发布版和自建版本的插件目录都是 **leap.exe 同级目录下的 `plugins\`**：

```
leap.exe
plugins\
    hello\                    <- 一个插件一个文件夹（推荐，可以带图标等资源）
        leap_hello_plugin.dll
        icon.png
    another_plugin.dll        <- 也支持单文件插件，直接放 dll
```

- 拷贝进去后**重启 leap**。程序只在启动时扫描一次。
- 加载结果可以在 **帮助** 对话框里看到：已加载的插件会列出名字，加载失败的会带上原因。
  对话框里也有「打开插件目录」按钮，直接跳到该文件夹。
- 工具栏「插件」按钮开关左侧插件侧边栏；侧边栏底部常驻「管理插件…」，点开直接停在
  **设置 → 插件**。
- **设置 → 插件** 里有同样的清单（名称 / 路径 / 失败原因）。每行右侧的开关控制插件是否
  加载：关掉后重启 leap，那个插件就不会被激活（入口消失、设置页也消失），再打开同样要重启。
  加载失败的插件开关是灰的，改不动。
- 删除插件同样是删掉文件夹再重启。

> 「帮助」里显示失败原因是为了自检。常见原因：dll 没编成 Qt 插件、`apiVersion` 不匹配、
> 用了别的编译器/Qt 版本编译。

---

## 2. 扩展点

leap 的扩展点分成两类。

**界面扩展点**（宿主明确登记过的位置，一共四处）：

| 接口 | 效果 |
| --- | --- |
| `LeapHost::addToolbarButton(spec)` | 在左侧「插件」侧边栏里，给本插件加一个 52×50 的图标文字按钮 |
| `LeapHost::addFileMenuAction(text)` | 在工具栏「文件」下拉菜单末尾增加一项 |
| `LeapSettingsPageProvider` | 在「设置」左侧导航末尾增加一整页，页面内容完全由插件绘制 |
| `LeapExporter` | 在「文件 → 导出」子菜单里增加一个导出格式 |

前两个是 `LeapHost` 上的方法，后两个是**独立接口**：插件在自己的 `QObject` 上
额外继承它们，宿主加载时会自动探测（`qobject_cast`）。不实现就只是少一项能力，不算加载失败。

> 侧边栏由工具栏上的「插件」按钮开关，可滚动、按插件分组（分组标题是插件名），
> 因此插件数量再多也不会挤坏工具栏。接口名仍叫 `addToolbarButton` 是历史原因，
> 宿主只承诺「登记一个入口按钮」，当前版本把它放在侧边栏里。

**能力接口**（不改界面，只给插件更多能做的事）：

| 接口 | 效果 |
| --- | --- |
| `LeapEventSink` | 文档变化 / 选中变化 / 主题切换 / 文档载入时收到回调 |
| `LeapDocumentEditor` | 改选中节点（加同级、删节点、折叠展开） |
| `LeapDocumentWriter` | 读整份文档、整份替换（批量生成 / 批量重命名），一次撤销可整体回退 |
| `LeapViewControl` | 读改缩放百分比、读改小地图大小、居中、适应窗口 |

这四者同样是独立接口，插件按需实现，互不依赖。
**API 3 与 API 4 都没有改动 `LeapHost` / `LeapPlugin` / `LeapSettingsPageProvider` 的任何签名**，
所以 API 2 的插件不用重新编译，宿主同时接受 `apiVersion` 为 2~4 的插件。

前两个接口都返回 `QAction*`，插件自己 `connect(action, &QAction::triggered, ...)` 处理点击。
按钮会自动跟随明暗主题切换（文本与图标颜色由宿主刷新），不需要插件做任何事。

按钮规格：

```cpp
LeapToolbarButtonSpec spec;
spec.text     = "统计";            // 按钮下方的文字，必填
spec.tooltip  = "统计节点数量";    // 悬停提示，可空
spec.shortcut = "Ctrl+Alt+K";      // 可空；写了宿主会注册为窗口级快捷键
spec.iconPath = host->pluginDir() + "/icon.png";  // 可空；空时显示内置占位图标
```

图标支持 `.svg` 与 `.png`。SVG 里的 `currentColor` 会被替换成当前主题色；
**PNG 不会自动反色**，所以给深色主题也要能看清的话，建议用 SVG，或者自己画深色底也能辨识的图。

### 设置页（LeapSettingsPageProvider）

想让插件有自己的一整页设置，就在同一个 `QObject` 上额外继承这个接口：

```cpp
class MyPlugin : public QObject, public LeapPlugin, public LeapSettingsPageProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin LeapSettingsPageProvider)

public:
    // 返回非空字符串才会出现在「设置」左侧导航里
    QString settingsPageTitle() const override { return "我的插件"; }

    // 每次打开设置窗口都会调用一次，必须新建控件
    QWidget* createSettingsPage(QWidget* parent) override {
        auto* page = new QWidget(parent);
        auto* layout = new QVBoxLayout(page);
        auto* edit = new QLineEdit(page);
        auto* label = new QLabel("问候语", page);
        label->setStyleSheet(QString("color: %1;").arg(m_host->textColor().name()));
        layout->addWidget(label);
        layout->addWidget(edit);
        layout->addStretch(1);
        connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_host->setPluginSetting("greeting", text);   // 立即生效，没有「应用」按钮
        });
        return page;
    }
};
```

四条必须遵守的约定：

1. **每次打开设置窗口都会调用 `createSettingsPage()`**，插件不能缓存上次返回的 `QWidget*`
   —— 那个控件在窗口关闭时已经销毁了。
2. 返回的控件**归宿主所有**，插件不要自己 `delete`；在 `deactivate()` 里更不许碰它。
3. 这一页**不参与设置窗口的「应用 / 取消」事务**。用户点「取消」不会撤销插件页里的改动，
   所以自己做的控件应该**改完立即生效**，不要依赖一个「确定」按钮。
4. 页面宽度自适应，但注意内容列在 880×580 的窗口里可用宽度约 400px，别做太宽的表单。

### 颜色令牌

插件画的控件要跟宿主主题一致，从 `LeapHost` 取颜色，**不要硬编码**：

| 方法 | 用途 |
| --- | --- |
| `accentColor()` | 强调色（浅/深主题各一套） |
| `textColor()` | 主要文字 |
| `secondaryTextColor()` | 次要说明文字 |
| `borderColor()` | 边框、分隔线 |
| `cardColor()` | 卡片、面板底色 |
| `windowColor()` | 窗口背景 |

这些值在明暗主题之间会变。如果插件页需要长期存在并跟随切换，可以监听
`QGuiApplication::styleHints()->colorSchemeChanged`（宿主也是这么做的）。

### 保存插件自己的配置

用 `pluginSetting(key, fallback)` / `setPluginSetting(key, value)`，不要自己写 ini 或注册表：

```cpp
const QString greeting = host->pluginSetting("greeting", "你好").toString();
host->setPluginSetting("greeting", "早上好");
```

- 存储位置按**插件 id**（dll 文件名）分段，插件之间互相看不到，也不会撞键。
- 支持 `QString` / `int` / `bool` / `QStringList` / `QColor` 等 `QVariant` 能装的类型。
- 插件被删掉后，它的键再也读不到，不会影响其他插件。

---

## 3. 能力接口（API 3、4 新增）

这几个接口都是可选的，互不依赖，按需实现。

### 事件通知（LeapEventSink）

```cpp
class MyPlugin : public QObject, public LeapPlugin, public LeapEventSink {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin LeapEventSink)

public:
    void onDocumentChanged() override { /* 内容变了（含撤销/重做） */ }
    void onSelectionChanged(bool hasSelection) override { /* 选中的节点变了 */ }
    void onThemeChanged(bool dark) override { /* 明暗主题切换了 */ }
    void onDocumentLoaded(const QString& filePath) override { /* 新建/打开/导入了文档 */ }
};
```

**四条使用规则**，违反会出问题：

1. **回调里不要直接改文档。** 宿主有重入保护，在通知期间发起的改动触发的通知会被丢弃，
   结果是状态不同步。要改就推迟一轮：`QTimer::singleShot(0, this, [this]{ ... });`
2. 回调在 **UI 线程同步执行**，做耗时的事会直接卡住界面。要计算就丢到工作线程。
3. `onDocumentChanged()` 在每次撤销栈变化时都会触发，包括拖动节点、改文字，
   别在里面做重活。
4. 这些回调在 `deactivate()` 之后不会再被调用，但已经排队的 `QTimer` 需要你自己停。

### 文档编辑（LeapDocumentEditor）

```cpp
m_editor = dynamic_cast<LeapDocumentEditor*>(host);
if (m_editor) {
    m_editor->addSiblingToSelected("新节点");   // 给选中节点加同级
    m_editor->removeSelectedNode();              // 删掉选中节点（根节点会被拒绝）
    m_editor->toggleSelectedNodeExpanded();      // 折叠/展开
}
```

- 只操作**当前选中的节点**，不提供节点句柄 —— 节点指针的生命周期由 leap 内部管理，
  交出去就是悬空指针。
- 全部走宿主的撤销命令，所以**支持撤销/重做**，也会自动重排布局。
- **没有「设置节点颜色」**。leap 的颜色不是存在节点上的，而是由「层级样式 + 彩虹分支序号」
  推导出来，文件里也只存一份 styles。按节点改色既没法持久化，也会在下次重排或换主题时被覆盖。
  真要按节点配色得先改文件格式，不在当前范围内。

### 视图控制（LeapViewControl）

```cpp
m_view = dynamic_cast<LeapViewControl*>(host);
if (m_view) {
    m_view->setZoomPercent(150);              // 缩放，夹在 20%~400%
    int percent = m_view->miniMapSizePercent();
    m_view->setMiniMapSizePercent(150);       // 小地图大小，夹在 50%~200%，会持久化
    m_view->centerOnScenePoint(x, y);         // 把某个场景坐标移到视口中心
    m_view->fitToWindow();                    // 缩放到适应内容
}
```

小地图的可用范围是 **50%~200%**：基准尺寸 180×120，50% 只剩 90×60，
再小就既看不清也点不到。完整示例见 `examples/minimap_scale/`。

---

## 4. 文档能力（本版范围）

宿主**不暴露** `MindMapNode*` / `QGraphicsScene*`。原因是 leap 编译成单个 exe，
插件没法链接它的实现代码，传指针会在调用时崩溃。所以文档能力做成两点：

- **读取**：`hasSelectedNode()`、`selectedNodeText()`、`nodeCount()`、`documentToJson()`；
- **写入（单个节点）**：`setSelectedNodeText()`、`addChildToSelected()`；
- **写入（整份文档）**：`LeapDocumentWriter::replaceDocument()`。

单节点写入走的是宿主原有的命令行对象，因此**支持撤销 / 重做**，并且会自动重排布局。
`documentToJson()` 返回与 `.leap` 文件同构的 JSON（格式版本见 `MindMapIO::CURRENT_VERSION`），
适合做统计、导出、校验一类的只读功能。

`setSelectedNodeText()` 会把传入的纯文本按行拆成多个文本块，替换该节点**所有文本块**
（节点里已有的图片 / 公式块会保留）。

### 批量改写（LeapDocumentWriter）

单个节点写入解决不了「从 Markdown 生成整张图」「把所有叶子改名」这类需求——它们要一次动很多节点。
这时用 `LeapDocumentWriter`：

```cpp
LeapDocumentWriter* writer = dynamic_cast<LeapDocumentWriter*>(host);
if (!writer) { /* 宿主版本太老 */ }

// 最省事的写法：先读出来，改完传回去。
QJsonObject doc = host->documentToJson();
// ... 在这里改 doc 的 children / blocks ...
writer->replaceDocument(doc);
```

要点：

- `doc` 的格式与 `documentToJson()` 返回的完全一样，也等于 `.leap` 文件里的 `root` 对象；
- 整次替换是**一条撤销命令**，用户按一次 Ctrl+Z 就整体回到替换前；
- 替换会**清空重做栈**（旧文档的节点已经释放，栈里引用它们的命令不能留）；
- 传入空对象或无法解析时返回 `false`，此时**文档一个字节都没动**；
- 不需要自己写 `_copyright` / `_signature` 之类的文件头；
- 图片块要带 base64 的 `data`、公式块要带 `latex`，否则该块会被丢掉；
- `currentFilePath()` 返回当前文档路径（新建未保存时为空字符串）。

### 导出器（LeapExporter）

想往「文件 → 导出」里加一个自己的格式：

```cpp
class MyPlugin : public QObject, public LeapPlugin, public LeapExporter {
    Q_INTERFACES(LeapPlugin LeapExporter)
public:
    QString menuText()   const override { return "导出我的格式"; }
    QString fileFilter() const override { return "我的格式 (*.mine)"; }
    QString fileSuffix() const override { return "mine"; }
    bool exportDocument(const QJsonObject& doc, const QString& filePath,
                        QString* error) override {
        // doc 与 .leap 文件同构，可以直接遍历
        return true;
    }
};
```

宿主负责弹保存对话框、补扩展名、出错时提示；插件只做「把 JSON 写成文件」这一件事。
只做导出不做导入：导入要把外部格式映射回 leap 的 Block 体系，接口面会大得多。

---

## 5. 写一个插件

最小可运行的例子见 `examples/hello-plugin/`。骨架：

```cpp
#include "LeapPluginApi.h"
#include <QObject>

class MyPlugin : public QObject, public LeapPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LEAP_PLUGIN_IID FILE "plugin.json")
    Q_INTERFACES(LeapPlugin)

public:
    bool activate(LeapHost* host, QString* errorMessage) override {
        if (!host) { if (errorMessage) *errorMessage = "宿主为空"; return false; }
        m_host = host;
        QAction* action = host->addToolbarButton({"", "我的功能", "点我", ""});
        connect(action, &QAction::triggered, this, [this] {
            m_host->showInfo(QString("共 %1 个节点").arg(m_host->nodeCount()));
        });
        return true;
    }

    void deactivate() override { m_host = nullptr; }

private:
    LeapHost* m_host = nullptr;
};

#include "MyPlugin.moc"
```

需要设置页时，再加 `public LeapSettingsPageProvider` 和 `Q_INTERFACES(LeapPlugin LeapSettingsPageProvider)`
两个实现（`settingsPageTitle()` / `createSettingsPage()`），写法见前面「设置页」一节。
需要事件通知、文档编辑、视图控制、导出时，同样各自追加 `public LeapEventSink` /
`LeapDocumentEditor` / `LeapViewControl` / `LeapExporter`，并把它们一起写进 `Q_INTERFACES`。

`plugin.json`（元数据，编译期嵌进 dll，不改文件名就改它）：

```json
{
    "name": "我的插件",
    "apiVersion": 4,
    "version": "1.0.0",
    "author": "你的名字",
    "description": "一句话说明"
}
```

`activate()` 返回 `false` 时，把原因写进 `errorMessage`，用户能在「帮助」里看到。

### 接口版本

`LEAP_PLUGIN_API_VERSION` 当前为 `4`，宿主接受 `apiVersion` 落在 **2~4** 的插件。

版本 1 → 2：`LeapHost` 追加了 6 个颜色令牌和 2 个配置存取方法，并新增了
`LeapSettingsPageProvider` 接口。

版本 2 → 3：**没有改动任何已有签名**，只新增了四个独立的可选接口
（`LeapEventSink` / `LeapDocumentEditor` / `LeapViewControl` / `LeapExporter`）。

版本 3 → 4：同样**没有改动任何已有签名**，只新增了一个独立接口 `LeapDocumentWriter`。

所以 API 2 / 3 的插件不需要重新编译，在 API 4 宿主上照常工作；它拿不到新能力而已。

版本 1 的插件会被拒绝加载：v1 的 `LeapHost` 比现在少 8 个虚函数，
宿主没法分辨「旧插件 + 新宿主」的组合，调过去就是错位的虚表。
所以下限卡在 2，上限是当前版本。

---

## 6. 编译要求（很重要）

插件的 dll 必须用**和 leap 本体完全相同的一套工具链**编译：

- MinGW-w64 GCC **11.2.0**（Qt 自带的那套：`C:\Qt\Tools\mingw1120_64`）
- **Qt 6.5.3**（`C:\Qt\6.5.3\mingw_64`）

原因是插件与宿主共享 C++ 对象（`QString`、`QAction`、虚函数表）。不同编译器版本、
不同 Qt 版本、甚至不同的 STL 实现都会导致：

- `qobject_cast<LeapPlugin*>` 返回空 ——「dll 未实现 LeapPlugin 接口」；
- 或者更糟：能 cast 成功，但一调用就崩。

`apiVersion` 只能挡住**接口签名**变化，挡不住编译器差异。所以报错时优先怀疑工具链。

接口里只使用 Qt 类型和 `QString`/`QJsonObject`，**不要**在跨 dll 边界的函数签名里用
`std::string` / `std::vector` 之类 STL 类型。

### 用 CMake 编译

**最省事的办法**：`examples/hello-plugin/` 里带了一个**独立**的 `CMakeLists.txt`，不依赖 leap
主工程。把它整个文件夹拷出去，改一改就能编：

```
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64"
cmake --build build
```

产物是 `build/leap_hello_plugin.dll`，连同 `plugin.json` 一起放进 leap 的
`plugins/hello/` 目录即可。如果文件夹已经离开了 leap 源码树，加
`-DLEAP_PLUGIN_API_DIR=<含 LeapPluginApi.h 的目录>` 指定头文件位置，或者干脆把
`LeapPluginApi.h` 复制到这个文件夹里（同目录的头优先使用）。

**在自己工程里手写**，参考下面这段（等价于主工程末尾的 `leap_hello_plugin` 目标）：

```cmake
add_library(my_plugin MODULE
    MyPlugin.cpp
    /path/to/leap/LeapPluginApi.h   # 或把 LeapPluginApi.h 复制进你的工程
)
target_include_directories(my_plugin PRIVATE "/path/to/leap")
target_link_libraries(my_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(my_plugin PROPERTIES PREFIX "")
```

在 leap 主工程里构建示例插件：

```
cmake --build <build目录> --target leap_hello_plugin
cmake --build <build目录> --target leap_minimap_scale_plugin
cmake --build <build目录> --target leap_markdown_import_plugin
```

产物自动落在 `<build目录>/plugins/hello/leap_hello_plugin.dll`、
`<build目录>/plugins/minimap_scale/leap_minimap_scale_plugin.dll` 与
`<build目录>/plugins/markdown_import/leap_markdown_import_plugin.dll`，而 `<build目录>` 里正好有
`leap.exe`，直接运行就能看到「插件」侧边栏里的「问候」「小地图缩放」「Markdown」三个按钮，
设置里也会多出「示例插件」一页。

> `examples/hello-plugin/` 演示三个界面扩展点（侧边栏按钮、文件菜单项、设置整页）
> 与 `pluginSetting` / `setPluginSetting`；
> `examples/minimap_scale/` 演示 API 3 的 `LeapViewControl`（改小地图大小并持久化）；
> `examples/markdown_import/` 演示 API 4 的 `LeapDocumentWriter`（把 Markdown 一次性转成整份导图）。

---

## 7. 安全与许可

- `plugins\` 里的 dll 会在 **leap 进程内、以你的用户权限** 执行，能做这个用户能做的任何事。
  所以只装你信得过的插件；本版没有签名校验，来源需要自己把关。
- leap 以 **GPL-3.0** 发布。插件通过 `Q_DECLARE_INTERFACE` 与本接口交互，接口头文件本身
  是功能性的，通常认为可用于闭源插件；但如果你要分发修改过的 leap 本体，仍需按 GPL 开源。
  商用闭源分发前建议先确认许可边界（联系方式见 README）。

---

## 8. 本版明确没有的东西

避免误期待，以下都还没做：

- 通用插件管理 UI——启用/关闭开关在「设置 → 插件」里可用，但没有排序、没有卸载按钮；
  插件自己的配置项请用 `pluginSetting` / `setPluginSetting` 自己存，宿主不提供通用配置界面；
- 热加载（改完必须重启）；
- 节点右键菜单、往内置页里追加卡片、导入格式注册这些扩展点；
  设置页只有「整页」这一种粒度；
- 插件之间的依赖声明（谁先加载、谁依赖谁）——当前按目录名字母序加载；
- 无代码的 JSON 插件；
- 在线安装 / 更新 / 卸载。

`LeapPluginApi.h` 的接口一旦发布就尽量保持稳定；确实需要改动时会提高
`LEAP_PLUGIN_API_VERSION`，让旧插件被干净地拒绝而不是崩溃。
