#include "LeapHostBridge.h"

#include "IconTextButton.h"
#include "MindMapBlock.h"
#include "MindMapCommands.h"
#include "MindMapIO.h"
#include "MindMapNode.h"
#include "MindMapView.h"
#include "UiColors.h"

#include <QAction>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 与 MainWindow::findRoot() 同义：场景里没有父节点的那个节点。
MindMapNode* rootOf(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    const auto items = scene->items();
    for (auto* item : items) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) return node;
        }
    }
    return nullptr;
}

// 选中节点的查找写法与 MainWindow::onAddChild() 保持一致。
MindMapNode* selectedNodeOf(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    const auto items = scene->items();
    for (auto* item : items) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (node->isNodeSelected()) return node;
        }
    }
    return nullptr;
}

// 收集节点全部文本块的 HTML，顺序与序列化一致。
QStringList textBlockHtml(MindMapNode* node) {
    QStringList htmls;
    if (!node) return htmls;
    for (auto* block : node->blocks()) {
        if (block->type() == BlockType::Text) {
            htmls.append(static_cast<TextBlock*>(block)->toHtml());
        }
    }
    return htmls;
}

int countNodes(MindMapNode* node) {
    if (!node) return 0;
    int total = 1;
    for (auto* child : node->childNodes()) total += countNodes(child);
    return total;
}

// 纯文本 -> 文本块 HTML。文本块内部是 QTextDocument，直接塞裸文本会被当成
// HTML 解析（"a < b" 里的 < 会吃掉后面内容），所以逐行转义。
QStringList plainTextToBlocks(const QString& text) {
    QStringList blocks;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        blocks.append(QStringLiteral("<p>%1</p>").arg(line.toHtmlEscaped()));
    }
    if (blocks.isEmpty()) blocks.append(QStringLiteral("<p></p>"));
    return blocks;
}

// detachSubtree() 是 MindMapCommands.cpp 的内部辅助函数，没在头文件里导出。
// 这里需要同样的语义（递归摘除整棵子树），所以自己写一份小的。
void detachSubtreeFromScene(MindMapNode* node, QGraphicsScene* scene) {
    if (!node) return;
    node->clearEdges();
    if (scene && node->scene()) scene->removeItem(node);
    for (auto* child : node->childNodes()) {
        detachSubtreeFromScene(child, scene);
    }
}

} // namespace

LeapHostBridge::LeapHostBridge(QWidget* window,
                               QWidget* container,
                               QMenu* fileMenu,
                               QGraphicsScene* scene,
                               QUndoStack* undoStack,
                               MindMapView* view,
                               QObject* parent)
    : QObject(parent),
      m_window(window),
      m_container(container),
      m_fileMenu(fileMenu),
      m_scene(scene),
      m_undoStack(undoStack),
      m_view(view) {}

void LeapHostBridge::setCurrentPlugin(const QString& dir, const QString& id,
                                      const QString& displayName) {
    m_currentPluginDir = dir;
    m_currentPluginId = id;
    m_currentPluginName = displayName;
}

QGridLayout* LeapHostBridge::groupGridForCurrentPlugin() {
    if (!m_container) return nullptr;
    auto* boxLayout = qobject_cast<QVBoxLayout*>(m_container->layout());
    if (!boxLayout) return nullptr;

    const QString key = m_currentPluginName.isEmpty() ? QStringLiteral("插件") : m_currentPluginName;
    if (QGridLayout* existing = m_groupGrids.value(key)) {
        return existing;
    }

    // 每个插件一个分组：标题行 + 网格（按钮三列排布，52px 宽正好塞进 240px 侧边栏）。
    auto* group = new QWidget(m_container);
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(6);

    auto* title = new QLabel(key, group);
    // 打上标记，MainWindow::applyTheme() 换主题时据此重设颜色。
    title->setProperty("leapPluginGroupTitle", true);
    QFont titleFont = title->font();
    titleFont.setPixelSize(12);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setContentsMargins(2, 0, 0, 0);
    groupLayout->addWidget(title);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    groupLayout->addLayout(grid);

    boxLayout->addWidget(group);
    m_groupGrids.insert(key, grid);
    return grid;
}

QAction* LeapHostBridge::addToolbarButton(const LeapToolbarButtonSpec& spec) {
    if (!m_container || !m_container->layout()) return nullptr;

    // 插件按钮宿主统一构造，保证与原生按钮同尺寸、同主题、同交互。
    // 没有自带图标时用内置的 plugin 图标，避免出现一个只有文字的空白按钮。
    auto* button = new IconTextButton(spec.iconPath.isEmpty() ? QStringLiteral("plugin") : QString(),
                                      spec.text, 22, 10, m_container);
    if (!spec.iconPath.isEmpty()) {
        button->setIconFromFile(spec.iconPath);
    }
    if (!spec.tooltip.isEmpty()) {
        button->setToolTip(spec.tooltip);
    } else if (!spec.shortcut.isEmpty()) {
        button->setToolTip(QStringLiteral("%1 (%2)").arg(spec.text, spec.shortcut));
    }

    auto* action = new QAction(spec.text, m_container);
    if (!spec.shortcut.isEmpty()) {
        // 快捷键挂在窗口上，插件自己不必（也不该）持有 QShortcut。
        auto* shortcut = new QShortcut(QKeySequence(spec.shortcut), m_container->window());
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, action, &QAction::trigger);
    }
    connect(button, &QToolButton::clicked, action, &QAction::trigger);

    // 放进当前插件的分组网格。侧边栏的可见性由 MainWindow 管，这里不碰。
    QGridLayout* grid = groupGridForCurrentPlugin();
    if (!grid) {
        delete button;
        delete action;
        return nullptr;
    }
    const int index = grid->count();
    // 侧边栏每行只放一个按钮（宽度按单列算）。
    const int columns = 1;
    grid->addWidget(button, index / columns, index % columns,
                    Qt::AlignLeft | Qt::AlignTop);
    ++m_groupButtonCounts[m_currentPluginName.isEmpty() ? QStringLiteral("插件") : m_currentPluginName];
    return action;
}

void LeapHostBridge::finishCurrentPlugin() {
    if (!m_container) return;
    auto* boxLayout = qobject_cast<QVBoxLayout*>(m_container->layout());
    if (!boxLayout) return;

    const QString key = m_currentPluginName.isEmpty() ? QStringLiteral("插件") : m_currentPluginName;
    if (m_groupButtonCounts.value(key) > 0) return;

    // 插件一个入口都没登记：分组本身可能还没建，这里补齐并写一行说明。
    QGridLayout* grid = groupGridForCurrentPlugin();
    if (!grid) return;
    m_groupButtonCounts.insert(key, 1);  // 防止重复调用时叠加多行说明

    auto* note = new QLabel(QStringLiteral("此插件没有界面入口"), m_container);
    note->setProperty("leapPluginGroupTitle", true);
    QFont noteFont = note->font();
    noteFont.setPixelSize(12);
    note->setFont(noteFont);
    note->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    grid->addWidget(note, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
}

QAction* LeapHostBridge::addFileMenuAction(const QString& text) {
    if (!m_fileMenu) return nullptr;
    QAction* action = m_fileMenu->addAction(text);
    return action;
}

bool LeapHostBridge::hasSelectedNode() const {
    return selectedNodeOf(m_scene) != nullptr;
}

QString LeapHostBridge::selectedNodeText() const {
    MindMapNode* node = selectedNodeOf(m_scene);
    return node ? node->allText() : QString();
}

bool LeapHostBridge::setSelectedNodeText(const QString& text) {
    MindMapNode* node = selectedNodeOf(m_scene);
    if (!node) return false;

    // 走 EditTextCommand，才能获得撤销/重做与 relayout 语义；
    // 直接 setAllText() 会重建文本块，历史里查不到这一步。
    const QStringList oldBlocks = textBlockHtml(node);
    const QStringList newBlocks = plainTextToBlocks(text);

    if (m_undoStack) {
        m_undoStack->push(new EditTextCommand(node, oldBlocks, newBlocks, true));
    } else {
        node->setAllText(text);
        relayout(node->getRoot());
        if (m_scene) m_scene->update();
    }
    return true;
}

bool LeapHostBridge::addChildToSelected(const QString& text) {
    MindMapNode* node = selectedNodeOf(m_scene);
    if (!node || !m_scene) return false;

    if (node->isEditing()) node->finishEdit();

    if (m_undoStack) {
        m_undoStack->push(new AddChildCommand(node, m_scene));
        // AddChildCommand 会把新节点设为选中，这里再填文本。
        MindMapNode* created = selectedNodeOf(m_scene);
        if (created && created != node && !text.isEmpty()) {
            const QStringList oldBlocks = textBlockHtml(created);
            m_undoStack->push(new EditTextCommand(created, oldBlocks, plainTextToBlocks(text), false));
        }
        return true;
    }

    auto* child = new MindMapNode(text, node);
    m_scene->addItem(child);
    relayout(node->getRoot());
    node->setNodeSelected(false);
    child->setNodeSelected(true);
    return true;
}

QJsonObject LeapHostBridge::documentToJson() const {
    MindMapNode* root = rootOf(m_scene);
    if (!root) return QJsonObject();
    return MindMapIO::serialize(root);
}

int LeapHostBridge::nodeCount() const {
    return countNodes(rootOf(m_scene));
}

bool LeapHostBridge::isDarkMode() const {
    return UiColors::isDarkMode();
}

void LeapHostBridge::showInfo(const QString& message) {
    if (m_window) {
        QMessageBox::information(m_window, QStringLiteral("插件"), message);
    }
}

void LeapHostBridge::showError(const QString& message) {
    if (m_window) {
        QMessageBox::warning(m_window, QStringLiteral("插件"), message);
    }
}

QString LeapHostBridge::pluginDir() const {
    return m_currentPluginDir;
}

QString LeapHostBridge::appVersion() const {
    return QCoreApplication::applicationVersion();
}

// ---- 只读颜色令牌 ----
// 直接复用 UiColors 的明暗两套常量，插件因此和宿主同一套配色，
// 但拿不到任何 QSS objectName（那是内部实现，不能当 ABI）。
QColor LeapHostBridge::accentColor() const {
    return UiColors::PRIMARY();
}

QColor LeapHostBridge::textColor() const {
    return UiColors::isDarkMode() ? UiColors::DARK_TEXT_PRIMARY() : UiColors::TEXT_PRIMARY();
}

QColor LeapHostBridge::secondaryTextColor() const {
    return UiColors::isDarkMode() ? UiColors::DARK_TEXT_SECONDARY() : UiColors::TEXT_SECONDARY();
}

QColor LeapHostBridge::borderColor() const {
    return UiColors::isDarkMode() ? UiColors::DARK_BORDER() : UiColors::BORDER();
}

QColor LeapHostBridge::cardColor() const {
    return UiColors::isDarkMode() ? UiColors::DARK_BG_TOOLBAR() : UiColors::BG_TOOLBAR();
}

QColor LeapHostBridge::windowColor() const {
    return UiColors::isDarkMode() ? UiColors::DARK_BG_CANVAS() : UiColors::BG_TOOLBAR();
}

// ---- 插件配置存储 ----
// 键统一挂在 Plugins/<id>/ 下：卸载插件后这一段再也读不到，
// 不会把注册表留成垃圾场。插件不需要知道 QSettings 的组织名/应用名。
QVariant LeapHostBridge::pluginSetting(const QString& key, const QVariant& fallback) const {
    if (m_currentPluginId.isEmpty() || key.isEmpty()) return fallback;
    QSettings settings;
    return settings.value(QStringLiteral("Plugins/%1/%2").arg(m_currentPluginId, key), fallback);
}

void LeapHostBridge::setPluginSetting(const QString& key, const QVariant& value) {
    if (m_currentPluginId.isEmpty() || key.isEmpty()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("Plugins/%1/%2").arg(m_currentPluginId, key), value);
}

// ---- LeapDocumentEditor ----
// 全部走既有撤销命令，插件不需要（也不能）拿到 MindMapNode*：
// 节点指针的生命周期由 leap 内部管理，交出去就是悬空指针。

bool LeapHostBridge::addSiblingToSelected(const QString& text) {
    MindMapNode* node = selectedNodeOf(m_scene);
    if (!node) return false;
    if (node->isEditing()) node->finishEdit();

    if (!m_undoStack) {
        // 没有撤销栈时退回直接操作（真实运行时不会走到这里）。
        if (!node->parentNode()) return false;
        auto* created = new MindMapNode(QString(), nullptr);
        MindMapNode* parent = node->parentNode();
        const int index = parent->childNodes().indexOf(node);
        parent->insertChild(index + 1, created);
        if (m_scene) m_scene->addItem(created);
        relayout(node->getRoot());
        node->setNodeSelected(false);
        created->setNodeSelected(true);
        if (!text.isEmpty()) created->setAllText(text);
        return true;
    }

    m_undoStack->push(new AddSiblingCommand(node, m_scene));
    // AddSiblingCommand 会把新节点设为选中，这里补一次文本（它建的是空节点）。
    if (!text.isEmpty()) {
        if (MindMapNode* created = selectedNodeOf(m_scene)) {
            const QStringList oldBlocks = textBlockHtml(created);
            m_undoStack->push(new EditTextCommand(created, oldBlocks,
                                                  plainTextToBlocks(text), false));
        }
    }
    return true;
}

bool LeapHostBridge::removeSelectedNode() {
    MindMapNode* node = selectedNodeOf(m_scene);
    if (!node) return false;
    // 根节点删掉就整个文档没了，拒绝。
    if (!node->parentNode()) return false;
    if (node->isEditing()) node->finishEdit();

    if (m_undoStack) {
        m_undoStack->push(new RemoveNodeCommand(node, m_scene));
    } else {
        MindMapNode* root = node->getRoot();
        node->parentNode()->removeChild(node);
        // 子树里每个节点都是独立的 QGraphicsItem，必须递归摘除，
        // 只 removeItem 顶层节点会把后代留在场景里。
        detachSubtreeFromScene(node, m_scene);
        delete node;
        relayout(root);
    }
    return true;
}

bool LeapHostBridge::toggleSelectedNodeExpanded() {
    MindMapNode* node = selectedNodeOf(m_scene);
    if (!node) return false;
    const bool willExpand = !node->isExpanded();
    if (m_undoStack) {
        m_undoStack->push(new ToggleExpandCommand(node, willExpand));
    } else {
        node->setExpanded(willExpand);
    }
    return willExpand;
}

bool LeapHostBridge::selectedNodeExpanded() const {
    MindMapNode* node = selectedNodeOf(m_scene);
    return node ? node->isExpanded() : false;
}

// ---- LeapDocumentWriter ----
// 整体替换文档。这是给「批量改写」用的：插件先 documentToJson() 读整棵树，
// 改完再整个传回来，而不是只能一个一个节点地改。

bool LeapHostBridge::replaceDocument(const QJsonObject& doc) {
    if (!m_scene || doc.isEmpty()) return false;

    MindMapNode* oldRoot = rootOf(m_scene);
    if (!oldRoot) return false;
    if (!m_undoStack) return false;   // 没有撤销栈就无法保证可回退，宁可不做

    // 结束编辑态，否则编辑中的文本块会在文档被换掉后指向已删除的节点。
    if (MindMapNode* editing = MindMapNode::findEditingNode(m_scene)) {
        editing->finishEdit();
    }

    // 先试反序列化一次：doc 有问题就在这里失败，此时文档一个字节都没动。
    // 命令里还会再解析一次，代价是两次解析，换来「失败时绝不破坏现有文档」。
    MindMapNode* probe = MindMapIO::deserialize(doc, nullptr);
    if (!probe) return false;
    delete probe;

    const QJsonObject oldDoc = MindMapIO::serialize(oldRoot);

    // clear() 是必须的：命令只保存两份 JSON 快照，push() 之后会立刻把旧树
    // 整个 delete 掉，栈里若还留着引用旧节点的命令（EditTextCommand 之类），
    // 撤销时就会访问已释放内存。
    m_undoStack->clear();

    // push() 会立刻调 redo()，由命令完成「删旧树 + 装新树」。
    m_undoStack->push(new ReplaceDocumentCommand(oldDoc, doc, m_scene,
                                                 QStringLiteral("插件替换文档")));
    return true;
}

QString LeapHostBridge::currentFilePath() const {
    return m_view ? m_view->currentFilePath() : QString();
}

// ---- LeapViewControl ----

int LeapHostBridge::zoomPercent() const {
    return m_view ? m_view->zoomPercent() : 100;
}

void LeapHostBridge::setZoomPercent(int percent) {
    if (m_view) m_view->setZoomPercent(percent);
}

int LeapHostBridge::miniMapSizePercent() const {
    return m_view ? m_view->miniMapSizePercent() : 100;
}

bool LeapHostBridge::setMiniMapSizePercent(int percent) {
    if (!m_view) return false;
    // 顺带持久化：小地图尺寸是全局偏好，重启后应该保持。
    m_view->setMiniMapSizePercent(percent);
    QSettings().setValue(QStringLiteral("minimapSizePercent"), m_view->miniMapSizePercent());
    return true;
}

void LeapHostBridge::centerOnScenePoint(qreal x, qreal y) {
    if (m_view) m_view->centerOn(x, y);
}

void LeapHostBridge::fitToWindow() {
    if (m_view) m_view->fitToWindow();
}

// ---- 事件广播 ----
// 全部同步调用，且带重入保护：插件在回调里改文档会再次触发通知，
// 这里直接丢弃嵌套的那一层，避免无限递归。

void LeapHostBridge::setEventSinks(const QList<LeapEventSink*>& sinks) {
    m_eventSinks = sinks;
}

void LeapHostBridge::notifyDocumentChanged() {
    if (m_notifying) return;
    m_notifying = true;
    for (LeapEventSink* sink : m_eventSinks) {
        if (sink) sink->onDocumentChanged();
    }
    m_notifying = false;
}

void LeapHostBridge::notifySelectionChanged(bool hasSelection) {
    if (m_notifying) return;
    m_notifying = true;
    for (LeapEventSink* sink : m_eventSinks) {
        if (sink) sink->onSelectionChanged(hasSelection);
    }
    m_notifying = false;
}

void LeapHostBridge::notifyThemeChanged(bool dark) {
    if (m_notifying) return;
    m_notifying = true;
    for (LeapEventSink* sink : m_eventSinks) {
        if (sink) sink->onThemeChanged(dark);
    }
    m_notifying = false;
}

void LeapHostBridge::notifyDocumentLoaded(const QString& filePath) {
    if (m_notifying) return;
    m_notifying = true;
    for (LeapEventSink* sink : m_eventSinks) {
        if (sink) sink->onDocumentLoaded(filePath);
    }
    m_notifying = false;
}
