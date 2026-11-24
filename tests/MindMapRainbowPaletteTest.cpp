// 用户自定义色卡库（RainbowPaletteLibrary）的行为测试。
//
// 这里只验证"存取与重名"这套逻辑，不涉及 QtWidgets：色卡本身存在 QSettings 里，
// 测试时把 QSettings 重定向到临时目录的 ini 文件，避免污染真实注册表。
#include "../MindMapStyle.h"
#include "../RainbowPaletteLibrary.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

namespace {

MindMapRainbowPalette makePalette(const QString& name, const QVector<QColor>& colors) {
    MindMapRainbowPalette palette;
    palette.name = name;
    palette.colors = colors;
    palette.builtIn = false;
    return palette;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // 沙箱里注册表可能不可写，而且不该把测试数据写进用户的真实配置。
    QCoreApplication::setOrganizationName(QStringLiteral("leap-test"));
    QCoreApplication::setApplicationName(QStringLiteral("rainbow-palette-test"));

    QTemporaryDir dir;
    CHECK(dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    { QSettings reset; reset.clear(); }

    const auto presets = MindMapRainbowStyle::presets();
    CHECK(!presets.isEmpty());

    // 初始为空。
    CHECK(RainbowPaletteLibrary::savedPalettes().isEmpty());

    // 存一套，能读回来，且颜色与三色覆盖都保住。
    MindMapRainbowPalette mine = makePalette(QStringLiteral("我的配色"), presets.at(1).colors);
    MindMapRainbowBranchStyle tweak;
    tweak.hasFill = true;
    tweak.fillColor = QColor("#123456");
    mine.branchStyles.insert(MindMapRainbowStyle::branchStyleKey(1, 1), tweak);
    CHECK(RainbowPaletteLibrary::savePalette(mine));

    auto saved = RainbowPaletteLibrary::savedPalettes();
    CHECK(saved.size() == 1);
    CHECK(saved.at(0).name == QStringLiteral("我的配色"));
    CHECK(saved.at(0).colors.size() == kRainbowBranchPeriod);
    CHECK(!saved.at(0).builtIn); // 存进来的一定不是"内置"
    CHECK(saved.at(0).branchStyles.size() == 1);

    // 重名（含与内置预设重名）一律拒绝：由对话框提示用户改名。
    CHECK(!RainbowPaletteLibrary::savePalette(makePalette(QStringLiteral("我的配色"), presets.at(2).colors)));
    CHECK(!RainbowPaletteLibrary::savePalette(makePalette(presets.at(0).name, presets.at(2).colors)));
    CHECK(RainbowPaletteLibrary::nameExists(QStringLiteral("我的配色")));
    CHECK(RainbowPaletteLibrary::nameExists(presets.at(0).name));
    CHECK(!RainbowPaletteLibrary::nameExists(QStringLiteral("还没用过这个名字")));
    // 空名字当作已存在，避免存出无名色卡。
    CHECK(RainbowPaletteLibrary::nameExists(QStringLiteral("   ")));

    // findPalette 既认用户色卡也认内置预设。
    CHECK(RainbowPaletteLibrary::findPalette(QStringLiteral("我的配色")).isValid());
    CHECK(RainbowPaletteLibrary::findPalette(presets.at(3).name).isValid());
    CHECK(RainbowPaletteLibrary::findPalette(presets.at(3).name).builtIn);
    CHECK(!RainbowPaletteLibrary::findPalette(QStringLiteral("不存在")).isValid());

    // 再存一套，顺序按存入先后。
    CHECK(RainbowPaletteLibrary::savePalette(makePalette(QStringLiteral("第二套"), presets.at(4).colors)));
    saved = RainbowPaletteLibrary::savedPalettes();
    CHECK(saved.size() == 2);
    CHECK(saved.at(1).name == QStringLiteral("第二套"));

    // 删除只影响指定的一套。
    CHECK(RainbowPaletteLibrary::removePalette(QStringLiteral("我的配色")));
    saved = RainbowPaletteLibrary::savedPalettes();
    CHECK(saved.size() == 1);
    CHECK(saved.at(0).name == QStringLiteral("第二套"));
    CHECK(!RainbowPaletteLibrary::removePalette(QStringLiteral("我的配色"))); // 已经没了

    // 删空后键会被移除，不会留下 "[]" 这种残渣。
    CHECK(RainbowPaletteLibrary::removePalette(QStringLiteral("第二套")));
    CHECK(RainbowPaletteLibrary::savedPalettes().isEmpty());
    {
        QSettings settings;
        CHECK(!settings.contains(RainbowPaletteLibrary::storageKey()));
    }

    // 坏数据不能污染列表：手动塞一条颜色数量不对的存档。
    {
        QJsonArray array;
        MindMapRainbowPalette broken = makePalette(QStringLiteral("残缺"), presets.at(0).colors);
        broken.colors = { QColor("#010203") };
        array.append(broken.toJson());
        QSettings settings;
        settings.setValue(RainbowPaletteLibrary::storageKey(),
                          QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    }
    CHECK(RainbowPaletteLibrary::savedPalettes().isEmpty());

    qInfo() << "Rainbow palette library checks passed";
    return 0;
}
