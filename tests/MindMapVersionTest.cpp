// 版本号单一真源的回归测试。
//
// 背景：这个项目曾经把版本号抄在 5 个地方（MainWindow.cpp、MindMapIO.cpp、
// leap.rc、build_release.bat、installer/leap.iss），结果源码里的小版本号
// 落后于实际发布的包，联网更新一旦上线就会永远提示"有新版本"。
//
// 现在版本号只写在根目录 VERSION.txt，CMake 生成 LeapVersion.h。本测试负责：
//   1. 校验生成的 LeapVersion.h 内容合法（三段式、无 BOM 残留、数字可解析）；
//   2. 钉死"程序版本"与"文件格式版本"不是同一个东西，防止后人合并。

#include "LeapVersion.h"
#include "MindMapIO.h"

#include <QDebug>
#include <QRegularExpression>
#include <QString>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            qCritical() << "Failed:" << #condition;                            \
            return 1;                                                          \
        }                                                                      \
    } while (false)

static int run() {
    // ---- 1. LEAP_VERSION_STRING 必须是干净的 主.次.修订 ----
    const QString version = QStringLiteral(LEAP_VERSION_STRING);
    CHECK(!version.isEmpty());

    // 不能带 'v' 前缀（显示用带 v，存储与比较一律不带）
    CHECK(!version.startsWith(QLatin1Char('v')));

    // 不能有空白 / BOM / 换行残留（VERSION.txt 被编辑器改坏的典型症状）
    CHECK(version.trimmed() == version);
    CHECK(!version.contains(QLatin1Char(' ')));
    CHECK(!version.contains(QLatin1Char('\n')));
    CHECK(!version.contains(QLatin1Char('\r')));
    CHECK(version.at(0) != QChar(0xFEFF));

    static const QRegularExpression pattern(
        QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(version);
    CHECK(match.hasMatch());

    // ---- 2. 三段宏必须与字符串一致 ----
    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    const int patch = match.captured(3).toInt();

    CHECK(LEAP_VERSION_MAJOR == major);
    CHECK(LEAP_VERSION_MINOR == minor);
    CHECK(LEAP_VERSION_PATCH == patch);

    // 资源文件用的是四段式，第 4 段固定为 0
    CHECK(LEAP_VERSION_BUILD == 0);

    // ---- 3. 显示串带 v 前缀 ----
    const QString display = QStringLiteral(LEAP_VERSION_DISPLAY);
    CHECK(display == QLatin1String("v") + version);

    // ---- 4. 程序版本 ≠ 文件格式版本 ----
    // MindMapIO::CURRENT_VERSION 是 .mindmap 文件的 schema 版本，与产品版本
    // 完全无关。这两个数字一旦相等，说明有人把两者当成同一个东西了。
    CHECK(MindMapIO::CURRENT_VERSION != LEAP_VERSION_MAJOR);
    // 文件格式版本必须是小的正整数，现在存档内容是 7。
    CHECK(MindMapIO::CURRENT_VERSION > 0);
    CHECK(MindMapIO::CURRENT_VERSION < 1000);

    // ---- 5. 主版本号必须 >= 1（防止 VERSION.txt 被写成 0.x 或空行）----
    CHECK(LEAP_VERSION_MAJOR >= 1);

    qInfo() << "leap version:" << version
            << "| file format version:" << MindMapIO::CURRENT_VERSION;
    return 0;
}

int main() { return run(); }
