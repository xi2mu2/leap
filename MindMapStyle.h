#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>

enum class MindMapNodeShape {
    RoundedRect,
    Rect
};

// 文本行宽度（px）：QSettings 键 textLineWidth 的缺省值，节点折行与设置对话框共用。
inline constexpr int kDefaultTextLineWidth = 350;

struct MindMapStyle {
    // 新建文档的默认层级样式见 MindMapLevelStyles；这里是与之外层（更深层节点）一致的基础样式，
    // 同时充当旧文件缺字段时的兜底值。
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int fontPointSize = 16;
    QColor textColor = QColor(5, 56, 96);
    Qt::Alignment alignment = Qt::AlignLeft;
    qreal lineSpacing = 1.0;
    QColor fillColor = QColor(211, 234, 253);
    QColor borderColor = QColor(211, 234, 253);
    bool borderFollowsFill = true;
    int borderWidth = 4;
    MindMapNodeShape shape = MindMapNodeShape::RoundedRect;

    // 组装一个完整的层级样式：字体、字号、字色、填充，边框默认跟随填充。
    static MindMapStyle make(const QString& family, int pointSize, const QColor& text, const QColor& fill) {
        MindMapStyle s;
        s.fontFamily = family;
        s.fontPointSize = pointSize;
        s.textColor = text;
        s.fillColor = fill;
        s.borderColor = fill;
        s.borderFollowsFill = true;
        return s;
    }

    QColor effectiveBorderColor() const { return borderFollowsFill ? fillColor : borderColor; }

    QFont font() const { return QFont(fontFamily, fontPointSize); }
    QJsonObject toJson() const {
        QJsonObject o;
        o[QStringLiteral("fontFamily")] = fontFamily;
        o[QStringLiteral("fontPointSize")] = fontPointSize;
        o[QStringLiteral("textColor")] = textColor.name(QColor::HexArgb);
        o[QStringLiteral("alignment")] = int(alignment);
        o[QStringLiteral("lineSpacing")] = lineSpacing;
        o[QStringLiteral("fillColor")] = fillColor.name(QColor::HexArgb);
        o[QStringLiteral("borderColor")] = borderColor.name(QColor::HexArgb);
        o[QStringLiteral("borderFollowsFill")] = borderFollowsFill;
        o[QStringLiteral("borderWidth")] = borderWidth;
        o[QStringLiteral("shape")] = shape == MindMapNodeShape::Rect ? QStringLiteral("rect") : QStringLiteral("roundedRect");
        return o;
    }
    static MindMapStyle fromJson(const QJsonObject& o, const MindMapStyle& fallback) {
        MindMapStyle s = fallback;
        if (o.contains("fontFamily")) s.fontFamily = o.value("fontFamily").toString(s.fontFamily);
        if (o.contains("fontPointSize")) s.fontPointSize = qBound(6, o.value("fontPointSize").toInt(s.fontPointSize), 96);
        if (o.contains("textColor")) s.textColor = QColor(o.value("textColor").toString());
        if (o.contains("alignment")) s.alignment = Qt::Alignment(o.value("alignment").toInt(int(s.alignment)));
        if (o.contains("lineSpacing")) s.lineSpacing = qBound<qreal>(0.5, o.value("lineSpacing").toDouble(s.lineSpacing), 4.0);
        if (o.contains("fillColor")) s.fillColor = QColor(o.value("fillColor").toString());
        if (o.contains("borderColor")) s.borderColor = QColor(o.value("borderColor").toString());
        s.borderFollowsFill = o.contains("borderFollowsFill") ? o.value("borderFollowsFill").toBool(s.borderFollowsFill) : true;
        if (o.contains("borderWidth")) s.borderWidth = qBound(1, o.value("borderWidth").toInt(s.borderWidth), 12);
        if (o.value("shape").toString() == QStringLiteral("rect")) s.shape = MindMapNodeShape::Rect;
        else if (o.contains("shape")) s.shape = MindMapNodeShape::RoundedRect;
        return s;
    }
    static MindMapStyle fromJson(const QJsonObject& o) {
        return fromJson(o, MindMapStyle{});
    }
};

struct MindMapLevelStyles {
    // 新建文档的默认外观：根节点深蓝底白字、1 级稍亮的蓝底白字、2 级浅蓝底深蓝字。
    MindMapStyle root =
        MindMapStyle::make(QStringLiteral("Microsoft YaHei"), 25, QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0x7F));
    MindMapStyle firstChild =
        MindMapStyle::make(QStringLiteral("Microsoft YaHei"), 20, QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0xAB));
    MindMapStyle deeper = MindMapStyle::make(QStringLiteral("Microsoft YaHei"), 16, QColor(0x05, 0x38, 0x60),
                                             QColor(0xD3, 0xEA, 0xFD));

    // 按层级 key（root / firstChild / deeper）取默认样式，供设置对话框的“恢复默认”使用。
    static MindMapStyle defaultFor(const QString& key) {
        const MindMapLevelStyles defaults;
        if (key == QLatin1String("firstChild")) return defaults.firstChild;
        if (key == QLatin1String("deeper")) return defaults.deeper;
        return defaults.root;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o[QStringLiteral("root")] = root.toJson();
        o[QStringLiteral("firstChild")] = firstChild.toJson();
        o[QStringLiteral("deeper")] = deeper.toJson();
        return o;
    }
    static MindMapLevelStyles fromJson(const QJsonObject& o) {
        MindMapLevelStyles s;
        s.root = MindMapStyle::fromJson(o.value("root").toObject(), s.root);
        s.firstChild = MindMapStyle::fromJson(o.value("firstChild").toObject(), s.firstChild);
        s.deeper = MindMapStyle::fromJson(o.value("deeper").toObject(), s.deeper);
        return s;
    }
};
