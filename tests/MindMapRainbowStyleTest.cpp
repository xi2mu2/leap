#include "../MindMapStyle.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

namespace {
bool sameColor(const QColor& a, const QColor& b) {
    return a.rgba() == b.rgba();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // 默认配色：正好一个周期，六色互不相同。
    const QVector<QColor> defaults = MindMapRainbowStyle::defaultColors();
    CHECK(defaults.size() == kRainbowBranchPeriod);
    for (int i = 0; i < defaults.size(); ++i) {
        CHECK(defaults.at(i).isValid());
        for (int j = i + 1; j < defaults.size(); ++j) {
            CHECK(!sameColor(defaults.at(i), defaults.at(j)));
        }
    }

    // 取色按序号循环：超出一个周期折回开头，负序号同样折回。
    MindMapRainbowStyle rainbow;
    CHECK(!rainbow.enabled);
    CHECK(rainbow.colors.size() == kRainbowBranchPeriod);
    for (int i = 0; i < defaults.size(); ++i) {
        CHECK(sameColor(rainbow.colorForBranch(i), defaults.at(i)));
        CHECK(sameColor(rainbow.colorForBranch(i + kRainbowBranchPeriod), defaults.at(i)));
        CHECK(sameColor(rainbow.colorForBranch(i - kRainbowBranchPeriod), defaults.at(i)));
    }
    CHECK(sameColor(rainbow.colorForBranch(-1), defaults.at(kRainbowBranchPeriod - 1)));
    CHECK(sameColor(rainbow.colorForBranch(kRainbowBranchPeriod * 3 + 2), defaults.at(2)));
    CHECK(sameColor(rainbow.colorForBranch(13), defaults.at(1)));

    // 空配色时取不到颜色，样式原样返回。
    MindMapRainbowStyle empty;
    empty.colors.clear();
    CHECK(!empty.colorForBranch(0).isValid());
    const MindMapStyle base = MindMapLevelStyles{}.firstChild;
    CHECK(sameColor(empty.styleFor(base, 0, 1).fillColor, base.fillColor));

    // 1 级：填充直接用分支色，白字，边框比分支色深。
    const MindMapStyle first = rainbow.styleFor(base, 0, 1);
    CHECK(sameColor(first.fillColor, QColor(0xC0, 0x39, 0x2B)));
    CHECK(sameColor(first.borderColor, QColor(144, 43, 32)));
    CHECK(sameColor(first.textColor, QColor(0xFF, 0xFF, 0xFF)));
    CHECK(!first.borderFollowsFill);
    CHECK(sameColor(first.effectiveBorderColor(), first.borderColor));

    // 2 级及更深：浅底深字，且 2 级、3 级、更深的颜色完全一致（不逐级变浅）。
    const MindMapStyle deeperBase = MindMapLevelStyles{}.deeper;
    const MindMapStyle second = rainbow.styleFor(deeperBase, 0, 2);
    const MindMapStyle third = rainbow.styleFor(deeperBase, 0, 3);
    const MindMapStyle fourth = rainbow.styleFor(deeperBase, 0, 7);
    CHECK(sameColor(second.fillColor, QColor(246, 225, 223)));
    CHECK(sameColor(second.textColor, QColor(106, 31, 24)));
    CHECK(sameColor(second.fillColor, third.fillColor));
    CHECK(sameColor(second.textColor, third.textColor));
    CHECK(sameColor(second.borderColor, third.borderColor));
    CHECK(sameColor(second.fillColor, fourth.fillColor));
    CHECK(!second.borderFollowsFill);
    // 浅底配深字，保证对比度。
    CHECK(second.fillColor.lightness() > second.textColor.lightness());

    // 同一分支下，1 级与 2 级的填充不同（饱和 / 浅色两档），但同属该分支色。
    CHECK(!sameColor(first.fillColor, second.fillColor));

    // 派生只改三个颜色角色，排版与形状一律沿用基础层级样式。
    MindMapStyle styled = base;
    styled.fontFamily = QStringLiteral("Consolas");
    styled.fontPointSize = 18;
    styled.alignment = Qt::AlignHCenter;
    styled.lineSpacing = 1.5;
    styled.borderWidth = 7;
    styled.shape = MindMapNodeShape::Rect;
    const MindMapStyle derived = rainbow.styleFor(styled, 3, 1);
    CHECK(derived.fontFamily == styled.fontFamily);
    CHECK(derived.fontPointSize == styled.fontPointSize);
    CHECK(derived.alignment == styled.alignment);
    CHECK(derived.lineSpacing == styled.lineSpacing);
    CHECK(derived.borderWidth == styled.borderWidth);
    CHECK(derived.shape == styled.shape);
    CHECK(derived.font().pointSize() == styled.font().pointSize());

    // 每个分支的 1 级填充互不相同。
    for (int i = 0; i < kRainbowBranchPeriod; ++i) {
        for (int j = i + 1; j < kRainbowBranchPeriod; ++j) {
            CHECK(!sameColor(rainbow.styleFor(base, i, 1).fillColor,
                             rainbow.styleFor(base, j, 1).fillColor));
        }
    }

    // 混合系数被钳位在 0..1。
    CHECK(sameColor(MindMapRainbowStyle::mix(QColor(10, 20, 30), QColor(200, 210, 220), -5.0),
                    QColor(10, 20, 30)));
    CHECK(sameColor(MindMapRainbowStyle::mix(QColor(10, 20, 30), QColor(200, 210, 220), 5.0),
                    QColor(200, 210, 220)));

    // 序列化往返：开关与自定义配色都要保住。
    MindMapRainbowStyle custom;
    custom.enabled = true;
    custom.colors = {QColor("#111111"), QColor("#222222"), QColor("#333333"),
                     QColor("#444444"), QColor("#555555"), QColor("#666666")};
    const MindMapRainbowStyle restored = MindMapRainbowStyle::fromJson(custom.toJson());
    CHECK(restored.enabled);
    CHECK(restored.colors.size() == kRainbowBranchPeriod);
    for (int i = 0; i < custom.colors.size(); ++i) {
        CHECK(sameColor(restored.colors.at(i), custom.colors.at(i)));
    }

    // 关闭状态也要往返，且不因为关闭而丢掉配色。
    MindMapRainbowStyle off = custom;
    off.enabled = false;
    const MindMapRainbowStyle offRestored = MindMapRainbowStyle::fromJson(off.toJson());
    CHECK(!offRestored.enabled);
    CHECK(offRestored.colors.size() == kRainbowBranchPeriod);
    CHECK(sameColor(offRestored.colors.at(5), custom.colors.at(5)));

    // 数量不符 / 空对象：退回默认六色，不出现“某些分支没有颜色”。
    QJsonObject broken;
    QJsonArray shortPalette;
    shortPalette.append(QStringLiteral("#111111"));
    shortPalette.append(QStringLiteral("#222222"));
    broken["enabled"] = true;
    broken["colors"] = shortPalette;
    const MindMapRainbowStyle repaired = MindMapRainbowStyle::fromJson(broken);
    CHECK(repaired.enabled);
    CHECK(repaired.colors.size() == kRainbowBranchPeriod);
    for (int i = 0; i < defaults.size(); ++i) {
        CHECK(sameColor(repaired.colors.at(i), defaults.at(i)));
    }

    const MindMapRainbowStyle missing = MindMapRainbowStyle::fromJson(QJsonObject{});
    CHECK(!missing.enabled);
    CHECK(missing.colors.size() == kRainbowBranchPeriod);

    // 非法颜色串会被跳过，数量不足仍回落默认。
    QJsonObject withInvalid;
    QJsonArray mixed;
    mixed.append(QStringLiteral("#123456"));
    mixed.append(QStringLiteral("not-a-color"));
    withInvalid["colors"] = mixed;
    CHECK(MindMapRainbowStyle::fromJson(withInvalid).colors.size() == kRainbowBranchPeriod);

    // 层级样式与彩虹配置并存：序列化后两套都还在，互不擦除。
    MindMapLevelStyles all;
    all.firstChild.fillColor = QColor("#010203");
    all.firstChild.fontPointSize = 33;
    all.rainbow = custom;
    const MindMapLevelStyles allRestored = MindMapLevelStyles::fromJson(all.toJson());
    CHECK(sameColor(allRestored.firstChild.fillColor, QColor("#010203")));
    CHECK(allRestored.firstChild.fontPointSize == 33);
    CHECK(allRestored.rainbow.enabled);
    CHECK(allRestored.rainbow.colors.size() == kRainbowBranchPeriod);
    CHECK(sameColor(allRestored.rainbow.colors.at(0), QColor("#111111")));

    // 关闭彩虹后，层级样式仍原样可取回（这正是"切换回来恢复上次样式"的数据基础）。
    MindMapLevelStyles offAll = allRestored;
    offAll.rainbow.enabled = false;
    CHECK(sameColor(offAll.firstChild.fillColor, QColor("#010203")));
    CHECK(offAll.firstChild.fontPointSize == 33);
    CHECK(offAll.rainbow.colors.size() == kRainbowBranchPeriod);

    // 老文件没有 rainbow 键：读出来就是关闭状态 + 默认配色。
    QJsonObject legacy = all.toJson();
    legacy.remove("rainbow");
    const MindMapLevelStyles legacyRestored = MindMapLevelStyles::fromJson(legacy);
    CHECK(!legacyRestored.rainbow.enabled);
    CHECK(legacyRestored.rainbow.colors.size() == kRainbowBranchPeriod);
    CHECK(sameColor(legacyRestored.firstChild.fillColor, QColor("#010203")));

    // 恢复默认值时不应该改动彩虹配置。
    all.firstChild = MindMapLevelStyles::defaultFor(QStringLiteral("firstChild"));
    CHECK(all.rainbow.enabled);
    CHECK(all.rainbow.colors.size() == kRainbowBranchPeriod);

    // ---- 预设色卡 ----
    const auto presets = MindMapRainbowStyle::presets();
    CHECK(presets.size() == 14);
    for (const auto& preset : presets) {
        CHECK(!preset.name.isEmpty());
        CHECK(preset.colors.size() == kRainbowBranchPeriod);
        // 每套内部六个颜色两两不同，否则相邻分支会看不出区别。
        for (int i = 0; i < preset.colors.size(); ++i) {
            CHECK(preset.colors.at(i).isValid());
            for (int j = i + 1; j < preset.colors.size(); ++j) {
                CHECK(!sameColor(preset.colors.at(i), preset.colors.at(j)));
            }
        }
        // 任意一套都要能读：按用户定的 2:1 下限校验（色彩方案优先，不再为凑 4.5 把字压成深色）。
        // 这里要连预设自带的 branchStyles 一起带上，否则量到的是"没有白字规则"的派生色。
        const MindMapLevelStyles base;
        MindMapRainbowStyle style;
        style.enabled = true;
        style.colors = preset.colors;
        style.branchStyles = preset.branchStyles;
        for (int i = 0; i < preset.colors.size(); ++i) {
            const MindMapStyle l1 = style.styleFor(base.firstChild, i, 1);
            const MindMapStyle l2 = style.styleFor(base.deeper, i, 2);
            // 用户点名要白字的三套（经典彩虹/深靛/赤陶）由预设里的分支规则固定成纯白，
            // 这时对比度可以低于 2:1 —— 是"按用户要求"而不是漏算，所以单独断言白色。
            const MindMapRainbowBranchStyle rule =
                preset.branchStyles.value(MindMapRainbowStyle::branchStyleKey(i, 1));
            if (rule.hasText) {
                CHECK(sameColor(l1.textColor, rule.textColor));
            } else {
                CHECK(MindMapRainbowStyle::contrastRatio(l1.fillColor, l1.textColor) >=
                      kMinBranchTextContrast);
            }
            CHECK(MindMapRainbowStyle::contrastRatio(l2.fillColor, l2.textColor) >=
                  kMinBranchTextContrast);
        }

        // 所有预设的 1 级都是"边框跟随填充"：纯色块，没有可见轮廓。
        for (int i = 0; i < preset.colors.size(); ++i) {
            const MindMapStyle l1 = style.styleFor(base.firstChild, i, 1);
            CHECK(l1.borderFollowsFill);
            CHECK(sameColor(l1.effectiveBorderColor(), l1.fillColor));
        }
    }
    // 预设名字唯一，界面上才分得清。
    for (int i = 0; i < presets.size(); ++i) {
        for (int j = i + 1; j < presets.size(); ++j) {
            CHECK(presets.at(i).name != presets.at(j).name);
        }
    }

    // presetIndex() 认得预设，也认得「自定义」。
    MindMapRainbowStyle picked = MindMapRainbowStyle{};
    picked.colors = presets.at(2).colors;
    CHECK(picked.presetIndex() == 2);
    picked.colors[0] = QColor("#010203");
    CHECK(picked.presetIndex() < 0);

    // 用户点名：经典彩虹 / 深靛 / 赤陶 的 1 级子节点一律纯白字。
    {
        // 必须用 QStringLiteral 而不是 const char*：源码里的中文字面量是 UTF-8，
        // 套 QLatin1String 会按 Latin-1 解码，比较永远不相等。
        const QString whiteNames[] = {QStringLiteral("经典彩虹"), QStringLiteral("深靛"),
                                      QStringLiteral("赤陶")};
        int matched = 0;
        const MindMapLevelStyles base;
        for (const auto& preset : presets) {
            bool wanted = false;
            for (const QString& candidate : whiteNames) {
                if (preset.name == candidate) wanted = true;
            }
            if (!wanted) continue;
            ++matched;
            MindMapRainbowStyle style;
            style.enabled = true;
            style.colors = preset.colors;
            style.branchStyles = preset.branchStyles;
            for (int i = 0; i < preset.colors.size(); ++i) {
                const MindMapStyle l1 = style.styleFor(base.firstChild, i, 1);
                CHECK(sameColor(l1.textColor, QColor(Qt::white)));
                // 2 级浅底仍然走派生的深字，白字规则只管 1 级。
                const MindMapStyle l2 = style.styleFor(base.deeper, i, 2);
                CHECK(!sameColor(l2.textColor, QColor(Qt::white)));
            }
        }
        CHECK(matched == 3);
    }

    // 「单色蓝渐变」整体前移一位，第 6 个补深蓝收口；原来那个几乎与白底同色的 #E7F5FF 已经不用了。
    {
        int gradientIndex = -1;
        for (int i = 0; i < presets.size(); ++i) {
            if (presets.at(i).name == QStringLiteral("单色蓝渐变")) gradientIndex = i;
        }
        CHECK(gradientIndex >= 0);
        const QVector<QColor> expect = {QColor(0xA5, 0xD8, 0xFF), QColor(0x74, 0xC0, 0xFC),
                                        QColor(0x33, 0x9A, 0xF0), QColor(0x1C, 0x7E, 0xD6),
                                        QColor(0x18, 0x64, 0xAB), QColor(0x10, 0x41, 0x6F)};
        const auto& actual = presets.at(gradientIndex).colors;
        CHECK(actual.size() == expect.size());
        for (int i = 0; i < expect.size(); ++i) {
            CHECK(sameColor(actual.at(i), expect.at(i)));
            CHECK(!sameColor(actual.at(i), QColor(0xE7, 0xF5, 0xFF)));
        }
        // 明度单调递增（由浅到深），否则"渐变"就断了。
        for (int i = 1; i < actual.size(); ++i) {
            CHECK(MindMapRainbowStyle::relativeLuminance(actual.at(i)) <
                  MindMapRainbowStyle::relativeLuminance(actual.at(i - 1)));
        }
    }

    // 配色方案名跟着存档走；老文件缺 palette 键时回落到默认名字。
    MindMapRainbowStyle named;
    named.enabled = true;
    int qiulinIndex = -1;
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).name == QStringLiteral("秋林")) qiulinIndex = i;
    }
    CHECK(qiulinIndex >= 0);
    named.paletteName = QStringLiteral("秋林");
    named.colors = presets.at(qiulinIndex).colors;
    const MindMapRainbowStyle namedBack = MindMapRainbowStyle::fromJson(named.toJson());
    CHECK(namedBack.paletteName == QStringLiteral("秋林"));
    CHECK(namedBack.presetIndex() == qiulinIndex);
    QJsonObject noPalette = named.toJson();
    noPalette.remove(QStringLiteral("palette"));
    CHECK(MindMapRainbowStyle::fromJson(noPalette).paletteName == QStringLiteral("宝石"));

    // ---- 逐分支三色覆盖 ----
    // 键编码：1 级是偶数键、2 级及更深是奇数键，同一分支要占两个键。
    CHECK(MindMapRainbowStyle::branchStyleKey(0, 1) == 0);
    CHECK(MindMapRainbowStyle::branchStyleKey(0, 2) == 1);
    CHECK(MindMapRainbowStyle::branchStyleKey(3, 1) == 6);
    CHECK(MindMapRainbowStyle::branchStyleKey(3, 5) == 7);

    {
        MindMapRainbowStyle rainbow = MindMapRainbowStyle{};
        rainbow.enabled = true;
        const MindMapStyle base = MindMapLevelStyles::defaultFor("deeper");

        // 没有覆盖时，两档层级各自派生。
        CHECK(!rainbow.hasAnyBranchStyle());
        const MindMapStyle derived1 = rainbow.styleFor(base, 1, 1);
        const MindMapStyle derived2 = rainbow.styleFor(base, 1, 2);

        // 只覆盖 1 级的填充：同一层级的边框/字色仍派生，另一层级完全不受影响。
        MindMapRainbowBranchStyle fillOnly;
        fillOnly.hasFill = true;
        fillOnly.fillColor = QColor("#112233");
        rainbow.setBranchStyle(1, 1, fillOnly);
        CHECK(rainbow.hasAnyBranchStyle());
        const MindMapStyle overridden1 = rainbow.styleFor(base, 1, 1);
        CHECK(sameColor(overridden1.fillColor, QColor("#112233")));
        CHECK(sameColor(overridden1.borderColor, derived1.borderColor));
        CHECK(sameColor(overridden1.textColor, derived1.textColor));
        CHECK(sameColor(rainbow.styleFor(base, 1, 2).fillColor, derived2.fillColor));

        // 1 级与 2 级互不串色。
        MindMapRainbowBranchStyle darkText;
        darkText.hasText = true;
        darkText.textColor = QColor("#445566");
        rainbow.setBranchStyle(1, 2, darkText);
        CHECK(sameColor(rainbow.styleFor(base, 1, 2).textColor, QColor("#445566")));
        CHECK(sameColor(rainbow.styleFor(base, 1, 1).textColor, derived1.textColor));

        // 不同分支互不影响。
        const MindMapRainbowStyle clean = MindMapRainbowStyle{};
        CHECK(sameColor(rainbow.styleFor(base, 2, 1).fillColor, clean.styleFor(base, 2, 1).fillColor));

        // 覆盖不改变边框跟随填充的规则，也不动字体/字号等布局属性。
        CHECK(!overridden1.borderFollowsFill);
        CHECK(overridden1.fontPointSize == derived1.fontPointSize);

        // 三色全空 == 清除，不会留下占位键。
        rainbow.setBranchStyle(1, 1, MindMapRainbowBranchStyle{});
        CHECK(sameColor(rainbow.styleFor(base, 1, 1).fillColor, derived1.fillColor));
        rainbow.setBranchStyle(1, 2, MindMapRainbowBranchStyle{});
        CHECK(!rainbow.hasAnyBranchStyle());
        CHECK(!rainbow.toJson().contains(QStringLiteral("branches")));

        // 分支级「跟随填充」：只有显式打开的那一支会让边框 = 填充色，
        // 其余分支仍然各自派生，互不干扰。
        {
            MindMapRainbowStyle follow = MindMapRainbowStyle{};
            MindMapRainbowBranchStyle on;
            on.hasBorderFollowsFill = true;
            on.borderFollowsFill = true;
            follow.setBranchStyle(2, 1, on);
            const MindMapStyle followed = follow.styleFor(base, 2, 1);
            CHECK(followed.borderFollowsFill);
            CHECK(sameColor(followed.effectiveBorderColor(), followed.fillColor));
            // 同一分支的 2 级、以及别的分支都不受影响。
            CHECK(!follow.styleFor(base, 2, 2).borderFollowsFill);
            CHECK(!follow.styleFor(base, 3, 1).borderFollowsFill);

            // 关掉之后回到派生边框，覆盖项本身也被清空。
            MindMapRainbowBranchStyle off;
            off.hasBorderFollowsFill = true;
            off.borderFollowsFill = false;
            follow.setBranchStyle(2, 1, off);
            CHECK(!follow.styleFor(base, 2, 1).borderFollowsFill);
            // hasBorderFollowsFill 为真时即使值是 false 也算一条覆盖（明确"关闭"）。
            CHECK(follow.hasAnyBranchStyle());
            follow.setBranchStyle(2, 1, MindMapRainbowBranchStyle{});
            CHECK(!follow.hasAnyBranchStyle());

            // 存档往返：这个开关也要跟着走，否则重新打开文件"跟随填充"就失效了。
            MindMapRainbowStyle savedFollow = MindMapRainbowStyle{};
            savedFollow.enabled = true;
            savedFollow.setBranchStyle(0, 1, on);
            const MindMapRainbowStyle followBack = MindMapRainbowStyle::fromJson(savedFollow.toJson());
            CHECK(followBack.styleFor(base, 0, 1).borderFollowsFill);
            CHECK(sameColor(followBack.styleFor(base, 0, 1).effectiveBorderColor(),
                            followBack.styleFor(base, 0, 1).fillColor));
        }

        // ---- 跨周期：序号折回后必须共用同一份颜色和覆盖 ----
        // 界面只有 6 个色块，用户能调的就是第 1~6 个分支；第 7 个分支起只是颜色重复，
        // 所以它的微调也必须跟着第 1 个分支走，否则会"前 6 个生效、第 7 个开始回预设"。
        {
            MindMapRainbowStyle cycle = MindMapRainbowStyle{};
            cycle.enabled = true;
            CHECK(cycle.wrappedBranchIndex(0) == 0);
            CHECK(cycle.wrappedBranchIndex(5) == 5);
            CHECK(cycle.wrappedBranchIndex(6) == 0);
            CHECK(cycle.wrappedBranchIndex(7) == 1);
            CHECK(cycle.wrappedBranchIndex(12) == 0);
            CHECK(cycle.wrappedBranchIndex(-1) == 5);

            MindMapRainbowBranchStyle mine;
            mine.hasFill = true;
            mine.fillColor = QColor("#0F1E2D");
            // 只改第 1 个分支，第 7 / 13 个分支要跟着变。
            cycle.setBranchStyle(0, 1, mine);
            CHECK(sameColor(cycle.styleFor(base, 0, 1).fillColor, QColor("#0F1E2D")));
            CHECK(sameColor(cycle.styleFor(base, 6, 1).fillColor, QColor("#0F1E2D")));
            CHECK(sameColor(cycle.styleFor(base, 12, 1).fillColor, QColor("#0F1E2D")));
            // 第 2 个分支的覆盖不能漏到第 8 个以外的地方。
            CHECK(!sameColor(cycle.styleFor(base, 1, 1).fillColor, QColor("#0F1E2D")));
            CHECK(!sameColor(cycle.styleFor(base, 7, 1).fillColor, QColor("#0F1E2D")));

            // 反过来：直接给第 8 个分支写覆盖，落在周期内序号 1 上，第 2 个分支也看得到。
            cycle.setBranchStyle(7, 1, mine);
            CHECK(sameColor(cycle.styleFor(base, 1, 1).fillColor, QColor("#0F1E2D")));
            CHECK(sameColor(cycle.styleFor(base, 7, 1).fillColor, QColor("#0F1E2D")));

            // 2 级仍与 1 级分开：跨周期也不能串档。
            MindMapRainbowBranchStyle deep;
            deep.hasFill = true;
            deep.fillColor = QColor("#AABBCC");
            cycle.setBranchStyle(6, 2, deep);
            CHECK(sameColor(cycle.styleFor(base, 6, 2).fillColor, QColor("#AABBCC")));
            CHECK(sameColor(cycle.styleFor(base, 0, 2).fillColor, QColor("#AABBCC")));
            CHECK(sameColor(cycle.styleFor(base, 12, 2).fillColor, QColor("#AABBCC")));
            CHECK(!sameColor(cycle.styleFor(base, 6, 1).fillColor, QColor("#AABBCC")));

            // 折回之后只占 6 个周期位，重建 6 个分支的调色不会撑出 12 份存档。
            CHECK(cycle.branchStyles.size() == 3);  // 键 0、2、1
            CHECK(cycle.toJson().value(QStringLiteral("branches")).toObject().size() == 3);
            const MindMapRainbowStyle cycleBack = MindMapRainbowStyle::fromJson(cycle.toJson());
            CHECK(sameColor(cycleBack.styleFor(base, 12, 1).fillColor, QColor("#0F1E2D")));
            CHECK(sameColor(cycleBack.styleFor(base, 7, 1).fillColor, QColor("#0F1E2D")));

            // 清空也必须按周期位清：清第 7 个分支等于清第 1 个。
            cycle.setBranchStyle(6, 1, MindMapRainbowBranchStyle{});
            CHECK(sameColor(cycle.styleFor(base, 0, 1).fillColor, clean.styleFor(base, 0, 1).fillColor));
        }

        // 序列化往返：覆盖与配色都要保住。
        MindMapRainbowStyle saved = MindMapRainbowStyle{};
        saved.enabled = true;
        saved.paletteName = QStringLiteral("自定义");
        saved.colors = presets.at(0).colors;
        MindMapRainbowBranchStyle all;
        all.hasFill = true;
        all.fillColor = QColor("#0A0B0C");
        all.hasBorder = true;
        all.borderColor = QColor("#1A1B1C");
        all.hasText = true;
        all.textColor = QColor("#2A2B2C");
        saved.setBranchStyle(4, 2, all);
        saved.setBranchStyle(5, 1, fillOnly);
        const QJsonObject savedJson = saved.toJson();
        CHECK(savedJson.contains(QStringLiteral("branches")));
        const MindMapRainbowStyle savedBack = MindMapRainbowStyle::fromJson(savedJson);
        CHECK(savedBack.hasAnyBranchStyle());
        CHECK(savedBack.branchStyles.size() == 2);
        const MindMapStyle backAll = savedBack.styleFor(base, 4, 2);
        CHECK(sameColor(backAll.fillColor, QColor("#0A0B0C")));
        CHECK(sameColor(backAll.borderColor, QColor("#1A1B1C")));
        CHECK(sameColor(backAll.textColor, QColor("#2A2B2C")));
        const MindMapStyle backFillOnly = savedBack.styleFor(base, 5, 1);
        CHECK(sameColor(backFillOnly.fillColor, QColor("#112233")));
        CHECK(sameColor(backFillOnly.textColor, saved.styleFor(base, 5, 1).textColor));

        // 非法键（负数、非数字）被丢弃，不炸不崩。
        QJsonObject messy = savedJson;
        QJsonObject messyBranches = messy.value(QStringLiteral("branches")).toObject();
        messyBranches.insert(QStringLiteral("-1"), all.toJson());
        messyBranches.insert(QStringLiteral("abc"), all.toJson());
        messyBranches.insert(QStringLiteral("999"), QJsonObject{});
        messy[QStringLiteral("branches")] = messyBranches;
        const MindMapRainbowStyle messyBack = MindMapRainbowStyle::fromJson(messy);
        CHECK(messyBack.branchStyles.size() == 2);
        CHECK(!messyBack.branchStyles.contains(-1));
        CHECK(!messyBack.branchStyles.contains(999));
    }

    // ---- 用户色卡（MindMapRainbowPalette）的存档结构 ----
    {
        MindMapRainbowPalette palette;
        palette.name = QStringLiteral("我的配色");
        palette.colors = presets.at(1).colors;
        palette.builtIn = false;
        MindMapRainbowBranchStyle tweak;
        tweak.hasFill = true;
        tweak.fillColor = QColor("#123456");
        palette.branchStyles.insert(MindMapRainbowStyle::branchStyleKey(2, 1), tweak);
        CHECK(palette.isValid());

        const MindMapRainbowPalette back = MindMapRainbowPalette::fromJson(palette.toJson());
        CHECK(back.isValid());
        CHECK(back.name == QStringLiteral("我的配色"));
        CHECK(!back.builtIn);
        CHECK(back.colors.size() == kRainbowBranchPeriod);
        CHECK(back.branchStyles.size() == 1);
        CHECK(sameColor(back.branchStyles.value(MindMapRainbowStyle::branchStyleKey(2, 1)).fillColor,
                        QColor("#123456")));

        // 名字空 / 颜色数量不对 / 完全空的 JSON 都判为无效，不会混进备选列表。
        CHECK(!MindMapRainbowPalette::fromJson(QJsonObject{}).isValid());
        QJsonObject noName = palette.toJson();
        noName.remove(QStringLiteral("name"));
        CHECK(!MindMapRainbowPalette::fromJson(noName).isValid());
        QJsonArray shortColors;
        shortColors.append(QColor("#010203").name(QColor::HexArgb));
        QJsonObject fewColors = palette.toJson();
        fewColors[QStringLiteral("colors")] = shortColors;
        CHECK(!MindMapRainbowPalette::fromJson(fewColors).isValid());
    }
    return 0;
}
