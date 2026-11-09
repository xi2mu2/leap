# leap 思维导图

一款使用 Qt 6 / C++17 编写的桌面思维导图软件，界面简洁，支持数学公式与 PPT 导出。

![leap 界面](docs/mockup-light.png)

## 功能

- 思维导图编辑：节点增删改、拖拽、折叠、多选
- 数学公式渲染：基于 JKQTMathText
- 导出：可将导图导出为 PPT
- 主题：亮色 / 暗色
- 小地图导航、图片节点、自动备份

## 下载

前往 [Releases](../../releases) 页面下载最新版本。

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

## 许可证

本项目以 **GNU General Public License v3.0** 发布，完整条款见 [LICENSE](LICENSE)。

- 你可以自由使用、修改、分发本软件；
- 分发修改后的版本时，必须同样以 GPL-3.0 开放源代码；
- **闭源商业使用需要单独授权**，请联系 1022645285@qq.com。

第三方组件（Qt、JKQTMathText、Fira Math 等）适用各自的许可证，详见 [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)。

## 作者

刘羽林 · 1022645285@qq.com
