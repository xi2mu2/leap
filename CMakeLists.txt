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
set(LEAP_RC_FILE "")
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/leap.ico")
    set(LEAP_RC_FILE "${CMAKE_SOURCE_DIR}/leap.rc")
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
    MindMapIO.cpp
    MindMapIO.h
    MindMapNode.cpp
    MindMapNode.h
    MindMapNodeGeometry.h
    MindMapSceneRect.h
    MindMapNodeSizeCache.h
    MindMapStyle.h
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
    leap.qrc
    ${JKQTMATHTEXT_SOURCES}
    ${JKQTCOMMON_SOURCES}
    ${LEAP_RC_FILE}
)

target_link_libraries(leap Qt6::Core Qt6::Widgets Qt6::Svg)

include(CTest)
if(BUILD_TESTING)
    # Exercise real node/document/image mutations and file round trips.
    get_target_property(LEAP_CACHE_TEST_SOURCES leap SOURCES)
    list(REMOVE_ITEM LEAP_CACHE_TEST_SOURCES main.cpp leap.qrc ${LEAP_RC_FILE})
    add_executable(mindmap_serialization_cache_test
        tests/MindMapSerializationCacheTest.cpp ${LEAP_CACHE_TEST_SOURCES})
    target_include_directories(mindmap_serialization_cache_test PRIVATE "${CMAKE_SOURCE_DIR}/libs")
    target_link_libraries(mindmap_serialization_cache_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
    if(WIN32)
        target_link_libraries(mindmap_serialization_cache_test PRIVATE dwmapi)
    endif()
    add_test(NAME mindmap_serialization_cache_test COMMAND mindmap_serialization_cache_test)
    set_tests_properties(mindmap_serialization_cache_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(mindmap_style_test tests/MindMapStyleTest.cpp)
    target_link_libraries(mindmap_style_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME mindmap_style_test COMMAND mindmap_style_test)

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
)


