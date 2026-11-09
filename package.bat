@echo off
setlocal enabledelayedexpansion
chcp 65001 > nul

:: ====================================================
:: leap 思维导图 - Windows 便携打包脚本
:: 使用方法：
::   1. 确保已完成 Release 构建（例如在 build 目录下执行 cmake --build . --config Release）
::   2. 根据需要修改下方的 QT_PATH 和 BUILD_DIR
::   3. 以管理员身份运行（非必要，但建议避免权限问题）
::   4. 打包结果将生成在 %OUTPUT_DIR% 中
:: ====================================================

title leap 打包工具

:: ------------------- 可配置部分 ---------------------
:: Qt 安装路径（指向包含 bin 目录的父目录）
set QT_PATH=C:\Qt\6.5.3\mingw_64

:: MinGW 运行时路径（通常与编译器所在目录一致，例如 Qt 安装目录下的 tools）
set MINGW_PATH=C:\Qt\Tools\mingw1120_64\bin

:: 构建输出目录（包含 leap.exe 的 Release 文件夹）
set BUILD_DIR=build\Release

:: 打包后的输出目录
set OUTPUT_DIR=leap-portable

:: 是否需要压缩为 zip 文件（1=压缩，0=仅复制）
set ZIP_OUTPUT=1

:: 是否运行公式渲染器生成对照表图片（1=运行，0=跳过）
set GENERATE_CHEATSHEET=1

:: ------------------- 自动检测部分 -------------------
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

if not exist "%SCRIPT_DIR%\%BUILD_DIR%\leap.exe" (
    echo [错误] 找不到 leap.exe，请先完成 Release 构建。
    echo 尝试查找的位置：%SCRIPT_DIR%\%BUILD_DIR%\leap.exe
    pause
    exit /b 1
)

if not exist "%QT_PATH%\bin\windeployqt.exe" (
    echo [错误] 未找到 windeployqt.exe，请检查 QT_PATH 是否正确。
    echo 当前 QT_PATH=%QT_PATH%
    pause
    exit /b 1
)

:: ------------------- 创建输出目录 -------------------
echo [1/6] 准备输出目录...
if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%" > nul 2>&1
mkdir "%OUTPUT_DIR%" > nul 2>&1

:: ------------------- 复制主程序 -------------------
echo [2/6] 复制主程序...
xcopy /y /q "%SCRIPT_DIR%\%BUILD_DIR%\leap.exe" "%OUTPUT_DIR%\" > nul

:: ------------------- 运行 windeployqt -------------------
echo [3/6] 部署 Qt 运行时库...
cd /d "%OUTPUT_DIR%"
"%QT_PATH%\bin\windeployqt.exe" --release --no-compiler-runtime --no-angle --no-opengl-sw --no-system-d3d-compiler leap.exe
if errorlevel 1 (
    echo windeployqt 执行失败，请检查 Qt 环境。
    cd /d "%SCRIPT_DIR%"
    pause
    exit /b 1
)

:: ------------------- 复制 MinGW 运行时 -------------------
echo [4/6] 复制 MinGW 运行时库...
if exist "%MINGW_PATH%\libstdc++-6.dll" (
    xcopy /y /q "%MINGW_PATH%\libstdc++-6.dll" "%OUTPUT_DIR%\" > nul
    xcopy /y /q "%MINGW_PATH%\libgcc_s_seh-1.dll" "%OUTPUT_DIR%\" > nul
    xcopy /y /q "%MINGW_PATH%\libwinpthread-1.dll" "%OUTPUT_DIR%\" > nul
) else (
    echo [警告] MinGW 运行时路径无效，请手动复制 libstdc++-6.dll 等文件。
)

:: ------------------- 生成 / 复制公式对照表图片 -------------------
echo [5/6] 准备公式对照表...
set CHEATSHEET_SRC=%SCRIPT_DIR%\formula_cheatsheet
set CHEATSHEET_DST=%OUTPUT_DIR%\formula_cheatsheet

if "%GENERATE_CHEATSHEET%"=="1" (
    if exist "%SCRIPT_DIR%\%BUILD_DIR%\leap_formula_renderer.exe" (
        echo 运行公式渲染器生成对照表图片...
        "%SCRIPT_DIR%\%BUILD_DIR%\leap_formula_renderer.exe"
        if errorlevel 1 (
            echo [警告] 公式渲染器执行失败，将尝试复制已有图片。
        ) else (
            echo 公式图片生成完成。
        )
    ) else (
        echo [提示] 未找到 leap_formula_renderer.exe，跳过自动生成。
    )
)

if exist "%CHEATSHEET_SRC%" (
    xcopy /e /i /y "%CHEATSHEET_SRC%" "%CHEATSHEET_DST%\" > nul
    echo 公式图片已复制。
) else (
    echo [提示] 未找到公式图片文件夹，公式查阅功能可能无法正常显示预览。
)

:: ------------------- 可选：复制额外数据文件 -------------------
:: 如果项目中有其他需要随包的文件（如 readme.txt、LICENSE 等），可在此处添加
if exist "%SCRIPT_DIR%\README.md" (
    xcopy /y /q "%SCRIPT_DIR%\README.md" "%OUTPUT_DIR%\" > nul
)

:: ------------------- 压缩打包 -------------------
cd /d "%SCRIPT_DIR%"
if "%ZIP_OUTPUT%"=="1" (
    echo [6/6] 正在生成 zip 压缩包...
    set ZIP_NAME=leap_%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%.zip
    set ZIP_NAME=!ZIP_NAME: =0!
    if exist "%ZIP_NAME%" del "%ZIP_NAME%" > nul 2>&1

    :: 使用 PowerShell 压缩（Windows 8+ 自带）
    PowerShell -Command "Compress-Archive -Path '%OUTPUT_DIR%' -DestinationPath '%ZIP_NAME%' -Force"
    if errorlevel 1 (
        echo [警告] 压缩失败，请手动压缩 %OUTPUT_DIR% 文件夹。
    ) else (
        echo 打包成功: %ZIP_NAME%
    )
) else (
    echo [6/6] 跳过压缩，便携目录已就绪: %OUTPUT_DIR%
)

:: ------------------- 完成 -------------------
echo.
echo ============================================
echo 打包流程完成！
echo 便携版目录: %SCRIPT_DIR%\%OUTPUT_DIR%
if "%ZIP_OUTPUT%"=="1" echo Zip 包: %ZIP_NAME%
echo ============================================
pause
exit /b 0