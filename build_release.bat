@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

echo ========================================
echo leap Release build and package
echo ========================================

set "APP_VERSION=2.14.10"
set "QT_DIR=C:\Qt\6.5.3\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1120_64"
set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build\release"
set "DIST_ROOT=%PROJECT_DIR%\release"
set "PACKAGE_NAME=leap-v%APP_VERSION%-win64"
set "PACKAGE_DIR=%DIST_ROOT%\%PACKAGE_NAME%"
set "ZIP_PATH=%DIST_ROOT%\%PACKAGE_NAME%.zip"
set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"

rem ISCC：优先用用户级安装，其次 Program Files (x86)
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo ERROR: windeployqt not found: "%QT_DIR%\bin\windeployqt.exe"
    exit /b 1
)

if not exist "%QT_DIR%\bin\qtpaths.exe" (
    echo ERROR: qtpaths not found: "%QT_DIR%\bin\qtpaths.exe"
    exit /b 1
)

if not exist "%QT_DIR%\plugins\platforms\qwindows.dll" (
    echo ERROR: Qt platform plugin not found: "%QT_DIR%\plugins\platforms\qwindows.dll"
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found in PATH.
    exit /b 1
)

if exist "%BUILD_DIR%\leap.exe" (
    tasklist /FI "IMAGENAME eq leap.exe" 2>nul | find /I "leap.exe" >nul
    if not errorlevel 1 (
        echo ERROR: leap.exe is running. Close it before packaging.
        exit /b 1
    )
)

echo [1/6] Configure Release build...
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo [2/6] Build executables...
cmake --build "%BUILD_DIR%" --target leap leap_formula_renderer -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 exit /b 1

echo [3/6] Prepare package directory...
if exist "%PACKAGE_DIR%" rd /s /q "%PACKAGE_DIR%"
if exist "%ZIP_PATH%" del /f /q "%ZIP_PATH%"
mkdir "%PACKAGE_DIR%"

copy /y "%BUILD_DIR%\leap.exe" "%PACKAGE_DIR%\" >nul
if exist "%PROJECT_DIR%\LICENSE" copy /y "%PROJECT_DIR%\LICENSE" "%PACKAGE_DIR%\" >nul
if exist "%PROJECT_DIR%\THIRD-PARTY-LICENSES.md" copy /y "%PROJECT_DIR%\THIRD-PARTY-LICENSES.md" "%PACKAGE_DIR%\" >nul
if exist "%PROJECT_DIR%\leap.ico" copy /y "%PROJECT_DIR%\leap.ico" "%PACKAGE_DIR%\" >nul

echo [4/6] Deploy Qt runtime...
"%QT_DIR%\bin\windeployqt.exe" --no-translations --qtpaths "%QT_DIR%\bin\qtpaths.exe" "%PACKAGE_DIR%\leap.exe"
if errorlevel 1 exit /b 1

if exist "%MINGW_DIR%\bin\libgcc_s_seh-1.dll" copy /y "%MINGW_DIR%\bin\libgcc_s_seh-1.dll" "%PACKAGE_DIR%\" >nul
if exist "%MINGW_DIR%\bin\libstdc++-6.dll" copy /y "%MINGW_DIR%\bin\libstdc++-6.dll" "%PACKAGE_DIR%\" >nul
if exist "%MINGW_DIR%\bin\libwinpthread-1.dll" copy /y "%MINGW_DIR%\bin\libwinpthread-1.dll" "%PACKAGE_DIR%\" >nul

echo [5/6] Pre-render formula cheatsheet...
rem 公式对照表图片由 leap_formula_renderer 生成，输出到工作目录下的 formula_cheatsheet\
rem 这里在包目录内运行，图片直接落在 leap.exe 旁边；渲染器本身不随包发布。
if exist "%BUILD_DIR%\leap_formula_renderer.exe" (
    copy /y "%BUILD_DIR%\leap_formula_renderer.exe" "%PACKAGE_DIR%\" >nul
    pushd "%PACKAGE_DIR%"
    leap_formula_renderer.exe >nul 2>nul
    popd
    del /f /q "%PACKAGE_DIR%\leap_formula_renderer.exe" >nul 2>nul
) else (
    echo WARNING: leap_formula_renderer.exe not built, formula cheatsheet will be missing
)

echo [6/6] Create zip package and installer...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%PACKAGE_DIR%\*' -DestinationPath '%ZIP_PATH%' -Force"
if errorlevel 1 exit /b 1

if exist "%ISCC%" (
    "%ISCC%" "%PROJECT_DIR%\installer\leap.iss"
    if errorlevel 1 (
        echo ERROR: Inno Setup compilation failed.
        exit /b 1
    )
) else (
    echo WARNING: ISCC.exe not found, skipped installer. Install Inno Setup 6:
    echo          winget install JRSoftware.InnoSetup
)

echo ========================================
echo Done.
echo Package directory: %PACKAGE_DIR%
echo Zip package:       %ZIP_PATH%
if exist "%DIST_ROOT%\leap-%APP_VERSION%-setup.exe" echo Installer:          %DIST_ROOT%\leap-%APP_VERSION%-setup.exe
echo ========================================
endlocal
