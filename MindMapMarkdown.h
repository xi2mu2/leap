#pragma once

#include <QImage>
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class MindMapNode;

// ============================================================
// Markdown 大纲（.md）导入 / 导出
//
// 目标格式：Obsidian 兼容的通用 Markdown 无序列表大纲。
//   - 每级一个 `- ` 列表项，缩进 4 空格；
//   - 节点内多行内容用「续行」表达（列表项后续行比 `-` 多缩进 2 空格）；
//   - 公式用 $$...$$ 围栏（导出时首行是 $ 就单独写一个空 `-`，
//     因为 Obsidian 只认行首（可缩进）的 $$）；
//   - 图片用 ![](image_001.png) 引用，带尺寸时写成 ![宽](image_001.png)；
//     图片与 .md 放在同一目录（或同名子文件夹）里；
//   - 任务列表 - [x] / - [ ] 导入时降级成 ☑ / ☐ 前缀，不可逆；
//   - 空节点导出成占位文字（空节点），导入时再还原成空节点。
//
// 与 Mermaid 那条路径一样，导入不自己造节点树，而是产出一份
// “MindMapIO 根节点 JSON”交给 MindMapIO::deserialize()。
// ============================================================

// 需要随 .md 一起写到磁盘的一张图片
struct MarkdownImageAsset {
    QString fileName;   // image_001.png（与 .md 同目录）
    QImage image;
    QSizeF displaySize; // 为空表示按原始尺寸
};

struct MarkdownExportResult {
    QString text;                        // .md 正文
    QVector<MarkdownImageAsset> images;   // 需要调用方写出的图片
    QStringList warnings;                 // 已降级 / 已丢弃的内容
    int imageCount = 0;                   // 树里的图片块总数
};

class MindMapMarkdownExporter {
public:
    // includeImages == false 时只导出文字，图片块变成 `[图片: 名称]` 占位，
    // 并在 warnings 里说明丢了几张。includeImages == true 时把图片收集进
    // images，正文里写 ![](image_00N.png)，文件名由这里统一分配。
    static MarkdownExportResult toMarkdown(MindMapNode* root, bool includeImages);
};

struct MarkdownParseResult {
    bool ok = false;
    QJsonObject rootJson;      // 可直接传给 MindMapIO::deserialize()
    QStringList warnings;      // 已降级 / 已忽略的内容
    QString errorMessage;      // ok == false 时的原因
    int imageTotal = 0;        // 文档里出现的图片引用数
    int imageMissing = 0;      // 找不到图片文件的个数
};

class MindMapMarkdownImporter {
public:
    // baseDir：.md 所在目录，用来解析图片相对路径。
    // includeImages == false 时图片引用降级成 `[图片: 文件名]`。
    // progress：可空；参数是 (已完成, 总数)，返回 false 表示用户取消。
    static MarkdownParseResult parse(const QString& text,
                                     const QString& baseDir,
                                     bool includeImages,
                                     const std::function<bool(int, int)>& progress = {});

    static bool isMarkdownFilePath(const QString& filePath);
    static QString fileFilter();
    static QString openFileFilter();
    // 文档里是否出现图片引用（用于决定要不要问「是否保留图片」）
    static bool containsImageReference(const QString& text);
    static int countImageReferences(const QString& text);
};
