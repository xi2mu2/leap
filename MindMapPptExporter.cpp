#include "MindMapPptExporter.h"
#include "MindMapBlock.h"
#include "MindMapNode.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

namespace {
constexpr qint64 SlideWidthEmu = 9144000;
constexpr qint64 SlideHeightEmu = 5143500;
constexpr qint64 EmuPerPoint = 12700;

constexpr const char* BackgroundColor = "000000";
constexpr const char* PrimaryTextColor = "FFFFFF";

struct ZipEntry {
    QString name;
    QByteArray data;
    quint32 crc = 0;
    quint32 offset = 0;
};

struct TopicImage {
    QString mediaName;
    QByteArray data;
    QSize pixelSize;
};

struct Topic {
    MindMapNode* node = nullptr;
    QString title;
    QVector<TopicImage> images;
    QVector<Topic*> children;
};

struct Slide {
    QString title;
    QString shapes;
    QVector<TopicImage> images;
};

struct MediaStore {
    int nextId = 1;
    QHash<QByteArray, TopicImage> byHash;

    TopicImage addPng(const QImage& image) {
        if (image.isNull()) return {};

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");

        const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        if (byHash.contains(hash)) return byHash.value(hash);

        TopicImage ref;
        ref.mediaName = QString("image%1.png").arg(nextId++);
        ref.data = data;
        ref.pixelSize = image.size();
        byHash.insert(hash, ref);
        return ref;
    }
};

quint32 crc32(const QByteArray& data) {
    static quint32 table[256] = {};
    static bool initialized = false;
    if (!initialized) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }

    quint32 c = 0xffffffffU;
    for (uchar byte : data) {
        c = table[(c ^ byte) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffU;
}

void appendLe16(QByteArray& out, quint16 value) {
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void appendLe32(QByteArray& out, quint32 value) {
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

void dosDateTime(quint16& date, quint16& time) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    date = static_cast<quint16>(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    time = static_cast<quint16>((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

bool writeZip(const QString& filePath, QVector<ZipEntry> entries, QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = QString("无法写入文件：%1").arg(filePath);
        return false;
    }

    QByteArray centralDirectory;
    quint16 date = 0;
    quint16 time = 0;
    dosDateTime(date, time);

    for (ZipEntry& entry : entries) {
        entry.crc = crc32(entry.data);
        entry.offset = static_cast<quint32>(file.pos());
        const QByteArray name = entry.name.toUtf8();

        QByteArray localHeader;
        appendLe32(localHeader, 0x04034b50);
        appendLe16(localHeader, 20);
        appendLe16(localHeader, 0x0800);
        appendLe16(localHeader, 0);
        appendLe16(localHeader, time);
        appendLe16(localHeader, date);
        appendLe32(localHeader, entry.crc);
        appendLe32(localHeader, static_cast<quint32>(entry.data.size()));
        appendLe32(localHeader, static_cast<quint32>(entry.data.size()));
        appendLe16(localHeader, static_cast<quint16>(name.size()));
        appendLe16(localHeader, 0);
        localHeader.append(name);
        file.write(localHeader);
        file.write(entry.data);

        QByteArray centralHeader;
        appendLe32(centralHeader, 0x02014b50);
        appendLe16(centralHeader, 20);
        appendLe16(centralHeader, 20);
        appendLe16(centralHeader, 0x0800);
        appendLe16(centralHeader, 0);
        appendLe16(centralHeader, time);
        appendLe16(centralHeader, date);
        appendLe32(centralHeader, entry.crc);
        appendLe32(centralHeader, static_cast<quint32>(entry.data.size()));
        appendLe32(centralHeader, static_cast<quint32>(entry.data.size()));
        appendLe16(centralHeader, static_cast<quint16>(name.size()));
        appendLe16(centralHeader, 0);
        appendLe16(centralHeader, 0);
        appendLe16(centralHeader, 0);
        appendLe16(centralHeader, 0);
        appendLe32(centralHeader, 0);
        appendLe32(centralHeader, entry.offset);
        centralHeader.append(name);
        centralDirectory.append(centralHeader);
    }

    const quint32 centralOffset = static_cast<quint32>(file.pos());
    file.write(centralDirectory);

    QByteArray endRecord;
    appendLe32(endRecord, 0x06054b50);
    appendLe16(endRecord, 0);
    appendLe16(endRecord, 0);
    appendLe16(endRecord, static_cast<quint16>(entries.size()));
    appendLe16(endRecord, static_cast<quint16>(entries.size()));
    appendLe32(endRecord, static_cast<quint32>(centralDirectory.size()));
    appendLe32(endRecord, centralOffset);
    appendLe16(endRecord, 0);
    file.write(endRecord);

    if (file.error() != QFile::NoError) {
        if (errorMessage) *errorMessage = "写入 PPTX 包失败";
        return false;
    }
    return true;
}

QString xmlEscape(QString text) {
    text.replace('&', "&amp;");
    text.replace('<', "&lt;");
    text.replace('>', "&gt;");
    text.replace('"', "&quot;");
    text.replace('\'', "&apos;");
    return text;
}

QString plainText(QString text) {
    text.remove(QRegularExpression("<[^>]*>"));
    text.replace(QChar(0x2028), '\n');
    text.replace(QRegularExpression("[\\t\\r ]+"), " ");
    text.replace(QRegularExpression("\\n\\s*\\n+"), "\n");
    return text.trimmed();
}

QString plainTitle(MindMapNode* node) {
    if (!node) return {};
    QString text = plainText(node->allText());
    if (text.isEmpty()) text = plainText(node->nodeText());
    return text.left(160);
}

MindMapNode* findRoot(QGraphicsScene* scene) {
    if (!scene) return nullptr;
    for (auto* item : scene->items()) {
        if (auto* node = dynamic_cast<MindMapNode*>(item)) {
            if (!node->parentNode()) return node;
        }
    }
    return nullptr;
}

QVector<TopicImage> collectNodeImages(MindMapNode* node, MediaStore& mediaStore) {
    QVector<TopicImage> images;
    if (!node) return images;

    for (MindMapBlock* block : node->blocks()) {
        if (!block) continue;
        if (block->type() == BlockType::Image) {
            auto* imageBlock = static_cast<ImageBlock*>(block);
            TopicImage ref = mediaStore.addPng(imageBlock->originalImage());
            if (!ref.data.isEmpty()) images.append(ref);
        } else if (block->type() == BlockType::Formula) {
            auto* formulaBlock = static_cast<FormulaBlock*>(block);
            if (formulaBlock->pixmapItem()) {
                TopicImage ref = mediaStore.addPng(formulaBlock->pixmapItem()->pixmap().toImage());
                if (!ref.data.isEmpty()) images.append(ref);
            }
        }
    }
    return images;
}

Topic* buildTopic(MindMapNode* node, MediaStore& mediaStore) {
    if (!node) return nullptr;
    Topic* topic = new Topic;
    topic->node = node;
    topic->title = plainTitle(node);
    if (topic->title.isEmpty()) topic->title = "未命名主题";
    topic->images = collectNodeImages(node, mediaStore);
    for (MindMapNode* child : node->childNodes()) {
        if (Topic* childTopic = buildTopic(child, mediaStore)) topic->children.append(childTopic);
    }
    return topic;
}

void deleteTopic(Topic* topic) {
    if (!topic) return;
    for (Topic* child : topic->children) deleteTopic(child);
    delete topic;
}

int depthOf(const Topic* topic) {
    int depth = 0;
    const MindMapNode* node = topic ? topic->node : nullptr;
    while (node && node->parentNode()) {
        ++depth;
        node = node->parentNode();
    }
    return depth;
}

qint64 pt(qreal value) {
    return qRound64(value * EmuPerPoint);
}

qint64 pctX(qreal value) {
    return qRound64(SlideWidthEmu * value);
}

qint64 pctY(qreal value) {
    return qRound64(SlideHeightEmu * value);
}

QString runXml(const QString& text, int fontSize, const QString& color, bool bold) {
    return QString("<a:r><a:rPr lang=\"zh-CN\" sz=\"%1\" b=\"%2\">"
                   "<a:solidFill><a:srgbClr val=\"%3\"/></a:solidFill>"
                   "<a:latin typeface=\"Microsoft YaHei\"/><a:ea typeface=\"Microsoft YaHei\"/>"
                   "</a:rPr><a:t>%4</a:t></a:r>")
        .arg(fontSize * 100)
        .arg(bold ? "1" : "0")
        .arg(color)
        .arg(xmlEscape(text));
}

QString paragraphXml(const QString& text, int fontSize, const QString& color, bool bold,
                     const QString& align = "left", bool bullet = false) {
    QString pPr = QString("<a:pPr marL=\"0\" indent=\"0\" algn=\"%1\">").arg(align);
    if (bullet) {
        pPr = "<a:pPr marL=\"228600\" indent=\"-152400\"><a:buChar char=\"•\"/>";
    }
    return QString("<a:p>%1</a:pPr>%2<a:endParaRPr lang=\"zh-CN\" sz=\"%3\"/></a:p>")
        .arg(pPr)
        .arg(runXml(text, fontSize, color, bold))
        .arg(fontSize * 100);
}

QString cleanParagraphXml(const QString& text, int fontSize, const QString& color, bool bold,
                          const QString& align = "left", bool bullet = false) {
    QString pPr = QString("<a:pPr marL=\"0\" indent=\"0\" algn=\"%1\">").arg(align);
    if (bullet) {
        pPr = QString("<a:pPr marL=\"228600\" indent=\"-152400\"><a:buClr><a:srgbClr val=\"%1\"/></a:buClr><a:buChar char=\"&#8226;\"/>")
                  .arg(color);
    }
    return QString("<a:p>%1</a:pPr>%2<a:endParaRPr lang=\"zh-CN\" sz=\"%3\">"
                   "<a:solidFill><a:srgbClr val=\"%4\"/></a:solidFill>"
                   "<a:latin typeface=\"Microsoft YaHei\"/><a:ea typeface=\"Microsoft YaHei\"/>"
                   "</a:endParaRPr></a:p>")
        .arg(pPr)
        .arg(runXml(text, fontSize, color, bold))
        .arg(fontSize * 100)
        .arg(color);
}

QString textBody(const QVector<QString>& lines, int fontSize, const QString& color, bool bold,
                 const QString& align = "left", bool bullet = false) {
    QString xml;
    for (const QString& line : lines) {
        const QString cleaned = plainText(line);
        if (!cleaned.isEmpty()) xml += cleanParagraphXml(cleaned, fontSize, color, bold, align, bullet);
    }
    if (xml.isEmpty()) xml = cleanParagraphXml(" ", fontSize, color, bold, align, bullet);
    return xml;
}

QString textBox(int id, qint64 x, qint64 y, qint64 w, qint64 h, const QString& body,
                const QString& valign = "top", int insetPt = 0) {
    const qint64 inset = pt(insetPt);
    return QString("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"Text %1\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr>"
                   "<p:spPr><a:xfrm><a:off x=\"%2\" y=\"%3\"/><a:ext cx=\"%4\" cy=\"%5\"/></a:xfrm>"
                   "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/><a:ln><a:noFill/></a:ln></p:spPr>"
                   "<p:txBody><a:bodyPr wrap=\"square\" anchor=\"%6\" lIns=\"%7\" tIns=\"%7\" rIns=\"%7\" bIns=\"%7\">"
                   "<a:normAutofit fontScale=\"70000\" lnSpcReduction=\"20000\"/></a:bodyPr>"
                   "<a:lstStyle/>%8</p:txBody></p:sp>")
        .arg(id).arg(x).arg(y).arg(w).arg(h).arg(valign).arg(inset).arg(body);
}

QString rectShape(int id, qint64 x, qint64 y, qint64 w, qint64 h, const QString& fill, int transparency = 0) {
    const QString alpha = transparency > 0
        ? QString("<a:alpha val=\"%1\"/>").arg(qMax(0, 100000 - transparency * 1000))
        : QString();
    return QString("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"Rect %1\"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                   "<p:spPr><a:xfrm><a:off x=\"%2\" y=\"%3\"/><a:ext cx=\"%4\" cy=\"%5\"/></a:xfrm>"
                   "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                   "<a:solidFill><a:srgbClr val=\"%6\">%7</a:srgbClr></a:solidFill><a:ln><a:noFill/></a:ln></p:spPr></p:sp>")
        .arg(id).arg(x).arg(y).arg(w).arg(h).arg(fill).arg(alpha);
}

QString lineShape(int id, qint64 x1, qint64 y1, qint64 x2, qint64 y2, const QString& color, int widthPt = 1) {
    const qint64 x = qMin(x1, x2);
    const qint64 y = qMin(y1, y2);
    const qint64 w = qMax<qint64>(1, qAbs(x2 - x1));
    const qint64 h = qMax<qint64>(1, qAbs(y2 - y1));
    return QString("<p:cxnSp><p:nvCxnSpPr><p:cNvPr id=\"%1\" name=\"Line %1\"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>"
                   "<p:spPr><a:xfrm flipH=\"%2\" flipV=\"%3\"><a:off x=\"%4\" y=\"%5\"/><a:ext cx=\"%6\" cy=\"%7\"/></a:xfrm>"
                   "<a:prstGeom prst=\"straightConnector1\"><a:avLst/></a:prstGeom>"
                   "<a:ln w=\"%8\"><a:solidFill><a:srgbClr val=\"%9\"/></a:solidFill></a:ln></p:spPr></p:cxnSp>")
        .arg(id)
        .arg(x2 < x1 ? "1" : "0")
        .arg(y2 < y1 ? "1" : "0")
        .arg(x).arg(y).arg(w).arg(h)
        .arg(widthPt * EmuPerPoint)
        .arg(color);
}

QRectF fitImageRect(const QSize& imageSize, qreal nx, qreal ny, qreal nw, qreal nh) {
    const qreal areaW = SlideWidthEmu * nw;
    const qreal areaH = SlideHeightEmu * nh;
    const qreal aspect = imageSize.height() > 0
        ? static_cast<qreal>(imageSize.width()) / imageSize.height()
        : 1.0;
    qreal w = areaW;
    qreal h = w / aspect;
    if (h > areaH) {
        h = areaH;
        w = h * aspect;
    }
    return QRectF(SlideWidthEmu * nx + (areaW - w) / 2.0,
                  SlideHeightEmu * ny + (areaH - h) / 2.0,
                  w,
                  h);
}

QString imageShape(int id, const QString& relId, const TopicImage& image, const QRectF& rect) {
    return QString("<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"%2\" descr=\"%2\"/>"
                   "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
                   "<p:blipFill><a:blip r:embed=\"%3\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                   "<p:spPr><a:xfrm><a:off x=\"%4\" y=\"%5\"/><a:ext cx=\"%6\" cy=\"%7\"/></a:xfrm>"
                   "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>")
        .arg(id)
        .arg(xmlEscape(image.mediaName))
        .arg(relId)
        .arg(qRound64(rect.x()))
        .arg(qRound64(rect.y()))
        .arg(qRound64(rect.width()))
        .arg(qRound64(rect.height()));
}

Slide makeTopicSlide(Topic* topic, const QString& footer, bool cover = false) {
    Slide slide;
    slide.title = topic ? topic->title : QString();
    int id = 2;

    const int fontSize = cover ? 30 : 26;
    slide.shapes += textBox(id++, pctX(0.10), pctY(0.18), pctX(0.80), pctY(0.52),
                            textBody({topic->title}, fontSize, PrimaryTextColor, true, "ctr"),
                            "mid");
    Q_UNUSED(footer);
    return slide;
}

Slide makeBraceListSlide(Topic* topic, const QString& footer) {
    Slide slide;
    slide.title = topic ? topic->title : QString();
    int id = 2;

    slide.shapes += textBox(id++, pctX(0.08), pctY(0.34), pctX(0.25), pctY(0.22),
                            textBody({topic->title}, 32, PrimaryTextColor, true, "ctr"),
                            "mid");

    slide.shapes += textBox(id++, pctX(0.31), pctY(0.11), pctX(0.10), pctY(0.73),
                            textBody({QString("{")}, 150, PrimaryTextColor, false, "ctr"),
                            "mid");

    QVector<QString> lines;
    for (Topic* child : topic->children) lines.append(child->title);

    const int count = qMax(1, lines.size());
    const qreal top = 0.08;
    const qreal bottom = 0.86;
    const qreal slot = (bottom - top) / count;
    const int itemFont = count <= 4 ? 28 : (count <= 6 ? 22 : 17);
    for (int i = 0; i < lines.size(); ++i) {
        const qreal y = top + i * slot;
        slide.shapes += textBox(id++, pctX(0.43), pctY(y), pctX(0.52), pctY(slot * 0.88),
                                textBody({lines[i]}, itemFont, PrimaryTextColor, false, "left"),
                                "mid");
    }

    Q_UNUSED(footer);
    return slide;
}

Slide makeBulletListSlide(Topic* topic, const QString& footer) {
    Slide slide;
    slide.title = topic ? topic->title : QString();
    int id = 2;

    slide.shapes += textBox(id++, pctX(0.08), pctY(0.08), pctX(0.84), pctY(0.15),
                            textBody({topic->title}, 24, PrimaryTextColor, true, "left"),
                            "mid");
    slide.shapes += lineShape(id++, pctX(0.08), pctY(0.245), pctX(0.73), pctY(0.245), PrimaryTextColor, 1);

    QVector<QString> childTitles;
    for (Topic* child : topic->children) childTitles.append(child->title);

    const bool twoColumns = childTitles.size() > 6;
    if (!twoColumns) {
        const int fontSize = childTitles.size() > 4 ? 13 : 15;
        slide.shapes += textBox(id++, pctX(0.10), pctY(0.30), pctX(0.82), pctY(0.62),
                                textBody(childTitles, fontSize, PrimaryTextColor, false, "left", true),
                                "top");
    } else {
        QVector<QString> left;
        QVector<QString> right;
        for (int i = 0; i < childTitles.size(); ++i) {
            (i % 2 == 0 ? left : right).append(childTitles[i]);
        }
        slide.shapes += textBox(id++, pctX(0.08), pctY(0.30), pctX(0.41), pctY(0.62),
                                textBody(left, 12, PrimaryTextColor, false, "left", true),
                                "top");
        slide.shapes += textBox(id++, pctX(0.52), pctY(0.30), pctX(0.41), pctY(0.62),
                                textBody(right, 12, PrimaryTextColor, false, "left", true),
                                "top");
    }

    Q_UNUSED(footer);
    return slide;
}

Slide makeImageTopicSlide(Topic* topic, const QString& footer) {
    Slide slide;
    slide.title = topic ? topic->title : QString();
    slide.images = topic->images;
    int id = 2;

    slide.shapes += textBox(id++, pctX(0.08), pctY(0.08), pctX(0.84), pctY(0.15),
                            textBody({topic->title}, 24, PrimaryTextColor, true, "left"),
                            "mid");
    slide.shapes += lineShape(id++, pctX(0.08), pctY(0.245), pctX(0.73), pctY(0.245), PrimaryTextColor, 1);

    const int count = qMin(4, topic->images.size());
    for (int i = 0; i < count; ++i) {
        const TopicImage& image = topic->images[i];
        const QString relId = QString("rId%1").arg(i + 2);
        QRectF rect;
        if (count == 1) {
            rect = fitImageRect(image.pixelSize, 0.12, 0.30, 0.76, 0.56);
        } else if (count == 2) {
            rect = fitImageRect(image.pixelSize, 0.10 + i * 0.42, 0.31, 0.36, 0.50);
        } else {
            const int row = i / 2;
            const int col = i % 2;
            rect = fitImageRect(image.pixelSize, 0.10 + col * 0.42, 0.30 + row * 0.27, 0.36, 0.23);
        }
        slide.shapes += imageShape(id++, relId, image, rect);
    }

    Q_UNUSED(footer);
    return slide;
}

QString topicFooter(const Topic* topic, int slideNo, const QString& hint = QString()) {
    const QString suffix = hint.isEmpty() ? QString() : QString("，%1").arg(hint);
    if (!topic) return QString("第%1页").arg(slideNo);
    if (depthOf(topic) == 0) return QString("第%1页，展示根节点").arg(slideNo);
    return QString("第%1页，第%2级节点%3").arg(slideNo).arg(depthOf(topic)).arg(suffix);
}

void appendTopicSlides(QVector<Slide>& slides, Topic* topic, int& slideNo) {
    if (!topic) return;

    slides.append(makeTopicSlide(topic, topicFooter(topic, slideNo++, "主题页")));

    if (!topic->images.isEmpty()) {
        slides.append(makeImageTopicSlide(topic, topicFooter(topic, slideNo++, "图片内容")));
    }

    if (!topic->children.isEmpty()) {
        const bool simpleBrace = depthOf(topic) <= 1
            && topic->children.size() <= 6
            && std::all_of(topic->children.cbegin(), topic->children.cend(), [](Topic* child) {
                   return child->title.size() <= 28;
               });
        slides.append(simpleBrace
            ? makeBraceListSlide(topic, topicFooter(topic, slideNo++, "子节点列表"))
            : makeBulletListSlide(topic, topicFooter(topic, slideNo++, "子节点列表")));

        for (Topic* child : topic->children) appendTopicSlides(slides, child, slideNo);
    }
}

QVector<Slide> buildSlides(Topic* root) {
    QVector<Slide> slides;
    if (!root) return slides;

    int slideNo = 1;
    slides.append(makeTopicSlide(root, QString("第%1页，展示根节点").arg(slideNo++), true));

    if (!root->children.isEmpty()) {
        slides.append(makeBraceListSlide(root, topicFooter(root, slideNo++, "根节点列表")));
        for (Topic* child : root->children) appendTopicSlides(slides, child, slideNo);
    }

    Topic ending;
    ending.title = "Thank you";
    slides.append(makeTopicSlide(&ending, QString("第%1页").arg(slideNo++), true));
    return slides;
}

QString contentTypesXml(int slideCount) {
    QString overrides;
    for (int i = 1; i <= slideCount; ++i) {
        overrides += QString("<Override PartName=\"/ppt/slides/slide%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>").arg(i);
    }
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                   "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                   "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                   "<Default Extension=\"png\" ContentType=\"image/png\"/>"
                   "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
                   "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
                   "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
                   "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                   "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
                   "%1</Types>").arg(overrides);
}

QString rootRelsXml() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
           "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
           "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
           "</Relationships>";
}

QString presentationXml(int slideCount) {
    QString ids;
    for (int i = 1; i <= slideCount; ++i) {
        ids += QString("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(255 + i).arg(i);
    }
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                   "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                   "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                   "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId%1\"/></p:sldMasterIdLst>"
                   "<p:sldIdLst>%2</p:sldIdLst><p:sldSz cx=\"%3\" cy=\"%4\" type=\"wide\"/>"
                   "<p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>")
        .arg(slideCount + 1).arg(ids).arg(SlideWidthEmu).arg(SlideHeightEmu);
}

QString presentationRelsXml(int slideCount) {
    QString rels;
    for (int i = 1; i <= slideCount; ++i) {
        rels += QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/slide%1.xml\"/>").arg(i);
    }
    rels += QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>").arg(slideCount + 1);
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%1</Relationships>").arg(rels);
}

QString slideXml(const QString& shapes) {
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                   "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                   "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                   "<p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill></p:bgPr></p:bg>"
                   "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                   "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
                   "%2</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>")
        .arg(BackgroundColor).arg(shapes);
}

QString slideRelsXml(const Slide& slide) {
    QString rels = "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>";
    for (int i = 0; i < slide.images.size(); ++i) {
        rels += QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/%2\"/>")
                    .arg(i + 2)
                    .arg(slide.images[i].mediaName);
    }
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%1</Relationships>").arg(rels);
}

QString slideMasterXml() {
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                   "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                   "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                   "<p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill></p:bgPr></p:bg>"
                   "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                   "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
                   "</p:spTree></p:cSld><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
                   "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
                   "</p:sldMaster>").arg(BackgroundColor);
}

QString slideLayoutXml() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
           "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
           "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" type=\"blank\" preserve=\"1\">"
           "<p:cSld name=\"Blank\"><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
           "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
           "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
}

QString coreXml() {
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                   "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                   "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                   "<dc:title>leap mind map</dc:title><dc:creator>leap</dc:creator><cp:lastModifiedBy>leap</cp:lastModifiedBy>"
                   "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%1</dcterms:created>"
                   "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">%1</dcterms:modified></cp:coreProperties>").arg(now);
}

QString appXml(int slideCount) {
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
                   "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
                   "<Application>leap</Application><PresentationFormat>Wide Screen</PresentationFormat>"
                   "<Slides>%1</Slides><ScaleCrop>false</ScaleCrop></Properties>").arg(slideCount);
}

void addTextEntry(QVector<ZipEntry>& entries, const QString& name, const QString& text) {
    entries.append({name, text.toUtf8()});
}

void addBinaryEntry(QVector<ZipEntry>& entries, const QString& name, const QByteArray& data) {
    entries.append({name, data});
}
}

bool MindMapPptExporter::exportToPptx(QGraphicsScene* scene, const QString& filePath, QString* errorMessage) {
    MediaStore mediaStore;
    Topic* root = buildTopic(findRoot(scene), mediaStore);
    if (!root) {
        if (errorMessage) *errorMessage = "没有可导出的根节点";
        return false;
    }

    const QVector<Slide> slides = buildSlides(root);
    const int slideCount = slides.size();
    if (slideCount == 0) {
        deleteTopic(root);
        if (errorMessage) *errorMessage = "没有可导出的内容";
        return false;
    }

    QVector<ZipEntry> entries;
    addTextEntry(entries, "[Content_Types].xml", contentTypesXml(slideCount));
    addTextEntry(entries, "_rels/.rels", rootRelsXml());
    addTextEntry(entries, "docProps/core.xml", coreXml());
    addTextEntry(entries, "docProps/app.xml", appXml(slideCount));
    addTextEntry(entries, "ppt/presentation.xml", presentationXml(slideCount));
    addTextEntry(entries, "ppt/_rels/presentation.xml.rels", presentationRelsXml(slideCount));
    addTextEntry(entries, "ppt/slideMasters/slideMaster1.xml", slideMasterXml());
    addTextEntry(entries, "ppt/slideMasters/_rels/slideMaster1.xml.rels",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
                 "</Relationships>");
    addTextEntry(entries, "ppt/slideLayouts/slideLayout1.xml", slideLayoutXml());
    addTextEntry(entries, "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/>"
                 "</Relationships>");

    QSet<QString> writtenMedia;
    for (int i = 0; i < slideCount; ++i) {
        const int slideIndex = i + 1;
        addTextEntry(entries, QString("ppt/slides/slide%1.xml").arg(slideIndex), slideXml(slides[i].shapes));
        addTextEntry(entries, QString("ppt/slides/_rels/slide%1.xml.rels").arg(slideIndex), slideRelsXml(slides[i]));
        for (const TopicImage& image : slides[i].images) {
            if (!writtenMedia.contains(image.mediaName)) {
                addBinaryEntry(entries, QString("ppt/media/%1").arg(image.mediaName), image.data);
                writtenMedia.insert(image.mediaName);
            }
        }
    }

    const bool ok = writeZip(filePath, entries, errorMessage);
    deleteTopic(root);
    return ok;
}
