#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QSettings>
#include <QTimer>
#include "MainWindow.h"
#include "StartupDialog.h"

int main(int argc, char *argv[]) {
    // 【关键】在 QApplication 构造前保存原始命令行参数，
    // 避免 Qt 消费掉部分参数后导致文件路径丢失
    QStringList rawArgs;
    for (int i = 1; i < argc; ++i) {
        rawArgs.append(QString::fromLocal8Bit(argv[i]));
    }

    QApplication app(argc, argv);
    // 统一 QSettings 命名空间，确保新手指引完成标记与应用其他设置
    // 保存在同一位置，并在不同启动路径间保持一致。
    QCoreApplication::setOrganizationName(QStringLiteral("刘羽林"));
    QCoreApplication::setApplicationName(QStringLiteral("leap"));

    // 从 Qt 资源系统加载图标（leap.png 已编译进 exe）
    app.setWindowIcon(QIcon(":/leap.png"));

    // 从原始参数中查找要打开的文件路径
    QString openFilePath;
    for (const QString& arg : rawArgs) {
        // 过滤掉 Qt 相关的命令行选项（如 -style, -platform 等）
        if (arg.startsWith("-")) continue;
        // 第一个非选项参数即视为文件路径
        openFilePath = arg;
        break;
    }

    MainWindow window;
    auto showStartupDialog = [&]() {
        while (true) {
            StartupDialog startupDialog(MainWindow::recentFiles());
            if (startupDialog.exec() != QDialog::Accepted) return false;

            if (startupDialog.choice() == StartupDialog::Choice::OpenRecent ||
                startupDialog.choice() == StartupDialog::Choice::OpenOther) {
                if (!window.openFile(startupDialog.selectedFilePath())) continue;
            }
            return true;
        }
    };
    // 如果有拖入的文件，直接同步打开（窗口已初始化完成）
    if (!openFilePath.isEmpty()) {
        if (!window.openFile(openFilePath)) {
            if (!showStartupDialog()) return 0;
        }
    } else {
        // 启动阶段主窗口尚未显示；让启动对话框作为独立顶层窗口显示，确保任务栏有应用图标。
        if (!showStartupDialog()) return 0;
    }
    window.show();
    if (!QSettings().value(QStringLiteral("newUserGuideCompleted"), false).toBool()) {
        QTimer::singleShot(0, &window, &MainWindow::startNewUserGuide);
    }
    return app.exec();
}
