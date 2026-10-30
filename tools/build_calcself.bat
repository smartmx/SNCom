@echo off
rem rebuild tools\calcself.exe (bignum self-test, §97) with the same toolchain as build.bat
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "VCVARS=%VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
cd /d "%~dp0.."
if not exist "%VCVARS%" (
    echo [ERROR] vcvars not found
    exit /b 1
)
call "%VCVARS%" >nul
cl /nologo /utf-8 /std:c++17 /W3 /MT /O2 /D_UNICODE /EHsc /Fetools\calcself.exe tools\calcself.cpp src\core\bignum.cpp src\core\hexutil.cpp /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
tools\calcself.exe tools\_cc_calc.txt
endlocal
