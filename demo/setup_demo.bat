@echo off
rem Sets up the demo project: copies the addon in (never committed) and
rem bakes the sample character with the dev machine's rigExecBake, once at
rem 1001 (fk.rigexec) and once at 1002 (fk_1002.rigexec, whose input
rem defaults verify.gd drives fk.rigexec with).
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
if errorlevel 1 (
    echo FATAL: copying the addon failed
    exit /b 1
)
rem /EXCLUDE strings match anywhere in a file's absolute path, and xcopy
rem still exits 0 when they exclude every file.
if not exist "%~dp0addons\rigexec\bin\librigexec.windows.template_debug.x86_64.dll" (
    echo FATAL: the addon copy has no extension DLL; check setup_exclude.txt
    exit /b 1
)
set USDINSTALL=%~dp0..\..\usd-install
set PATH=%USDINSTALL%\lib;%USDINSTALL%\bin;%PATH%
set PXR_PLUGINPATH_NAME=%USDRIG%\build\usd\rigExecSchema\resources
"%USDRIG%\build\rigExecBake.exe" "%USDRIG%\examples\01_FkChainTail.usda" --time 1001 -o "%~dp0fk.rigexec"
if errorlevel 1 (
    echo FATAL: bake at 1001 failed
    exit /b 1
)
"%USDRIG%\build\rigExecBake.exe" "%USDRIG%\examples\01_FkChainTail.usda" --time 1002 -o "%~dp0fk_1002.rigexec"
if errorlevel 1 (
    echo FATAL: bake at 1002 failed
    exit /b 1
)
rem Cached imports of earlier bytes would outlive the rebake.
rmdir /S /Q "%~dp0.godot" 2>NUL
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
echo demo ready: fk.rigexec and fk_1002.rigexec baked, addon copied
