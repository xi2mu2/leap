#pragma once

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QVector>

#include <cmath>

enum class MindMapNodeShape {
    RoundedRect,
    Rect
};

// 文本行宽度（px）：QSettings 键 textLineWidth 的缺省值，节点折行与设置对话框共用。
inline constexpr int kDefaultTextLineWidth = 350;

// 彩虹分支的取色周期：1 级子节点按序号循环使用配色，满 6 个回到第一个颜色。
inline constexpr int kRainbowBranchPeriod = 6;

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

// 单个分支的三色覆盖。
//
// 三个角色各自记录"是否被用户改过"：没改过的继续用派生值，改过的直接用覆盖值。
// 这样「调过填充但没动字色」的分支，字色仍会自动挑一个读得清的对比色。
struct MindMapRainbowBranchStyle {
    bool hasFill = false;
    bool hasBorder = false;
    bool hasText = false;
    // 彩虹下边框默认不跟随填充，但允许某个分支单独打开"跟随填充"，
    // 所以这个开关也得作为一条覆盖存下来，否则界面勾上了、画布上却没生效。
    bool hasBorderFollowsFill = false;
    bool borderFollowsFill = false;
    QColor fillColor;
    QColor borderColor;
    QColor textColor;

    bool isEmpty() const {
        return !hasFill && !hasBorder && !hasText && !hasBorderFollowsFill;
    }

    void clear() {
        hasFill = hasBorder = hasText = hasBorderFollowsFill = false;
        borderFollowsFill = false;
        fillColor = borderColor = textColor = QColor();
    }

    bool operator==(const MindMapRainbowBranchStyle& other) const {
        return hasFill == other.hasFill && hasBorder == other.hasBorder && hasText == other.hasText &&
               hasBorderFollowsFill == other.hasBorderFollowsFill &&
               borderFollowsFill == other.borderFollowsFill && fillColor == other.fillColor &&
               borderColor == other.borderColor && textColor == other.textColor;
    }
    bool operator!=(const MindMapRainbowBranchStyle& other) const { return !(*this == other); }

    QJsonObject toJson() const {
        QJsonObject o;
        // 只写被覆盖的角色，未覆盖的留空，读回来才知道该重新派生。
        if (hasFill) o[QStringLiteral("fill")] = fillColor.name(QColor::HexArgb);
        if (hasBorder) o[QStringLiteral("border")] = borderColor.name(QColor::HexArgb);
        if (hasText) o[QStringLiteral("text")] = textColor.name(QColor::HexArgb);
        if (hasBorderFollowsFill) o[QStringLiteral("borderFollowsFill")] = borderFollowsFill;
        return o;
    }

    static MindMapRainbowBranchStyle fromJson(const QJsonObject& o) {
        MindMapRainbowBranchStyle s;
        auto readColor = [&o](const char* key, bool* flag, QColor* target) {
            const QColor color(o.value(QLatin1String(key)).toString());
            if (color.isValid()) {
                *flag = true;
                *target = color;
            }
        };
        readColor("fill", &s.hasFill, &s.fillColor);
        readColor("border", &s.hasBorder, &s.borderColor);
        readColor("text", &s.hasText, &s.textColor);
        if (o.contains(QStringLiteral("borderFollowsFill"))) {
            s.hasBorderFollowsFill = true;
            s.borderFollowsFill = o.value(QStringLiteral("borderFollowsFill")).toBool();
        }
        return s;
    }
};

// 一套分支配色：6 个颜色一个周期，正好对应 6 个 1 级子节点。
struct MindMapRainbowPalette {
    QString name;
    QVector<QColor> colors;
    // 预设色卡为 true（不可改名、不可删除）；用户存的色卡为 false。
    bool builtIn = true;
    // 用户在"自定义"状态下微调过的分支三色，键的编码与 MindMapRainbowStyle 一致。
    QMap<int, MindMapRainbowBranchStyle> branchStyles;

    QJsonObject toJson() const {
        QJsonObject o;
        o[QStringLiteral("name")] = name;
        QJsonArray palette;
        for (const QColor& color : colors) {
            palette.append(color.name(QColor::HexArgb));
        }
        o[QStringLiteral("colors")] = palette;
        o[QStringLiteral("builtIn")] = builtIn;
        if (!branchStyles.isEmpty()) {
            QJsonObject overrides;
            for (auto it = branchStyles.cbegin(); it != branchStyles.cend(); ++it) {
                if (it.value().isEmpty()) continue;
                overrides[QString::number(it.key())] = it.value().toJson();
            }
            o[QStringLiteral("branches")] = overrides;
        }
        return o;
    }

    static MindMapRainbowPalette fromJson(const QJsonObject& o) {
        MindMapRainbowPalette p;
        p.name = o.value(QStringLiteral("name")).toString();
        p.builtIn = o.value(QStringLiteral("builtIn")).toBool(false);
        const QJsonArray palette = o.value(QStringLiteral("colors")).toArray();
        for (const auto& value : palette) {
            const QColor color(value.toString());
            if (color.isValid()) p.colors.append(color);
        }
        const QJsonObject overrides = o.value(QStringLiteral("branches")).toObject();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
            bool ok = false;
            const int key = it.key().toInt(&ok);
            if (!ok || key < 0) continue;
            const MindMapRainbowBranchStyle style = MindMapRainbowBranchStyle::fromJson(it.value().toObject());
            if (!style.isEmpty()) p.branchStyles.insert(key, style);
        }
        // 名字空或者颜色数量不对的存档直接判为无效，不塞进备选列表。
        if (p.name.isEmpty() || p.colors.size() != kRainbowBranchPeriod) return MindMapRainbowPalette{};
        return p;
    }

    bool isValid() const {
        return !name.isEmpty() && colors.size() == kRainbowBranchPeriod;
    }
};

// 彩虹分支配色：一支一色，颜色跟着「该分支是根节点的第几个 1 级子节点」走。
//
// 派生规则（用户确认的方案 B）：
//   · 1 级子节点：填充 = 分支色，边框 = 分支色压暗，字色取反差更大的那个（白或深色）；
//   · 2 级及更深：填充 = 分支色调亮，边框 = 分支色更亮一档，字色 = 分支色压黑；
//     同一分支内 2 级、3 级…… 颜色完全一致，不再逐级变浅。
// 字号、字体、对齐、行距、形状、边框粗细仍取自普通层级样式，所以开启彩虹分支不会改变布局。
//
// 预设色值取自 open-color（MIT 许可）的色阶，选哪六个色、怎么编排由本项目自己决定。
struct MindMapRainbowStyle {
    bool enabled = false;
    // 当前配色方案的名字，只给界面显示用；用户逐格改色后置为「自定义」。
    QString paletteName = QStringLiteral("宝石");
    QVector<QColor> colors = defaultColors();

    // 用户逐分支微调过的三色。键 = 分支序号 * 2 + 层级档（0 表示 1 级，1 表示 2 级及更深）。
    // 同一分支的 1 级和 2 级是两套颜色（一个饱和、一个浅），所以要分开记。
    // 空表示全部按派生规则算；某个角色没覆盖时仍然派生，这样改了填充也不会把字色弄糊。
    QMap<int, MindMapRainbowBranchStyle> branchStyles;

    // 默认 6 色：朱红 / 琥珀 / 金褐 / 松绿 / 靛蓝 / 藤紫。
    // 六色都在白字下满足 4.5:1 的正文对比度。
    static QVector<QColor> defaultColors() {
        return {QColor(0xC0, 0x39, 0x2B), QColor(0xA8, 0x5F, 0x12), QColor(0x8A, 0x6A, 0x0E),
                QColor(0x2E, 0x7D, 0x4F), QColor(0x1F, 0x5F, 0xA8), QColor(0x6B, 0x3F, 0xA0)};
    }

    // 预设配色方案，共 14 套：前 10 套对应常见的"配色性格"（供按风格挑），
    // 后 4 套是项目自己配的明度/冷暖区间（宝石为默认）。
    // 色值来源：open-color（MIT）的色阶 + CIELAB 规则生成，选哪六个、怎么排序由本项目决定。
    static QVector<MindMapRainbowPalette> presets() {
        return {
            {QStringLiteral("宝石"), defaultColors()},
            {QStringLiteral("经典彩虹"),
             {QColor(0xF0, 0x3E, 0x3E), QColor(0xFD, 0x7E, 0x14), QColor(0xFC, 0xC4, 0x19),
              QColor(0x40, 0xC0, 0x57), QColor(0x22, 0x8B, 0xE6), QColor(0xBE, 0x4B, 0xDB)}},
            {QStringLiteral("暖橙青紫"),
             {QColor(0xF7, 0x67, 0x07), QColor(0xF5, 0x9F, 0x00), QColor(0xE6, 0x49, 0x80),
              QColor(0x0C, 0xA6, 0x78), QColor(0x5C, 0x7C, 0xFA), QColor(0x9C, 0x36, 0xB5)}},
            {QStringLiteral("粉橙深红"),
             {QColor(0xFC, 0xC2, 0xD7), QColor(0xFF, 0xC0, 0x78), QColor(0xFF, 0xE0, 0x66),
              QColor(0xF0, 0x3E, 0x3E), QColor(0xE8, 0x59, 0x0C), QColor(0xC2, 0x25, 0x5C)}},
            {QStringLiteral("单色蓝渐变"),
             {QColor(0xE7, 0xF5, 0xFF), QColor(0xA5, 0xD8, 0xFF), QColor(0x74, 0xC0, 0xFC),
              QColor(0x33, 0x9A, 0xF0), QColor(0x1C, 0x7E, 0xD6), QColor(0x18, 0x64, 0xAB)}},
            {QStringLiteral("马卡龙淡彩"),
             {QColor(0xFD, 0xCA, 0xD8), QColor(0xAF, 0xFA, 0xFF), QColor(0xEE, 0xDB, 0xAF),
              QColor(0xCC, 0xD5, 0xFF), QColor(0xB6, 0xE2, 0xC0), QColor(0xFF, 0xE8, 0xDE)}},
            {QStringLiteral("大地色"),
             {QColor(0x6D, 0x4C, 0x41), QColor(0xA1, 0x88, 0x7F), QColor(0xD7, 0xCC, 0xC8),
              QColor(0x8D, 0x94, 0x40), QColor(0x5F, 0x71, 0x61), QColor(0xC9, 0xA2, 0x27)}},
            {QStringLiteral("宝石深色"),
             {QColor(0x16, 0x37, 0x6B), QColor(0x3B, 0x2E, 0x76), QColor(0x6B, 0x2A, 0x72),
              QColor(0x96, 0x20, 0x5A), QColor(0xB3, 0x2B, 0x2B), QColor(0x0B, 0x5E, 0x4F)}},
            {QStringLiteral("粉蓝撞色"),
             {QColor(0xFF, 0x6B, 0x6B), QColor(0xFF, 0xA9, 0x4D), QColor(0xF0, 0x65, 0x95),
              QColor(0x22, 0xB8, 0xCF), QColor(0x33, 0x9A, 0xF0), QColor(0x20, 0xC9, 0x97)}},
            {QStringLiteral("莫兰迪"),
             {QColor(0xAB, 0x9E, 0x91), QColor(0x69, 0x87, 0x86), QColor(0xA9, 0xC6, 0xCF),
              QColor(0x89, 0x7D, 0x8F), QColor(0xC9, 0xBD, 0xCF), QColor(0xBE, 0xC3, 0xAF)}},
            {QStringLiteral("深靛"),
             {QColor(0x0B, 0x72, 0x85), QColor(0x08, 0x7F, 0x5B), QColor(0x1C, 0x7E, 0xD6),
              QColor(0x5F, 0x3D, 0xC4), QColor(0x9C, 0x36, 0xB5), QColor(0xC2, 0x25, 0x5C)}},
            {QStringLiteral("赤陶"),
             {QColor(0xC9, 0x2A, 0x2A), QColor(0xD9, 0x48, 0x0F), QColor(0xE6, 0x77, 0x00),
              QColor(0x5C, 0x94, 0x0D), QColor(0xA6, 0x1E, 0x4D), QColor(0x86, 0x2E, 0x9C)}},
            {QStringLiteral("秋林"),
             {QColor(0xFA, 0x52, 0x52), QColor(0xFD, 0x7E, 0x14), QColor(0xFA, 0xB0, 0x05),
              QColor(0x82, 0xC9, 0x1E), QColor(0x40, 0xC0, 0x57), QColor(0x12, 0xB8, 0x86)}},
            {QStringLiteral("晨光"),
             {QColor(0xF0, 0x65, 0x95), QColor(0xBE, 0x4B, 0xDB), QColor(0x79, 0x50, 0xF2),
              QColor(0x22, 0xB8, 0xCF), QColor(0x20, 0xC9, 0x97), QColor(0x82, 0xC9, 0x1E)}},
        };
    }

    // 当前配色与哪个预设一致；都不一致返回 -1（界面上显示为「自定义」）。
    int presetIndex() const {
        const auto all = presets();
        for (int i = 0; i < all.size(); ++i) {
            if (all.at(i).colors == colors) return i;
        }
        return -1;
    }

    static QColor mix(const QColor& from, const QColor& to, qreal amount) {
        const qreal t = qBound<qreal>(0.0, amount, 1.0);
        return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                      qRound(from.green() + (to.green() - from.green()) * t),
                      qRound(from.blue() + (to.blue() - from.blue()) * t));
    }

    // ---- 对比度工具：用来给浅色分支挑一个读得清的字色 ----

    static qreal relativeLuminance(const QColor& color) {
        auto channel = [](qreal value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
               0.0722 * channel(color.blueF());
    }

    static qreal contrastRatio(const QColor& a, const QColor& b) {
        const qreal la = relativeLuminance(a);
        const qreal lb = relativeLuminance(b);
        return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
    }

    // 白字和压深字里挑对比度更高的一个：亮色分支（例如「秋林」的亮黄）配白字会糊掉。
    static QColor readableTextOn(const QColor& background) {
        const QColor white(0xFF, 0xFF, 0xFF);
        const QColor dark = mix(background, QColor(0, 0, 0), 0.88);
        return contrastRatio(background, dark) > contrastRatio(background, white) ? dark : white;
    }

    // 2 级及更深的浅底上写字：从 45% 压深开始，不够就继续往黑压，直到对比度达标。
    // 浅色系分支（马卡龙、粉橙这类）如果固定 45%，字会淡到看不清。
    static QColor readableTintOn(const QColor& background, const QColor& tint) {
        const QColor black(0, 0, 0);
        QColor candidate = mix(tint, black, 0.45);
        for (qreal amount = 0.45; amount <= 0.95; amount += 0.05) {
            candidate = mix(tint, black, amount);
            if (contrastRatio(background, candidate) >= 4.5) return candidate;
        }
        return candidate;
    }

    // 序号超过周期后回到开头的颜色；负序号同样按周期折回。
    QColor colorForBranch(int branchIndex) const {
        const int period = colors.isEmpty() ? 0 : colors.size();
        if (period <= 0) return QColor();
        const int index = ((branchIndex % period) + period) % period;
        return colors.at(index);
    }

    // 分支覆盖在表里的键：分支序号 × 2 + 层级档（1 级为 0，2 级及更深为 1）。
    static int branchStyleKey(int branchIndex, int level) {
        return branchIndex * 2 + (level <= 1 ? 0 : 1);
    }

    // 取某个分支某档层级的三色覆盖；没有则返回空覆盖（全部角色都走派生）。
    MindMapRainbowBranchStyle branchStyle(int branchIndex, int level) const {
        return branchStyles.value(branchStyleKey(branchIndex, level));
    }

    // 写入覆盖；三个角色都为空的覆盖直接丢掉，避免留下"占位但没改过"的垃圾键。
    void setBranchStyle(int branchIndex, int level, const MindMapRainbowBranchStyle& style) {
        const int key = branchStyleKey(branchIndex, level);
        if (style.isEmpty()) {
            branchStyles.remove(key);
        } else {
            branchStyles.insert(key, style);
        }
    }

    bool hasAnyBranchStyle() const {
        for (auto it = branchStyles.cbegin(); it != branchStyles.cend(); ++it) {
            if (!it.value().isEmpty()) return true;
        }
        return false;
    }

    // 把分支色套到某个层级的基础样式上，只改三个颜色角色。
    // level 用 1 表示 1 级子节点，2 表示 2 级及更深。
    MindMapStyle styleFor(const MindMapStyle& base, int branchIndex, int level) const {
        MindMapStyle style = base;
        const QColor branch = colorForBranch(branchIndex);
        if (!branch.isValid()) return style;
        if (level <= 1) {
            style.fillColor = branch;
            style.borderColor = mix(branch, QColor(0, 0, 0), 0.25);
            style.textColor = readableTextOn(branch);
        } else {
            style.fillColor = mix(branch, QColor(0xFF, 0xFF, 0xFF), 0.85);
            style.borderColor = mix(branch, QColor(0xFF, 0xFF, 0xFF), 0.62);
            style.textColor = readableTintOn(style.fillColor, branch);
        }

        // 用户微调过的角色盖掉派生值；没改过的仍然派生。
        const MindMapRainbowBranchStyle override = branchStyle(branchIndex, level);
        if (override.hasFill) style.fillColor = override.fillColor;
        if (override.hasBorder) style.borderColor = override.borderColor;
        if (override.hasText) style.textColor = override.textColor;

        // 彩虹模式下边框默认由分支色派生；但某个分支可以单独打开"跟随填充"，
        // 这时边框就等于该分支的填充色（和层级样式里的语义一致）。
        style.borderFollowsFill = override.hasBorderFollowsFill && override.borderFollowsFill;
        return style;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o[QStringLiteral("enabled")] = enabled;
        o[QStringLiteral("palette")] = paletteName;
        QJsonArray palette;
        for (const QColor& color : colors) {
            palette.append(color.name(QColor::HexArgb));
        }
        o[QStringLiteral("colors")] = palette;

        if (!branchStyles.isEmpty()) {
            // 键是整数，JSON 对象只能用字符串键，所以重新拼一遍（"0"/"1"/…）。
            QJsonObject overrides;
            for (auto it = branchStyles.cbegin(); it != branchStyles.cend(); ++it) {
                if (it.value().isEmpty()) continue;
                overrides[QString::number(it.key())] = it.value().toJson();
            }
            o[QStringLiteral("branches")] = overrides;
        }
        return o;
    }

    static MindMapRainbowStyle fromJson(const QJsonObject& o) {
        MindMapRainbowStyle s;
        s.enabled = o.value("enabled").toBool(false);
        s.paletteName = o.value("palette").toString(s.paletteName);
        const QJsonArray palette = o.value("colors").toArray();
        QVector<QColor> colors;
        colors.reserve(palette.size());
        for (const auto& value : palette) {
            const QColor color(value.toString());
            if (color.isValid()) colors.append(color);
        }
        // 只有正好一个周期的配色才算数；明显损坏的数据（空、数量不符）退回默认六色，
        // 避免出现“某些分支没有颜色”的状态。
        if (colors.size() == kRainbowBranchPeriod) {
            s.colors = colors;
        }

        const QJsonObject overrides = o.value("branches").toObject();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
            bool ok = false;
            const int key = it.key().toInt(&ok);
            if (!ok || key < 0) continue;
            const MindMapRainbowBranchStyle style = MindMapRainbowBranchStyle::fromJson(it.value().toObject());
            if (!style.isEmpty()) s.branchStyles.insert(key, style);
        }
        return s;
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

    // 彩虹分支配置与上面的层级样式并存：开启时层级样式原样保留（只是被分支色盖住显示），
    // 关闭后立刻恢复成上次的层级样式，两套设置互不擦除。
    MindMapRainbowStyle rainbow;

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
        o[QStringLiteral("rainbow")] = rainbow.toJson();
        return o;
    }
    static MindMapLevelStyles fromJson(const QJsonObject& o) {
        MindMapLevelStyles s;
        s.root = MindMapStyle::fromJson(o.value("root").toObject(), s.root);
        s.firstChild = MindMapStyle::fromJson(o.value("firstChild").toObject(), s.firstChild);
        s.deeper = MindMapStyle::fromJson(o.value("deeper").toObject(), s.deeper);
        s.rainbow = MindMapRainbowStyle::fromJson(o.value("rainbow").toObject());
        return s;
    }
};
