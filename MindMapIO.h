#pragma once

#include <QString>
#include <QJsonObject>
#include <QGraphicsScene>
#include <functional>
#include "MindMapStyle.h"

class MindMapNode;

// 思维导图文件 I/O 封装
class MindMapIO {
public:
    struct LoadProgress {
        enum class Stage { Parsing, BuildingNodes, Finalizing };
        Stage stage = Stage::Parsing;
        int current = 0;
        int total = 0;
        QString detail;
    };
    using LoadProgressCallback = std::function<bool(const LoadProgress&)>;
    static constexpr int CURRENT_VERSION = 7;
    static constexpr const char* FILE_FORMAT = "mindmap";

    static QJsonObject serialize(MindMapNode* root, const QString& idPrefix = "1");
    static MindMapNode* deserialize(const QJsonObject& obj, QGraphicsScene* scene = nullptr,
                                    const LoadProgressCallback& progress = {});
    // errorMessage 非空时，失败原因会写进去（系统错误 + 路径诊断），
    // 便于「保存失败」弹窗把真正的原因告诉用户。
    static bool saveToFile(MindMapNode* root, const QString& filePath,
                           QString* errorMessage = nullptr);
    static MindMapNode* loadFromFile(const QString& filePath, QGraphicsScene* scene = nullptr,
                                     const LoadProgressCallback& progress = {});
    static QString fileFilter();
    static QString ensureExtension(const QString& filePath);

    // 加载（反序列化）期间为 true：applyStyle 此时不清除文件里显式保存的
    // 段落格式（对齐/行距），否则打开文件后用户设过的居中、行距会消失。
    static bool isLoading();

private:
    static int s_loadingDepth;
    struct LoadingScope {
        LoadingScope() { ++s_loadingDepth; }
        ~LoadingScope() { --s_loadingDepth; }
    };
};

