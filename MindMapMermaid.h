#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class MindMapNode;

// ============================================================
// Mermaid mindmap 导入 / 导出
//
// 设计要点：导入不自己造节点树，而是把 Mermaid 文本翻译成一份
// “MindMapIO 根节点 JSON”，再交给 MindMapIO::deserialize()。这样
// 建块、层级样式、加载期格式保护（isLoading）、进度与取消、公式
// 渲染全部复用已经过测试的那条路径。
//
// 保真度：Mermaid 的圆角/圆形/六边形/Bang/云 等形状、::icon(...)
// 图标、:::类名 逐节点配色、折叠态、图片都没有对应物，导入时会被
// 丢弃并写进 warnings 交给界面提示；$$...$$ 与公式块双向互通。
// 方框 [..] 视作本项目的默认矩形，不算降级。
// ============================================================

struct MermaidParseResult {
    bool ok = false;
    QJsonObject rootJson;   // 可直接传给 MindMapIO::deserialize()
    QStringList warnings;   // 已降级 / 已忽略的内容，给界面展示
    QString errorMessage;   // ok == false 时的原因
};

class MindMapMermaidImporter {
public:
    static MermaidParseResult parse(const QString& text);

    // .mmd / .mermaid
    static bool isMermaidFilePath(const QString& filePath);
    // 文件对话框用：Mermaid 思维导图 (*.mmd *.mermaid);;所有文件 (*)
    static QString fileFilter();

    // “打开”对话框用的组合过滤器：leap 与 Mermaid 都能选。
    // 注意不要用它做保存对话框，否则会把 leap 存成 .mmd。
    static QString openFileFilter();
};

class MindMapMermaidExporter {
public:
    // 遍历节点树生成 Mermaid mindmap 文本。
    // warnings 非空时，无法表达的内容会写成提示条目追加进去。
    static QString toText(MindMapNode* root, QStringList* warnings = nullptr);
};
