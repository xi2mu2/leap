# 示例插件：独立构建
#
# 这个工程刻意不依赖 leap 主工程，目的是让插件作者把整个文件夹拷出去，
# 改一改就能验证「我的环境能不能编出可加载的 dll」——这是新手最容易卡住的一步。
#
# 用法（在 examples/hello-plugin 目录下）：
#     cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64"
#     cmake --build build
#
# 产物：build/leap_hello_plugin.dll
# 装法：把 dll 和 plugin.json 一起放进 leap 的 plugins/hello/ 目录，重启 leap。
#
# 关于 LeapPluginApi.h：
#   - 若本目录就在 leap 源码树里（examples/hello-plugin），默认从仓库根目录取；
#   - 若你把本目录拷到了别处，用 -DLEAP_PLUGIN_API_DIR=<含 LeapPluginApi.h 的目录> 指定，
#     或者直接把 LeapPluginApi.h 复制到本目录（同目录下的头会被优先使用）。

cmake_minimum_required(VERSION 3.16)

project(leap_hello_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ---- 定位 LeapPluginApi.h ----
if(LEAP_PLUGIN_API_DIR)
    # 用户显式指定
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LeapPluginApi.h")
    # 已经拷到本目录
    set(LEAP_PLUGIN_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../LeapPluginApi.h")
    # 处在 leap 源码树的 examples/hello-plugin 里
    set(LEAP_PLUGIN_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
else()
    message(FATAL_ERROR
        "找不到 LeapPluginApi.h。请用 -DLEAP_PLUGIN_API_DIR=<目录> 指定它的位置，"
        "或者把它复制到 ${CMAKE_CURRENT_SOURCE_DIR} 下。")
endif()

add_library(leap_hello_plugin MODULE
    HelloPlugin.cpp
    plugin.json
)

target_include_directories(leap_hello_plugin PRIVATE "${LEAP_PLUGIN_API_DIR}")
target_link_libraries(leap_hello_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# 插件 dll 不需要 lib 前缀；输出到 build 根目录，方便直接拷走。
set_target_properties(leap_hello_plugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# 把 plugin.json 也放到 dll 旁边：Q_PLUGIN_METADATA(FILE ...) 在编译期已经把它
# 嵌进 dll 了，运行期不需要这个文件；这里只是方便一起拷贝和查看。
add_custom_command(TARGET leap_hello_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "$<TARGET_FILE_DIR:leap_hello_plugin>/plugin.json"
    COMMENT "复制 plugin.json 到输出目录"
    VERBATIM
)

message(STATUS "LeapPluginApi.h 位置：${LEAP_PLUGIN_API_DIR}")
