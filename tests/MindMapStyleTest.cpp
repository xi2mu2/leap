#include "../MindMapStyle.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QDebug>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    MindMapStyle style;
    CHECK(style.borderFollowsFill);
    CHECK(style.borderWidth == 4);
    style.fillColor = QColor("#123456");
    style.borderColor = QColor("#abcdef");
    CHECK(style.effectiveBorderColor() == style.fillColor);

    style.borderFollowsFill = false;
    CHECK(style.effectiveBorderColor() == style.borderColor);
    const MindMapStyle restored = MindMapStyle::fromJson(style.toJson());
    CHECK(!restored.borderFollowsFill);
    CHECK(restored.borderWidth == 4);
    CHECK(restored.effectiveBorderColor() == style.borderColor);

    QJsonObject legacy = style.toJson();
    legacy.remove("borderFollowsFill");
    legacy.remove("borderWidth");
    CHECK(MindMapStyle::fromJson(legacy).borderFollowsFill);
    legacy["borderColor"] = legacy["fillColor"];
    CHECK(MindMapStyle::fromJson(legacy).borderFollowsFill);
    CHECK(MindMapStyle::fromJson(legacy).borderWidth == 4);

    legacy["borderWidth"] = 99;
    CHECK(MindMapStyle::fromJson(legacy).borderWidth == 12);
    return 0;
}
