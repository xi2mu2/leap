# 强制指定此项目使用 GCC 11.2.0（与 Qt 6.5.3 兼容）
set(CMAKE_C_COMPILER "C:/Qt/Tools/mingw1120_64/bin/gcc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "C:/Qt/Tools/mingw1120_64/bin/g++.exe" CACHE FILEPATH "" FORCE)

cmake_minimum_required(VERSION 3.16)
project(MindMapQt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 如果本地 Qt 不在 PATH 中，请取消注释并修改为你的 Qt 安装路径
# set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

# ========== 版本号单一真源 ==========
# 版本号只写在根目录的 VERSION 文件里（单行，形如 2.16.2）。
# LeapVersion.h 与 leap.rc 都由它生成，代码里不要再硬编码版本号。
file(READ "${CMAKE_SOURCE_DIR}/VERSION.txt" LEAP_VERSION_RAW)
string(STRIP "${LEAP_VERSION_RAW}" LEAP_VERSION)
if(NOT LEAP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION.txt 格式无效（应为 主.次.修订，例如 2.16.2）：'${LEAP_VERSION}'")
endif()
string(REPLACE "." ";" LEAP_VERSION_PARTS "${LEAP_VERSION}")
list(GET LEAP_VERSION_PARTS 0 LEAP_VERSION_MAJOR)
list(GET LEAP_VERSION_PARTS 1 LEAP_VERSION_MINOR)
list(GET LEAP_VERSION_PARTS 2 LEAP_VERSION_PATCH)
# Windows 资源文件的四段式版本号：2.16.2 -> 2,16,2,0
set(LEAP_VERSION_CSV "${LEAP_VERSION_MAJOR},${LEAP_VERSION_MINOR},${LEAP_VERSION_PATCH},0")

set(LEAP_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${LEAP_GENERATED_DIR}")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/LeapVersion.h.in"
    "${LEAP_GENERATED_DIR}/LeapVersion.h"
    @ONLY
)

message(STATUS "leap version: ${LEAP_VERSION} (${LEAP_VERSION_CSV})")

# 收集 jkqtmathtext 库的所有源文件
file(GLOB_RECURSE JKQTMATHTEXT_SOURCES
    "${CMAKE_SOURCE_DIR}/libs/jkqtmathtext/*.cpp"
)

# 收集 jkqtcommon 库的所有源文件（jkqtmathtext 依赖它）
file(GLOB_RECURSE JKQTCOMMON_SOURCES
    "${CMAKE_SOURCE_DIR}/libs/jkqtcommon/*.cpp"
)

# ========== Release 优化：去除符号表，增加反编译难度 ==========
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -s")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

# ========== Windows 图标资源文件（可选）==========
# leap.rc 也是模板，由 CMake 生成到 <build>/generated/leap.rc，
# 这样 exe 的版本信息与 VERSION 永远一致，不需要手动同步。
set(LEAP_RC_FILE "")
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/leap.ico")
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/leap.ico" LEAP_ICON_PATH)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/leap.rc.in"
        "${LEAP_GENERATED_DIR}/leap.rc"
        @ONLY
    )
    set(LEAP_RC_FILE "${LEAP_GENERATED_DIR}/leap.rc")
    message(STATUS "leap.ico found, will embed into exe")
else()
    message(STATUS "leap.ico not found, skipping icon embedding")
endif()

# ========== 主程序 ==========
# 【修复】添加 WIN32，避免启动时弹出 cmd 黑框
add_executable(leap WIN32
    main.cpp
    MainWindow.cpp
    MainWindow.h
    NewUserGuide.cpp
    NewUserGuide.h
    StartupDialog.cpp
    StartupDialog.h
    FloatingEditToolbar.cpp
    FloatingEditToolbar.h
    IconTextButton.cpp
    IconTextButton.h
    MindMapView.cpp
    MindMapView.h
    MindMapHoverGeometry.h
    MindMapHoverTracker.cpp
    MindMapHoverTracker.h
    PerformanceMetrics.cpp
    PerformanceMetrics.h
    MindMapMiniMap.cpp
    MindMapMiniMap.h
    MindMapPptExporter.cpp
    MindMapPptExporter.h
    MindMapMermaid.cpp
    MindMapMermaid.h
    MindMapMarkdown.cpp
    MindMapMarkdown.h
    MindMapIO.cpp
    MindMapIO.h
    MindMapNode.cpp
    MindMapNode.h
    MindMapNodeGeometry.h
    MindMapSceneRect.h
    MindMapNodeSizeCache.h
    MindMapStyle.h
    RainbowPaletteLibrary.h
    MindMapSettingsDialog.cpp
    MindMapSettingsDialog.h
    SettingsWidgets.cpp
    SettingsWidgets.h
    UiTheme.h
    MindMapBlock.cpp
    MindMapBlock.h
    MindMapImageItem.cpp
    MindMapImageItem.h
    MindMapCommands.cpp
    MindMapCommands.h
    MindMapFormulaDialog.cpp
    MindMapFormulaDialog.h
    FormulaCheatSheet.cpp
    FormulaCheatSheet.h
    LeapPluginApi.h
    PluginManager.cpp
    PluginManager.h
    LeapHostBridge.cpp
    LeapHostBridge.h
    leap.qrc
    ${JKQTMATHTEXT_SOURCES}
    ${JKQTCOMMON_SOURCES}
    ${LEAP_RC_FILE}
)

target_link_libraries(leap Qt6::Core Qt6::Widgets Qt6::Svg)

# LeapVersion.h 由 VERSION 生成在 build/generated 下，主程序需要这个 include 路径。
target_include_directories(leap PRIVATE "${LEAP_GENERATED_DIR}")

include(CTest)
if(BUILD_TESTING)
    # Exercise real node/document/image mutations and file round trips.
    get_target_property(LEAP_CACHE_TEST_SOURCES leap SOURCES)
    list(REMOVE_ITEM LEAP_CACHE_TEST_SOURCES main.cpp leap.qrc ${LEAP_RC_FILE})
    add_executable(mindmap_serialization_cache_test
        tests/MindMapSerializationCacheTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_serialization_cache_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_serialization_cache_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_serialization_cache_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_serialization_cache_test COMMAND mindmap_serialization_cache_test)
    set_tests_properties(mindmap_serialization_cache_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Mermaid 导入/导出：解析与生成只依赖 Qt，但要造节点树验证往返，
    # 因此复用 leap 的源列表（同 mindmap_serialization_cache_test）。
    add_executable(mindmap_mermaid_test
        tests/MindMapMermaidTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_mermaid_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_mermaid_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_mermaid_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_mermaid_test COMMAND mindmap_mermaid_test)
    set_tests_properties(mindmap_mermaid_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Markdown 大纲导入/导出：验证层级解析、公式围栏、任务列表降级与图片往返。
    add_executable(mindmap_markdown_test
        tests/MindMapMarkdownTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_markdown_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_markdown_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_markdown_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_markdown_test COMMAND mindmap_markdown_test)
    set_tests_properties(mindmap_markdown_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # 布局稳定性：layoutTree()/adjustLayout() 必须幂等，且规范原点与树之前
    # 的位置无关。设置刷新、撤销/重做、收尾编辑都依赖这条，否则画面会在两套
    # 绝对坐标之间跳变。
    add_executable(mindmap_layout_stability_test
        tests/MindMapLayoutStabilityTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_layout_stability_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_layout_stability_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_layout_stability_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_layout_stability_test COMMAND mindmap_layout_stability_test)
    set_tests_properties(mindmap_layout_stability_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # 视野稳定性：结构性重排（设置刷新、收尾编辑、撤销/重做）不能改变
    # 内容在屏幕上的位置，否则用户会看到画面跳变。
    add_executable(mindmap_viewport_stability_test
        tests/MindMapViewportStabilityTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_viewport_stability_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_viewport_stability_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_viewport_stability_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_viewport_stability_test COMMAND mindmap_viewport_stability_test)
    set_tests_properties(mindmap_viewport_stability_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(mindmap_style_test tests/MindMapStyleTest.cpp)
    target_link_libraries(mindmap_style_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_style_test COMMAND mindmap_style_test)

    add_executable(mindmap_rainbow_style_test tests/MindMapRainbowStyleTest.cpp)
    target_link_libraries(mindmap_rainbow_style_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_rainbow_style_test COMMAND mindmap_rainbow_style_test)

    add_executable(mindmap_rainbow_palette_test tests/MindMapRainbowPaletteTest.cpp)
    target_link_libraries(mindmap_rainbow_palette_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_rainbow_palette_test COMMAND mindmap_rainbow_palette_test)

    add_executable(mindmap_hover_geometry_test tests/MindMapHoverGeometryTest.cpp)
    target_link_libraries(mindmap_hover_geometry_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME mindmap_hover_geometry_test COMMAND mindmap_hover_geometry_test)

    add_executable(mindmap_node_size_cache_test tests/MindMapNodeSizeCacheTest.cpp)
    target_link_libraries(mindmap_node_size_cache_test PRIVATE Qt6::Core)
    add_test(NAME mindmap_node_size_cache_test COMMAND mindmap_node_size_cache_test)

    add_executable(mindmap_node_geometry_test tests/MindMapNodeGeometryTest.cpp)
    target_link_libraries(mindmap_node_geometry_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_node_geometry_test COMMAND mindmap_node_geometry_test)

    add_executable(mindmap_scene_rect_test tests/MindMapSceneRectTest.cpp)
    target_link_libraries(mindmap_scene_rect_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_scene_rect_test COMMAND mindmap_scene_rect_test)

    # 小地图投影：正/反变换与信箱带钳制，纯几何，不需要 QPA。
    add_executable(mindmap_minimap_geometry_test tests/MindMapMiniMapGeometryTest.cpp)
    target_link_libraries(mindmap_minimap_geometry_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_minimap_geometry_test COMMAND mindmap_minimap_geometry_test)

    # 版本号单一真源：校验 Version.h 由 VERSION 正确生成，并防止程序版本
    # 与文件格式版本（MindMapIO::CURRENT_VERSION）被后人混用。
    add_executable(mindmap_version_test tests/MindMapVersionTest.cpp)
    target_include_directories(mindmap_version_test PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${LEAP_GENERATED_DIR}"
    )
    # 需要 Widgets：测试里 include 了 MindMapIO.h 以读取文件格式版本常量。
    target_link_libraries(mindmap_version_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    add_test(NAME mindmap_version_test COMMAND mindmap_version_test)

    # ReplaceDocumentCommand 往返：插件批量改写文档（LeapDocumentWriter）的底座。
    add_executable(mindmap_document_replace_test
        tests/MindMapDocumentReplaceTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_document_replace_test PRIVATE "${CMAKE_SOURCE_DIR}/libs" "${LEAP_GENERATED_DIR}")
    target_link_libraries(mindmap_document_replace_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_document_replace_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_document_replace_test COMMAND mindmap_document_replace_test)
    set_tests_properties(mindmap_document_replace_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# 【新增】Windows 下链接 dwmapi 库（用于深色模式标题栏）
if(WIN32)
    target_link_libraries(leap dwmapi)
endif()

# include 路径指向 libs/
target_include_directories(leap PRIVATE
    "${CMAKE_SOURCE_DIR}/libs"
)

# ========== 公式批量渲染器（开发者工具）==========
# 注意：命令行工具保持原样，不添加 WIN32
add_executable(leap_formula_renderer
    FormulaBatchRenderer.cpp
    FormulaCheatSheet.cpp
    FormulaCheatSheet.h
    MindMapFormulaDialog.cpp
    MindMapFormulaDialog.h
    ${JKQTMATHTEXT_SOURCES}
    ${JKQTCOMMON_SOURCES}
)

target_link_libraries(leap_formula_renderer Qt6::Core Qt6::Widgets Qt6::Svg)

target_include_directories(leap_formula_renderer PRIVATE
    "${CMAKE_SOURCE_DIR}/libs"
)

# Qt Creator 的普通 Build 只生成可执行文件。构建 leap 后复制其直接运行时依赖，
# 使 Qt Creator 的“运行”以及直接双击构建目录中的 leap.exe 都能正常工作。
if(WIN32)
    get_filename_component(LEAP_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(LEAP_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)

    add_custom_command(TARGET leap POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/bin/Qt6Core.dll" "$<TARGET_FILE_DIR:leap>/Qt6Core.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/bin/Qt6Gui.dll" "$<TARGET_FILE_DIR:leap>/Qt6Gui.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/bin/Qt6Widgets.dll" "$<TARGET_FILE_DIR:leap>/Qt6Widgets.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/bin/Qt6Svg.dll" "$<TARGET_FILE_DIR:leap>/Qt6Svg.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_MINGW_BIN}/libgcc_s_seh-1.dll" "$<TARGET_FILE_DIR:leap>/libgcc_s_seh-1.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_MINGW_BIN}/libstdc++-6.dll" "$<TARGET_FILE_DIR:leap>/libstdc++-6.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_MINGW_BIN}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:leap>/libwinpthread-1.dll"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:leap>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/plugins/platforms/qwindows.dll" "$<TARGET_FILE_DIR:leap>/platforms/qwindows.dll"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:leap>/styles"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LEAP_QT_ROOT}/plugins/styles/qwindowsvistastyle.dll" "$<TARGET_FILE_DIR:leap>/styles/qwindowsvistastyle.dll"
        VERBATIM
    )
endif()

# ========== 设置窗口离屏预览（开发者工具）==========
# 只编译设置窗口中与 MindMapNode 无关的部分，用于把配色/控件渲染成 PNG 做视觉回归。
# MindMapNodeStub.cpp 提供设置对话框引用到的三个节点方法，避免把整棵节点依赖拉进来。
add_executable(settings_preview EXCLUDE_FROM_ALL
    tools/SettingsPreviewMain.cpp
    tools/MindMapNodeStub.cpp
    MindMapSettingsDialog.cpp
    MindMapSettingsDialog.h
    SettingsWidgets.cpp
    SettingsWidgets.h
    UiTheme.h
    leap.qrc
)

target_link_libraries(settings_preview Qt6::Core Qt6::Widgets Qt6::Svg)

if(WIN32)
    # 设置窗口同样需要 DwmSetWindowAttribute 点亮深色标题栏。
    target_link_libraries(settings_preview dwmapi)
endif()

target_include_directories(settings_preview PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${LEAP_GENERATED_DIR}"
)

# ========== 示例插件（开发者工具）==========
# 一个最小的 leap 插件，用来验证插件机制本身。
# 不参与默认构建：需要时执行
#     cmake --build . --target leap_hello_plugin
# 产物会落在 <build>/plugins/hello/leap_hello_plugin.dll，
# 正好是 leap.exe 启动时扫描的位置，构建完直接运行 leap 就能看到按钮。
add_library(leap_hello_plugin MODULE EXCLUDE_FROM_ALL
    examples/hello-plugin/HelloPlugin.cpp
    examples/hello-plugin/plugin.json
    LeapPluginApi.h
)

target_include_directories(leap_hello_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(leap_hello_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(leap_hello_plugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
)

# minimap_scale：演示 API 3 的视图控制接口（LeapViewControl）。
# 侧边栏里加一个「小地图缩放」按钮，按百分比调小地图大小。
#     cmake --build . --target leap_minimap_scale_plugin
add_library(leap_minimap_scale_plugin MODULE EXCLUDE_FROM_ALL
    examples/minimap_scale/MinimapScalePlugin.cpp
    examples/minimap_scale/plugin.json
    LeapPluginApi.h
)

target_include_directories(leap_minimap_scale_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(leap_minimap_scale_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(leap_minimap_scale_plugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/minimap_scale"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/minimap_scale"
)

# markdown_import：演示 API 4 的批量改写接口（LeapDocumentWriter）。
# 侧边栏里加一个「Markdown」按钮，把粘贴的 Markdown 一次性转成整份导图。
#     cmake --build . --target leap_markdown_import_plugin
add_library(leap_markdown_import_plugin MODULE EXCLUDE_FROM_ALL
    examples/markdown_import/MarkdownImportPlugin.cpp
    examples/markdown_import/plugin.json
    LeapPluginApi.h
)
target_include_directories(leap_markdown_import_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(leap_markdown_import_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(leap_markdown_import_plugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/markdown_import"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/markdown_import"
)


