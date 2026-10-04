@echo off
setlocal
call "%~dp0..\..\usdRig\bin\_vcvars.bat"
cl /nologo /EHsc /std:c++17 /MD /O2 /I"%~dp0..\..\usd-install\include" "%~dp0ball_subdivide.cpp" /Fo"%~dp0ball_subdivide.obj" /Fe"%~dp0ball_subdivide.exe" /link /LIBPATH:"%~dp0..\..\usd-install\lib" osdCPU.lib
exit /b %errorlevel%
