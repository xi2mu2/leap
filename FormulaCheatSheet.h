#pragma once

#include <QString>
#include <QVector>
#include <QImage>
#include <QDir>

// ============================================================
// LaTeX 公式对照表数据结构
// 运行时直接加载预渲染的 PNG 图片，不实时调用 JKQTMathText
// ============================================================

struct FormulaEntry {
    QString name;       // 显示名称（如"二次方程求根公式"）
    QString latex;      // LaTeX 代码（如"\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"）
    QString category;   // 分类标识（如"basic", "calculus", "matrix"）
    QString desc;       // 简短描述（可选）
};

struct FormulaCategory {
    QString id;         // 标识（如"basic"）
    QString title;      // 显示标题（如"基础运算"）
    QVector<FormulaEntry> entries;
};

class FormulaCheatSheet {
public:
    // 获取所有公式分类（包含 LaTeX 代码，不包含图片）
    static QVector<FormulaCategory> getCategories();

    // 获取预渲染图片的存储目录（相对于可执行文件）
    static QString imageDir();

    // 根据 LaTeX 代码生成唯一的图片文件名（不含路径）
    static QString imageFileName(const QString& latex);

    // 获取某条目的预渲染图片完整路径
    static QString imagePath(const FormulaEntry& entry);

    // 加载某条目的预渲染图片（若不存在返回空图）
    static QImage loadImage(const FormulaEntry& entry);

    // 检查某条目的预渲染图片是否存在
    static bool hasImage(const FormulaEntry& entry);

    // 获取所有条目（扁平化列表）
    static QVector<FormulaEntry> allEntries();
};
