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
- Markdown 大纲互操作：可把导图导出成 Obsidian 兼容的 `.md` 无序列表大纲，也能把 `.md` 大纲导入成节点树。导出时若导图里有图片会询问「保留图片 / 仅文字」：保留则在导出位置建同名文件夹，把 `<名称>.md` 与 `image_001.png`…`image_00N.png` 一起放进去，正文用 `![](image_001.png)` 引用（图片与 .md 在同一个 Obsidian vault 内才会显示）；仅文字则写成单文件并把图片降级成 `[图片: 名称]` 占位。公式用 `$$...$$` 围栏，任务列表 `- [x]` / `- [ ]` 导入时降级成 ☑ / ☐ 前缀（不可逆），空节点导出成占位文字「（空节点）」、导入时再还原。这个选择可以在 `设置 → 操作 → 导入 / 导出` 里记住，也可以随时「恢复为每次询问」
- 插件：把插件文件夹拷进 `plugins/` 目录、重启即可生效；插件能在左侧侧边栏加按钮、在「文件」菜单加菜单项、在「设置」里加整页、在「文件 → 导出」里加导出格式，还能读写文档（含整份文档批量改写）、控制缩放与小地图、接收文档变化通知，并能读写自己的配置。管理和开关在「设置 → 插件」里
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

## 插件开发

插件是普通的 Qt 动态库（`.dll`），用 `QPluginLoader` 在启动时加载。宿主只通过一个纯虚
接口 [LeapPluginApi.h](LeapPluginApi.h) 交互，插件不需要、也无法链接 leap 的其它代码。

- 完整指南（接口、生命周期约定、编译要求）：[docs/PLUGINS.md](docs/PLUGINS.md)
- 可编译的示例：
  - [examples/hello-plugin](examples/hello-plugin)——最小插件，演示侧边栏按钮、文件菜单项、一整页设置，以及读写自己的配置
  - [examples/minimap_scale](examples/minimap_scale)——演示视图控制接口：用百分比调整小地图大小
  - [examples/markdown_import](examples/markdown_import)——演示批量改写接口：把 Markdown 一次性转成整份导图

```bash
cmake -S examples/hello-plugin -B build-plugin -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64"
cmake --build build-plugin --config Release
```

把产出的 dll 连同文件夹拷到 leap 的 `plugins/` 目录下，重启 leap 后生效。

> 插件 dll 必须以 **相同的编译器与 Qt 版本**（MinGW 11.2.0 / Qt 6.5.3）编译，
> 否则加载会失败——C++ 没有跨编译器 ABI。

## 许可证

本项目以 **GNU General Public License v3.0** 发布，完整条款见 [LICENSE](LICENSE)。

- 你可以自由使用、修改、分发本软件；
- 分发修改后的版本时，必须同样以 GPL-3.0 开放源代码；
- **闭源商业使用需要单独授权**，请联系 1022645285@qq.com。

第三方组件（Qt、JKQTMathText、Fira Math 等）适用各自的许可证，详见 [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)。

## 作者

刘羽林 · 1022645285@qq.com
