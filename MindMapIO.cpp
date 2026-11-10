#include "MindMapIO.h"
#include "MindMapFormulaDialog.h"
#include "MindMapNode.h"
#include "MindMapImageItem.h"
#include "MindMapBlock.h"
#include "PerformanceMetrics.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>

int MindMapIO::s_loadingDepth = 0;

bool MindMapIO::isLoading() { return s_loadingDepth > 0; }
static int countNodes(const QJsonObject& obj) {
    int count = 1;
    for (const auto& child : obj.value("children").toArray()) {
        count += countNodes(child.toObject());
    }
    return count;
}


static const QString WATERMARK_AUTHOR = QStringLiteral("刘羽林");
static const QString WATERMARK_APP = QStringLiteral("leap");
static const QString WATERMARK_VERSION = QStringLiteral("v2.14.10");

static QString imageToBase64(const QImage& image) {
    PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::SaveImageEncode);
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");  // 【修改】去掉压缩质量参数，保存完整无损图片
    return QString::fromLatin1(ba.toBase64());
}

static QImage base64ToImage(const QString& base64) {
    QByteArray data = QByteArray::fromBase64(base64.toLatin1());
    QImage image;
    image.loadFromData(data);
    return image;
}

QJsonObject MindMapIO::serialize(MindMapNode* root, const QString& idPrefix) {
    QJsonObject obj;
    if (root->hasSerializationCache()) {
        obj = root->serializationCache();
    } else {
        // 【新增】保存用户拖动过的节点宽度
        if (root->textBlockWidth() > 0) {
            obj["textBlockWidth"] = root->textBlockWidth();
        }

        // 序列化 Block 列表
        QJsonArray blocksArray;
        for (auto* block : root->blocks()) {
            QJsonObject blockObj;

            switch (block->type()) {
            case BlockType::Text: {
                auto* tb = static_cast<TextBlock*>(block);
                blockObj["type"] = "text";
                blockObj["text"] = tb->toPlainText();

                // 检查是否需要保存 HTML（有富文本格式）
                QString html = tb->toHtml();
                bool needsHtml = false;
                QTextDocument* doc = tb->textItem()->document();
                for (QTextBlock paragraph = doc->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
                    if (paragraph.blockFormat() != QTextBlockFormat()) needsHtml = true;
                    for (auto it = paragraph.begin(); !it.atEnd(); ++it) {
                        QTextFragment fragment = it.fragment();
                        if (fragment.isValid() && fragment.charFormat() != QTextCharFormat()) {
                            needsHtml = true;
                            break;
                        }
                    }
                }
                // Keep HTML for any non-plain formatting; string-based style whitelists miss
                // font family/size, vertical alignment, and future QTextDocument formats.
                if (html.contains("<ul", Qt::CaseInsensitive) || html.contains("<ol", Qt::CaseInsensitive) ||
                    html.contains("<table", Qt::CaseInsensitive) || html.contains("<a ", Qt::CaseInsensitive))
                    needsHtml = true;

                if (needsHtml) {
                    blockObj["html"] = html;
                }
                // 【新增】保存文本块的固定宽度
                if (tb->textItem()->hasFixedWidth()) {
                    blockObj["textWidth"] = tb->textItem()->fixedTextWidth();
                }
                break;
            }
            case BlockType::Formula: {
                auto* fb = static_cast<FormulaBlock*>(block);
                blockObj["type"] = "formula";
                blockObj["latex"] = fb->latexCode();
                break;
            }
            case BlockType::Image: {
                auto* ib = static_cast<ImageBlock*>(block);
                blockObj["type"] = "image";
                blockObj["name"] = ib->imageName();
                blockObj["width"] = ib->width();
                blockObj["height"] = ib->height();
                blockObj["data"] = imageToBase64(ib->originalImage());
                break;
            }
            }

            blocksArray.append(blockObj);
        }
        obj["blocks"] = blocksArray;

        // 兼容旧版：同时保存 text 字段
        obj["text"] = root->allText();
        // Cache only local content. Tree structure and metadata are rebuilt below.
        root->setSerializationCache(obj);
    }
    obj["id"] = idPrefix;
    obj["expanded"] = root->isExpanded();
    obj["version"] = CURRENT_VERSION;
    if (!root->parentNode()) {
        obj["styles"] = static_cast<const MindMapNode*>(root)->levelStyles().toJson();
    }

    // 水印
    if (idPrefix == QStringLiteral("1")) {
        QString copyright = QString("Created by %1 with %2 %3")
            .arg(WATERMARK_AUTHOR, WATERMARK_APP, WATERMARK_VERSION);
        QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QByteArray payload = (copyright + timestamp).toUtf8();
        QByteArray signature = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
        obj["_copyright"] = copyright;
        obj["_timestamp"] = timestamp;
        obj["_signature"] = QString(signature.toHex());
    }

    QJsonArray children;
    auto childNodes = root->childNodes();
    for (int i = 0; i < childNodes.size(); ++i) {
        QString childId = idPrefix + "." + QString::number(i + 1);
        children.append(serialize(childNodes[i], childId));
    }
    obj["children"] = children;
    return obj;
}


static MindMapNode* deserializeNode(const QJsonObject& obj, QGraphicsScene* scene,
                                   const MindMapIO::LoadProgressCallback& progress,
                                   int totalNodes, int& currentNode, bool& cancelled) {
    if (cancelled) return nullptr;
    ++currentNode;
    if (progress && !progress({MindMapIO::LoadProgress::Stage::BuildingNodes,
                               currentNode, totalNodes, QStringLiteral("正在创建节点与内容")})) {
        cancelled = true;
        return nullptr;
    }
    if (currentNode % 32 == 0) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    bool expanded = obj.value("expanded").toBool(true);

    auto* node = new MindMapNode("", nullptr);
    if (obj.contains("styles")) {
        node->setLevelStyles(MindMapLevelStyles::fromJson(obj.value("styles").toObject()));
    }
    // 【修复】先不设置 expanded，等子树全部挂载完毕后再统一应用
    if (scene) scene->addItem(node);

    // 加载 blocks
    QJsonArray blocksArray = obj["blocks"].toArray();
    if (!blocksArray.isEmpty()) {
        // 清除默认创建的文本块
        node->clearBlocks();

        for (const auto& blockVal : blocksArray) {
            auto blockObj = blockVal.toObject();
            QString type = blockObj["type"].toString();

            if (type == "text") {
                TextBlock* tb = new TextBlock(node);
                if (blockObj.contains("html")) {
                    tb->setHtml(blockObj["html"].toString());
                } else {
                    tb->setPlainText(blockObj["text"].toString());
                }
                // 【新增】恢复文本块的固定宽度
                if (blockObj.contains("textWidth")) {
                    qreal tw = blockObj["textWidth"].toDouble();
                    if (tw > 0) {
                        tb->textItem()->setFixedTextWidth(tw);
                    }
                }
                node->addBlock(tb);
            } else if (type == "formula") {
                QString latex = blockObj["latex"].toString();
                if (!latex.isEmpty()) {
                    QImage image = MindMapFormulaDialog::renderLatexToImage(latex, 16);
                    if (!image.isNull()) {
                        QPixmap pixmap = QPixmap::fromImage(image);
                        pixmap.setDevicePixelRatio(image.devicePixelRatio());
                        FormulaBlock* fb = new FormulaBlock(pixmap, latex, node);
                        node->addBlock(fb);
                        fb->pixmapItem()->setFormulaBlock(fb);
                    }
                }
            } else if (type == "image") {
                QImage image = base64ToImage(blockObj["data"].toString());
                if (!image.isNull()) {
                    ImageBlock* ib = new ImageBlock(image, node);
                    ib->setImageName(blockObj["name"].toString());
                    qreal w = blockObj["width"].toDouble();
                    qreal h = blockObj["height"].toDouble();
                    if (w > 0 && h > 0) {
                        ib->setImageSize(QSizeF(w, h));
                    }
                    node->addBlock(ib);
                    ib->imageItem()->setImageBlock(ib);
                }
            }
        }
    } else {
        // 兼容旧版：没有 blocks 字段时，从 text 字段加载
        QString text = obj["text"].toString();
        if (!text.isEmpty()) {
            node->clearBlocks();
            TextBlock* tb = new TextBlock(node);
            tb->setPlainText(text);
            node->addBlock(tb);
        }
    }

    QJsonArray children = obj["children"].toArray();
    for (const auto& childVal : children) {
        auto childObj = childVal.toObject();
        auto* child = deserializeNode(childObj, scene, progress, totalNodes, currentNode, cancelled);
        if (cancelled) {
            delete node;
            return nullptr;
        }
        if (child) node->addChild(child);
    }

    node->applyStyle(node->styleForCurrentLevel());

    // 【新增】恢复用户拖动过的节点宽度
    if (obj.contains("textBlockWidth")) {
        node->setTextBlockWidth(obj["textBlockWidth"].toDouble());
    }
    // 【修复】在所有子节点都挂载到树后，再应用折叠状态
    node->setExpanded(expanded);
    if (progress && currentNode == totalNodes &&
        !progress({MindMapIO::LoadProgress::Stage::Finalizing, 1, 1,
                   QStringLiteral("正在完成内容恢复")})) {
        cancelled = true;
        delete node;
        return nullptr;
    }
    return node;
}

// Install caches after the entire tree is attached and styled: loading can emit
// document changes and reapply styles while attaching descendants.
static void cacheLoadedContent(MindMapNode* node, const QJsonObject& obj) {
    const QJsonArray blocks = obj.value("blocks").toArray();
    bool compatible = obj.value("version").toInt() == MindMapIO::CURRENT_VERSION
        && !blocks.isEmpty() && blocks.size() == node->blocks().size();
    for (const auto& value : blocks) {
        const QString type = value.toObject().value("type").toString();
        compatible = compatible && (type == "text" || type == "image" || type == "formula");
    }
    if (compatible) {
        QJsonObject local = obj;
        for (const char* key : {"id", "version", "children", "expanded", "styles",
                                "_copyright", "_timestamp", "_signature"}) {
            local.remove(QLatin1String(key));
        }
        node->setSerializationCache(local);
    }
    const auto children = node->childNodes();
    const QJsonArray sourceChildren = obj.value("children").toArray();
    for (int i = 0; i < children.size(); ++i) {
        cacheLoadedContent(children[i], sourceChildren[i].toObject());
    }
    if (node->childNodes().size() != sourceChildren.size()) {
        node->invalidateSerializationCache();
    }
}

MindMapNode* MindMapIO::deserialize(const QJsonObject& obj, QGraphicsScene* scene,
                                    const LoadProgressCallback& progress) {
    LoadingScope loadingScope;
    const int totalNodes = countNodes(obj);
    int currentNode = 0;
    bool cancelled = false;
    MindMapNode* node = deserializeNode(obj, scene, progress, totalNodes, currentNode, cancelled);
    if (cancelled) {
        if (scene) scene->clear();
        return nullptr;
    }
    cacheLoadedContent(node, obj);
    return node;
}

bool MindMapIO::saveToFile(MindMapNode* root, const QString& filePath) {
    PerformanceMetrics::ScopedTimer totalTimer(PerformanceMetrics::Stage::SaveTotal);
    if (!root) return false;

    QJsonObject doc;
    doc["version"] = CURRENT_VERSION;
    doc["format"] = FILE_FORMAT;
    {
        PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::SaveSerialize);
        doc["root"] = serialize(root);
    }

    QJsonDocument jsonDoc(doc);
    QByteArray jsonData;
    {
        PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::SaveJsonEncode);
        jsonData = jsonDoc.toJson(QJsonDocument::Indented);
    }
    {
        PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Stage::SaveFileWrite);
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        file.write(jsonData);
    }
    return true;
}

MindMapNode* MindMapIO::loadFromFile(const QString& filePath, QGraphicsScene* scene,
                                     const LoadProgressCallback& progress) {
    LoadingScope loadingScope;
    auto report = [&](LoadProgress::Stage stage, int current, int total, const QString& detail) {
        return !progress || progress({stage, current, total, detail});
    };
    if (!report(LoadProgress::Stage::Parsing, 0, 1, QStringLiteral("正在读取思维导图文件"))) return nullptr;
    QElapsedTimer elapsed;
    elapsed.start();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;

    QByteArray data = file.readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) return nullptr;

    QJsonObject doc = jsonDoc.object();
    if (doc["format"].toString() != QString(FILE_FORMAT)) return nullptr;

    QJsonObject rootObj = doc["root"].toObject();
    if (rootObj.isEmpty()) return nullptr;

    if (elapsed.elapsed() >= 400 &&
        !report(LoadProgress::Stage::Parsing, 1, 1, QStringLiteral("文件读取完成"))) return nullptr;
    return deserialize(rootObj, scene, progress);
}

QString MindMapIO::fileFilter() {
    return "leap files (*.leap);;All files (*)";
}

QString MindMapIO::ensureExtension(const QString& filePath) {
    if (filePath.endsWith(".leap", Qt::CaseInsensitive)) return filePath;
    return filePath + ".leap";
}

