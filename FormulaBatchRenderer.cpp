#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include "FormulaCheatSheet.h"
#include "MindMapFormulaDialog.h"

// ============================================================
// 公式批量渲染器（开发者工具）
// 运行方式：在项目构建目录中执行此独立程序
// 输出：formula_cheatsheet/ 目录下的 PNG 图片
// 注意：需要 QApplication（而非 QCoreApplication），因为 JKQTMathText 依赖 GUI
// ============================================================

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 确定输出目录
    QString outDir = QDir::current().filePath("formula_cheatsheet");
    QDir dir(outDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    qDebug() << "========================================";
    qDebug() << "LaTeX Formula Batch Renderer";
    qDebug() << "Output directory:" << outDir;
    qDebug() << "========================================";

    auto categories = FormulaCheatSheet::getCategories();
    int total = 0;
    int success = 0;
    int skipped = 0;
    int failed = 0;

    QElapsedTimer timer;
    timer.start();

    for (const auto& cat : categories) {
        qDebug() << "\n--- Category:" << cat.title << "(" << cat.entries.size() << "formulas) ---";

        for (const auto& entry : cat.entries) {
            ++total;
            QString filePath = dir.filePath(FormulaCheatSheet::imageFileName(entry.latex));

            // 检查是否已存在
            if (QFile::exists(filePath)) {
                qDebug() << "  [SKIP]" << entry.name << "-> already exists";
                ++skipped;
                continue;
            }

            // 渲染公式为图片
            QImage image = MindMapFormulaDialog::renderLatexToImage(entry.latex, 18);
            if (image.isNull()) {
                qDebug() << "  [FAIL]" << entry.name << "-> render failed:" << entry.latex.left(60);
                ++failed;
                continue;
            }

            // 保存为 PNG
            if (image.save(filePath, "PNG")) {
                qDebug() << "  [OK]" << entry.name << "->" << QFileInfo(filePath).fileName()
                         << "(" << image.width() << "x" << image.height() << ")";
                ++success;
            } else {
                qDebug() << "  [FAIL]" << entry.name << "-> save failed";
                ++failed;
            }
        }
    }

    qint64 elapsed = timer.elapsed();
    qDebug() << "\n========================================";
    qDebug() << "Done!";
    qDebug() << "Total:" << total;
    qDebug() << "Success:" << success;
    qDebug() << "Skipped:" << skipped;
    qDebug() << "Failed:" << failed;
    qDebug() << "Time:" << elapsed << "ms";
    qDebug() << "Output:" << outDir;
    qDebug() << "========================================";

    return failed > 0 ? 1 : 0;
}
