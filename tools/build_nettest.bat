@echo off
rem build_nettest.bat —— 编译并运行 M4 NetEngine 无 GUI 自测
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
cd /d "%~dp0.."
if not exist tools\out mkdir tools\out
call "%VCVARS%" >nul
if errorlevel 1 (echo [ERROR] vcvars failed & exit /b 1)
cl /nologo /utf-8 /std:c++17 /W3 /MT /EHsc /DWIN32_LEAN_AND_MEAN ^
   /Fetools\out\nettest.exe tools\nettest.cpp src\platform\net.cpp ^
   /link ws2_32.lib user32.lib
if errorlevel 1 (echo [FAILED] compile & exit /b 1)
tools\out\nettest.exe
endlocal
exit /b %errorlevel%
