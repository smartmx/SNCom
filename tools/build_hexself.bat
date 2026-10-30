@echo off
rem rebuild tools\hexself.exe (ParseHex self-test) with the same toolchain as build.bat
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "VCVARS=%VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
cd /d "%~dp0.."
if not exist "%VCVARS%" (
    echo [ERROR] vcvars not found
    exit /b 1
)
call "%VCVARS%" >nul
cl /nologo /utf-8 /std:c++17 /W3 /MT /O2 /D_UNICODE /EHsc /Fetools\hexself.exe tools\hexself.cpp src\core\hexutil.cpp src\core\checksum.cpp /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
tools\hexself.exe tools\_cc_hex.txt
endlocal
