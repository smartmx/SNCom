@echo off
rem build crosscheck tools: dump_native.exe (DAT to lines) + hexself.exe (selftest)
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
cd /d "%~dp0.."
if not exist tools mkdir tools
cl /nologo /utf-8 /std:c++17 /W3 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /EHsc ^
   tools\dump_native.cpp src\core\display.cpp src\core\encoding.cpp src\core\msini.cpp ^
   /Fe:tools\dump_native.exe /Fo:tools\
if errorlevel 1 goto fail
cl /nologo /utf-8 /std:c++17 /W3 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /EHsc ^
   tools\hexself.cpp src\core\hexutil.cpp src\core\checksum.cpp ^
   /Fe:tools\hexself.exe /Fo:tools\
if errorlevel 1 goto fail
echo [OK] tools\dump_native.exe + tools\hexself.exe
endlocal
exit /b 0
:fail
echo [FAILED] build tools
endlocal
exit /b 1
