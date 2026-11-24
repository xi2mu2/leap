#ifndef RAINBOWPALETTELIBRARY_H
#define RAINBOWPALETTELIBRARY_H

#include "MindMapStyle.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QVector>

// 用户自己存的彩虹色卡。
//
// 和 MindMapRainbowStyle::presets() 里那 14 套不同，这些是用户点「保存色卡」存下来的，
// 存在 QSettings 里（跟 textLineWidth、themeMode 一样属于界面偏好，不写进 .leap 文档）。
// 色卡名只在本机范围内唯一：重名时由对话框提示改名，这里直接拒绝写入。
class RainbowPaletteLibrary {
public:
    // QSettings 里的键，存一个 JSON 数组的字符串。
    static QString storageKey() { return QStringLiteral("rainbowPalettes"); }

    static QVector<MindMapRainbowPalette> savedPalettes() {
        QVector<MindMapRainbowPalette> result;
        const QString raw = QSettings().value(storageKey()).toString();
        if (raw.isEmpty()) return result;

        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (!doc.isArray()) return result;

        const QJsonArray array = doc.array();
        result.reserve(array.size());
        for (const auto& value : array) {
            const MindMapRainbowPalette palette = MindMapRainbowPalette::fromJson(value.toObject());
            // 名字空或颜色数量不对的存档直接丢掉，宁可少一套也不能出现残缺色卡。
            if (palette.isValid()) {
                result.append(palette);
            }
        }
        return result;
    }

    static void setSavedPalettes(const QVector<MindMapRainbowPalette>& palettes) {
        QJsonArray array;
        for (const auto& palette : palettes) {
            if (palette.isValid()) array.append(palette.toJson());
        }
        QSettings settings;
        if (array.isEmpty()) {
            settings.remove(storageKey());
        } else {
            settings.setValue(storageKey(),
                              QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
        }
    }

    // 是否与已有色卡（含内置预设和其它自定义色卡）重名。
    // exceptName 用于「保存时覆盖自己」这种情况，传空即只查冲突。
    static bool nameExists(const QString& name, const QString& exceptName = QString()) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) return true;
        for (const auto& preset : MindMapRainbowStyle::presets()) {
            if (preset.name == trimmed && preset.name != exceptName) return true;
        }
        for (const auto& palette : savedPalettes()) {
            if (palette.name == trimmed && palette.name != exceptName) return true;
        }
        return false;
    }

    // 存入或替换一套色卡。重名会直接返回 false，由调用方提示用户改名。
    static bool savePalette(MindMapRainbowPalette palette) {
        palette.name = palette.name.trimmed();
        if (!palette.isValid()) return false;
        if (nameExists(palette.name)) return false;
        palette.builtIn = false;

        auto palettes = savedPalettes();
        for (auto& existing : palettes) {
            if (existing.name == palette.name) {
                existing = palette;
                setSavedPalettes(palettes);
                return true;
            }
        }
        palettes.append(palette);
        setSavedPalettes(palettes);
        return true;
    }

    static bool removePalette(const QString& name) {
        auto palettes = savedPalettes();
        const int before = palettes.size();
        for (int i = palettes.size() - 1; i >= 0; --i) {
            if (palettes.at(i).name == name) palettes.remove(i);
        }
        if (palettes.size() == before) return false;
        setSavedPalettes(palettes);
        return true;
    }

    static MindMapRainbowPalette findPalette(const QString& name) {
        for (const auto& palette : savedPalettes()) {
            if (palette.name == name) return palette;
        }
        for (const auto& preset : MindMapRainbowStyle::presets()) {
            if (preset.name == name) return preset;
        }
        return MindMapRainbowPalette{};
    }
};

#endif // RAINBOWPALETTELIBRARY_H
