# leap 思维导图

一款使用 Qt 6 / C++17 编写的桌面思维导图软件，界面简洁，支持数学公式与 PPT 导出。

![leap 界面](docs/mockup-light.png)

## 功能

- 思维导图编辑：节点增删改、拖拽、折叠、多选
- 数学公式渲染：基于 JKQTMathText
- 导出：可将导图导出为 PPT
- 主题：亮色 / 暗色
- 彩虹分支：按 1 级子节点的先后顺序循环取色（六色一周期），每个分支的后续节点沿用同色系；内置 14 套预设色卡（经典彩虹 / 暖橙青紫 / 粉橙深红 / 单色蓝渐变 / 马卡龙淡彩 / 大地色 / 宝石深色 / 粉蓝撞色 / 莫兰迪等），也可以逐分支微调填充 / 边框 / 字体颜色，改完能命名保存成自己的色卡
- Mermaid 互操作：可把导图导出为 `.mmd`，也能把 `.mmd` / `.mermaid` 导入成节点树，导出的文件可直接贴到 mermaid.live 预览；形状、图标、`:::` 类名、逐节点配色与图片是 Mermaid 表达不了的，导入时会忽略并给出提示
- 小地图导航、图片节点、自动备份

## 下载

前往 [Releases](../../releases) 页面下载最新版本：

由于Windows的更新，建议还是用免安装版

- **安装版**：`leap-<版本>-setup.exe`（约 18 MB）—— 双击安装，无需管理员权限，安装路径可改，可选创建桌面快捷方式，可在「应用和功能」中正常卸载；
- **免安装版**：`leap-v<版本>-win64.zip`（约 23 MB）—— 解压后直接运行 `leap.exe`

系统要求：Windows 10 / 11 64 位。

## 从源码构建

```bash
git clone https://github.com/xi2mu2/leap.git
cd leap
```

依赖：

- CMake 3.16+
- Qt 6.5.3（Core、Widgets、Svg）
- MinGW 11.2.0（或 MSVC）

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64"
cmake --build build --config Release
```

### 打包发布

根目录的 `build_release.bat` 会完成整套发布流程：编译 → 部署 Qt 运行时 → 预渲染公式对照表 → 生成免安装 zip → 调用 [Inno Setup](https://jrsoftware.org/isinfo.php) 生成安装包。需要先安装 Inno Setup 6（`winget install JRSoftware.InnoSetup`），安装脚本位于 `installer/leap.iss`。

```bat
build_release.bat
```

产物在 `release/` 目录下：`leap-v<版本>-win64/`（免安装目录）、`.zip` 压缩包、`leap-<版本>-setup.exe` 安装包。

> **版本号只写在根目录的 `VERSION.txt` 文件里**（单行，形如 `2.16.2`）。
> `Version.h`、`leap.rc`、免安装包名、安装包名全部由 CMake / `build_release.bat`
> 从它自动生成，请勿手动改动其它位置的版本号。

> 注意：`MindMapIO::CURRENT_VERSION` 是 **`.mindmap` 文件格式版本**，
> 与产品版本号无关，不要跟着 `VERSION` 一起改。

## 许可证

本项目以 **GNU General Public License v3.0** 发布，完整条款见 [LICENSE](LICENSE)。

- 你可以自由使用、修改、分发本软件；
- 分发修改后的版本时，必须同样以 GPL-3.0 开放源代码；
- **闭源商业使用需要单独授权**，请联系 1022645285@qq.com。

第三方组件（Qt、JKQTMathText、Fira Math 等）适用各自的许可证，详见 [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)。

## 作者

刘羽林 · 1022645285@qq.com
