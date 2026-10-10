@echo off
rem Configures and builds Retromulator (VST3 + Standalone) with MSVC, CMake and Ninja.
rem Usage: build_win.bat [configure|build]   (default: both)
setlocal

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
	echo Visual Studio not found
	exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "SRC=%~dp0."
set "BLD=%~dp0build\win-release"

if /i "%~1"=="build" goto build

cmake -S "%SRC%" -B "%BLD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl || exit /b 1
if /i "%~1"=="configure" exit /b 0

:build
cmake --build "%BLD%" --target Retromulator_VST3 Retromulator_Standalone -- -k 0
exit /b %ERRORLEVEL%
