@echo off
rem M4 import check: the shipped runtime library must not import USD.
rem Usage: check_no_usd_imports.bat ^<librigexec....dll^> [dumpbin]
rem Exits 1 naming the first USD import found.
setlocal
set DLL=%~1
if "%DLL%"=="" (
    echo usage: check_no_usd_imports.bat ^<dll^> [dumpbin]
    exit /b 2
)
set DUMPBIN=%~2
if "%DUMPBIN%"=="" set DUMPBIN=dumpbin
where %DUMPBIN% >NUL 2>&1
if errorlevel 1 (
    echo FATAL: dumpbin not on PATH; run from a VS developer prompt
    exit /b 2
)
set FOUND=
for /f "tokens=*" %%L in ('%DUMPBIN% /IMPORTS "%DLL%" 2^>NUL ^| findstr /I /C:"usd_" /C:"pxr" /C:"tbb"') do (
    echo USD-LEAK: %%L
    set FOUND=1
)
if defined FOUND (
    echo FAIL: %DLL% imports USD symbols
    exit /b 1
)
echo OK: %DLL% imports no USD symbols
