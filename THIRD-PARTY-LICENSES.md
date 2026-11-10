# 第三方组件与许可证

本项目包含或依赖以下第三方组件。它们适用各自的许可证，**不受本项目 GPL-3.0 覆盖**。

## Qt 6.5.3

- 组件：Core、Widgets、Svg
- 许可证：GNU Lesser General Public License v3.0（LGPL-3.0）
- 主页：https://www.qt.io/
- 本项目以动态链接方式使用 Qt，用户可以自行替换 Qt 运行库。

## JKQTMathText / JKQTCommon

- 位置：`libs/jkqtmathtext`、`libs/jkqtcommon`
- 许可证：GNU Lesser General Public License v2.1 or later（LGPL-2.1-or-later）
- 作者：Jan W. Krieger
- 主页：https://github.com/jkriege2/JKQtPlotter
- 这两个目录中的源文件保留其原始许可证声明，不适用本项目的 GPL-3.0。

> 说明：本仓库将上述两个库的源文件直接编译进可执行文件（静态链接）。
> 由于本项目以完整源代码（含构建脚本）的形式发布，接收者可以修改或替换这两个库后重新编译链接，
> 以此满足 LGPL-2.1 第 6 条关于"可重新链接"的要求。各源文件头部保留了原始的 LGPL 声明。

## Fira Math

- 位置：`libs/jkqtmathtext/resources/firaMath`
- 许可证：SIL Open Font License 1.1
- 许可证全文见该目录下的 `LICENSE` 文件。

## XITS 字体

- 位置：`libs/jkqtmathtext/resources/xits`
- 许可证：SIL Open Font License 1.1
- 许可证全文见该目录下的 `OFL.txt` 文件。
