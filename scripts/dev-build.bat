@echo off
setlocal enableDelayedExpansion

rem Minimal development build for Artemis on Windows x64: compiles the app
rem and deploys a runnable Artemis.exe, skipping the installer/packaging
rem steps in build-artemis-arch.bat (so 7-Zip and WiX are not required).
rem
rem Usage (from the root of the repo, any shell):
rem   scripts\dev-build.bat [release|debug]     (default: release)
rem
rem Requires:
rem   - Visual Studio 2022 with C++ tools (located automatically via vswhere)
rem   - Qt for MSVC x64, with its bin directory on PATH or set in QT_BIN, e.g.:
rem       set QT_BIN=C:\Qt\6.8.3\msvc2022_64\bin

set BUILD_CONFIG=%1
if "%BUILD_CONFIG%"=="" set BUILD_CONFIG=release
if /I not "%BUILD_CONFIG%"=="release" if /I not "%BUILD_CONFIG%"=="debug" (
    echo Usage: scripts\dev-build.bat [release^|debug]
    exit /b 1
)

set SOURCE_ROOT=%cd%
set BUILD_FOLDER=%cd%\build\build-x64-%BUILD_CONFIG%

if not exist "%SOURCE_ROOT%\artemis.pro" (
    echo Run this script from the root of the repo, e.g. scripts\dev-build.bat
    exit /b 1
)

rem Add Qt to PATH if QT_BIN was provided
if not "%QT_BIN%"=="" set PATH=%QT_BIN%;%PATH%

where qmake.exe >nul 2>&1
if !ERRORLEVEL! NEQ 0 (
    echo Unable to find qmake. Add your Qt bin directory to PATH or set QT_BIN, e.g.:
    echo   set QT_BIN=C:\Qt\6.8.3\msvc2022_64\bin
    exit /b 1
)

rem Set up the MSVC x64 environment if we aren't already in a VS dev prompt.
rem This must happen before qmake runs, since qmake runs compile tests.
if "%VSCMD_ARG_TGT_ARCH%"=="" (
    for /f "usebackq delims=" %%i in (`"%SOURCE_ROOT%\scripts\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        call "%%i\VC\Auxiliary\Build\vcvarsall.bat" x64
    )
)
if "%VSCMD_ARG_TGT_ARCH%"=="" (
    echo Unable to find Visual Studio 2022 C++ build tools.
    echo Install the "Desktop development with C++" workload and try again.
    exit /b 1
)

if not exist "%BUILD_FOLDER%" mkdir "%BUILD_FOLDER%"
pushd "%BUILD_FOLDER%"

echo Configuring the project
qmake.exe "%SOURCE_ROOT%\artemis.pro"
if !ERRORLEVEL! NEQ 0 goto Error

echo Compiling Artemis in %BUILD_CONFIG% configuration
"%SOURCE_ROOT%\scripts\jom.exe" %BUILD_CONFIG%
if !ERRORLEVEL! NEQ 0 goto Error
popd

set APP_FOLDER=%BUILD_FOLDER%\app\%BUILD_CONFIG%

echo Copying dependency DLLs
copy /y "%SOURCE_ROOT%\libs\windows\lib\x64\*.dll" "%APP_FOLDER%" >nul
if !ERRORLEVEL! NEQ 0 goto Error
copy /y "%BUILD_FOLDER%\AntiHooking\%BUILD_CONFIG%\AntiHooking.dll" "%APP_FOLDER%" >nul
copy /y "%SOURCE_ROOT%\app\SDL_GameControllerDB\gamecontrollerdb.txt" "%APP_FOLDER%" >nul

echo Deploying Qt runtime
windeployqt.exe --%BUILD_CONFIG% --qmldir "%SOURCE_ROOT%\app\gui" --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types qmltooling,generic --no-ffmpeg "%APP_FOLDER%\Artemis.exe"
if !ERRORLEVEL! NEQ 0 goto Error

echo.
echo Build successful: %APP_FOLDER%\Artemis.exe
exit /b 0

:Error
popd 2>nul
echo Build failed!
exit /b 1
