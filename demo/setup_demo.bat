@echo off
rem Sets up the demo project: copies the addon in (never committed) and
rem bakes the sample character with the dev machine's rigExecBake.
rem Usage: setup_demo.bat [path\to\usdRig\build]
setlocal
set USDRIG=%~1
if "%USDRIG%"=="" set USDRIG=%~dp0..\..\usdRig
if not exist "%USDRIG%\build\rigExecBake.exe" (
    echo FATAL: no rigExecBake at %USDRIG%\build; build usdRig first
    exit /b 2
)
if not exist "%~dp0..\addons\rigexec\bin\librigexec.windows.template_debug.x86_64.dll" (
    echo FATAL: no extension DLL; run scons in godot_rigExec first
    exit /b 2
)
rmdir /S /Q "%~dp0addons" 2>NUL
xcopy "%~dp0..\addons" "%~dp0addons\" /E /I /Q /EXCLUDE:%~dp0setup_exclude.txt
set USDINSTALL=%~dp0..\..\usd-install
set PATH=%USDINSTALL%\lib;%USDINSTALL%\bin;%PATH%
set PXR_PLUGINPATH_NAME=%USDRIG%\build\usd\rigExecSchema\resources
"%USDRIG%\build\rigExecBake.exe" "%USDRIG%\examples\01_FkChainTail.usda" --frames 1001,1002 -o "%~dp0fk.rigexec"
if errorlevel 1 (
    echo FATAL: bake failed
    exit /b 1
)
where godot >NUL 2>&1
if errorlevel 1 (
    echo WARNING: godot not on PATH; skipping editor import.
    echo Run: godot --headless --path "%~dp0." --import
) else (
    godot --headless --path "%~dp0." --import
    if errorlevel 1 (
        echo FATAL: editor import failed
        exit /b 1
    )
)
echo demo ready: fk.rigexec baked, addon copied
