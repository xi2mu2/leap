#pragma once

#include "LeapPluginApi.h"

#include <QHash>
#include <QVariant>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class MainWindow;
class MindMapView;
class QGraphicsScene;
class QGridLayout;
class QUndoStack;
class QWidget;
class QMenu;

// LeapHost 在 leap 主程序里的实现。
//
// 单独成文件是为了让 MainWindow 不直接依赖插件接口的细节，
// 也方便将来换成方案 2 的具名插槽实现。
//
// 除了 LeapHost，它同时实现三个可选接口：
//   LeapDocumentEditor —— 改文档（都走既有撤销命令，插件无需感知 MindMapNode）
//   LeapViewControl    —— 缩放 / 小地图 / 视角
//   LeapEventSink 的宿主侧 —— 由 MainWindow 调 notify*() 转发给各插件
class LeapHostBridge : public QObject,
                       public LeapHost,
                       public LeapDocumentEditor,
                       public LeapDocumentWriter,
                       public LeapViewControl {
public:
    // window：宿主主窗口（调用方传 MainWindow*，这里只当 QWidget* 用，
    //         免得为了一个对话框把整个 MainWindow.h 拉进来）。
    // container：插件侧边栏里专门留给插件的容器（自带一个垂直布局，
    //           插件按钮按插件分组追加进去）。容器由 MainWindow 负责
    //           随「插件」开关显示/隐藏，这里不碰可见性。
    // fileMenu：「文件」下拉菜单。
    // scene / undoStack / view 允许为空（真实运行时都不为空）。
    LeapHostBridge(QWidget* window,
                   QWidget* container,
                   QMenu* fileMenu,
                   QGraphicsScene* scene,
                   QUndoStack* undoStack,
                   MindMapView* view,
                   QObject* parent = nullptr);

    // ---- LeapHost 实现 ----
    QAction* addToolbarButton(const LeapToolbarButtonSpec& spec) override;
    QAction* addFileMenuAction(const QString& text) override;
    bool hasSelectedNode() const override;
    QString selectedNodeText() const override;
    bool setSelectedNodeText(const QString& text) override;
    bool addChildToSelected(const QString& text) override;
    QJsonObject documentToJson() const override;
    int nodeCount() const override;
    bool isDarkMode() const override;
    void showInfo(const QString& message) override;
    void showError(const QString& message) override;
    QString pluginDir() const override;
    QString appVersion() const override;
    QColor accentColor() const override;
    QColor textColor() const override;
    QColor secondaryTextColor() const override;
    QColor borderColor() const override;
    QColor cardColor() const override;
    QColor windowColor() const override;
    QVariant pluginSetting(const QString& key, const QVariant& fallback = QVariant()) const override;
    void setPluginSetting(const QString& key, const QVariant& value) override;

    // PluginManager 在每次 activate 之前设置，供插件定位自己的资源文件，
    // 同时告诉桥接层「接下来登记入口的是哪个插件」，侧边栏据此分组。
    // id 用于插件配置的键分段，必须是稳定标识（宿主传 dll 的文件名）。
    void setCurrentPlugin(const QString& dir, const QString& id, const QString& displayName);

    // ---- LeapDocumentEditor 实现 ----
    bool addSiblingToSelected(const QString& text) override;
    bool removeSelectedNode() override;
    bool toggleSelectedNodeExpanded() override;
    bool selectedNodeExpanded() const override;

    // ---- LeapDocumentWriter 实现 ----
    bool replaceDocument(const QJsonObject& doc) override;
    QString currentFilePath() const override;

    // ---- LeapViewControl 实现 ----
    int zoomPercent() const override;
    void setZoomPercent(int percent) override;
    int miniMapSizePercent() const override;
    bool setMiniMapSizePercent(int percent) override;
    void centerOnScenePoint(qreal x, qreal y) override;
    void fitToWindow() override;

    // ---- 事件广播（MainWindow 调用，转发给实现了 LeapEventSink 的插件）----
    // sinks 由 PluginManager 在加载完成后注入（这里不持有所有权）。
    void setEventSinks(const QList<LeapEventSink*>& sinks);
    void notifyDocumentChanged();
    void notifySelectionChanged(bool hasSelection);
    void notifyThemeChanged(bool dark);
    void notifyDocumentLoaded(const QString& filePath);

    // 侧边栏里某个插件的分组容器（首次登记按钮时懒创建）。
    // 返回组内那个网格布局，按钮追加到这个网格里。
    QGridLayout* groupGridForCurrentPlugin();

    // activate() 结束后调用：插件一个按钮都没登记时，补一行灰色说明，
    // 避免侧边栏里出现一个只有标题、下面空着的分组。
    void finishCurrentPlugin();

private:
    QWidget* m_window = nullptr;
    QWidget* m_container = nullptr;
    QMenu* m_fileMenu = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QUndoStack* m_undoStack = nullptr;
    MindMapView* m_view = nullptr;
    // 实现了 LeapEventSink 的插件。归 PluginManager 所有。
    QList<LeapEventSink*> m_eventSinks;
    // 重入保护：插件在通知回调里又改文档会再次触发通知，
    // 这里直接丢弃嵌套通知，避免死循环。
    bool m_notifying = false;
    QString m_currentPluginDir;
    // 当前插件的稳定 id（= dll 文件名，不含扩展名）。配置存储按它分段。
    QString m_currentPluginId;
    // 当前正在激活的插件名（显示在侧边栏分组标题上）。
    QString m_currentPluginName;
    // 插件名 -> 该分组的网格布局。插件按钮都追加到自己的网格里。
    QHash<QString, QGridLayout*> m_groupGrids;
    // 插件名 -> 该分组的网格是否已经有按钮（用于决定要不要补「没有界面入口」）。
    QHash<QString, int> m_groupButtonCounts;
};
