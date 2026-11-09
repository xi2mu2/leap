#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>

enum class MindMapNodeShape {
    RoundedRect,
    Rect
};

struct MindMapStyle {
    QString fontFamily = QStringLiteral("SimSun");
    int fontPointSize = 14;
    QColor textColor = QColor(5, 56, 96);
    Qt::Alignment alignment = Qt::AlignLeft;
    qreal lineSpacing = 1.0;
    QColor fillColor = QColor(211, 234, 253);
    QColor borderColor = QColor(211, 234, 253);
    bool borderFollowsFill = true;
    int borderWidth = 4;
    MindMapNodeShape shape = MindMapNodeShape::RoundedRect;

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
    MindMapStyle root;
    MindMapStyle firstChild;
    MindMapStyle deeper;
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
