@echo off

rem ============================================================

rem  SNCom build script

rem  Toolchain : MSVC 14.29 (v142 / VS2019 build tools, installed side-by-side in the

rem              VS18 BuildTools instance) + Windows SDK 10.0.26100

rem  Output    : x86 single exe, /MT static CRT, /SUBSYSTEM:WINDOWS,6.01 - Win7 SP1..Win11

rem  Win7 note : v142 is the last toolset with official Win7 SP1 support; its STL calls

rem              the UCRT downlevel thunk, so no Win8+ import is emitted. The 14.51 STL

rem              hard-imports Win8+ GetSystemTimePreciseAsFileTime via <thread> - that

rem              broke Win7 startup (S105). src/platform/win7_shim.* + step [5/5] PE

rem              import audit stay as belt-and-braces: if vcvars ever falls back to a

rem              newer toolset, the shim neutralizes the import and the audit still fails

rem              the build on any Win8+ hit (tools/win7_import_audit.py).

rem  Gate      : [5/5] is MANDATORY (S108) - a missing python FAILS the build;

rem              escape hatch (explicit, loud): set SNCOM_ALLOW_SKIP_IMPORT_AUDIT=1.

rem  Usage     : double-click or run build.bat in a console

rem ============================================================

setlocal

set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"

set "VCVARS=%VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"

cd /d "%~dp0"



if not exist "%VCVARS%" goto no_vcvars

if not exist dist mkdir dist



rem LNK1104 guard: a running sncom.exe locks the output and breaks linking.
rem Close it before building. (ASCII-only on purpose: batch files must stay
rem readable under any OEM codepage; findstr avoids the GNU find PATH clash.)
tasklist /FI "IMAGENAME eq sncom.exe" 2>nul | findstr /I "sncom.exe" >nul
if not errorlevel 1 (
    echo [1/5] sncom.exe is running, closing it first ...
    taskkill /F /IM sncom.exe >nul 2>&1
    ping -n 2 127.0.0.1 >nul
)



call "%VCVARS%" -vcvars_ver=14.29 >nul

if errorlevel 1 goto fail_vcvars

rem S106: pin v142 (Win7 SP1 target). Fail loudly instead of silently building with

rem a newer toolset, whose STL would ship a Win7-incompatible exe.

echo toolset  = %VCToolsVersion%

if not "%VCToolsVersion:~0,6%"=="14.29." goto fail_toolset



echo [2/5] compiling resources : icon + manifest + version info ...

rc /nologo /fo dist\sncom.res sncom.rc

if errorlevel 1 goto fail_res



echo [3/5] assembling Win7 import shim (ml) ...

ml /nologo /c /coff /Fo dist\win7_shim_asm.obj src\platform\win7_shim.asm

if errorlevel 1 goto fail_asm



echo [4/5] compiling and linking ...

cl /nologo /utf-8 /std:c++17 /W3 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /EHsc ^
   /Fodist\ /Fddist\sncom.pdb ^
   src\main.cpp src\ui\mainwindow.cpp src\ui\rxview.cpp src\core\json.cpp src\core\settings.cpp src\core\paths.cpp ^
   src\core\encoding.cpp src\core\hexutil.cpp src\core\bignum.cpp src\core\display.cpp src\core\checksum.cpp src\core\msini.cpp src\core\protofile.cpp src\platform\win_util.cpp src\platform\serial.cpp src\platform\net.cpp src\platform\nics.cpp src\platform\win7_shim.cpp dist\win7_shim_asm.obj ^
   /link /SUBSYSTEM:WINDOWS,6.01 /ENTRY:wWinMainCRTStartup /MANIFEST:NO ^
   /OUT:dist\sncom.exe dist\sncom.res ^
   comctl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib imm32.lib setupapi.lib comdlg32.lib iphlpapi.lib ws2_32.lib uxtheme.lib

if errorlevel 1 goto fail_cl



echo.

echo [5/5] Win7 import audit ...

python --version >nul 2>nul

if errorlevel 1 goto audit_no_python

python -X utf8 tools\win7_import_audit.py dist\sncom.exe

if errorlevel 1 goto fail_audit

goto audit_done



:audit_no_python

rem S108: the import audit is a MANDATORY gate (Win7 load-time compatibility);

rem a missing python must fail the build, not silently disable the gate.

rem The escape hatch is explicit and loud only: SNCOM_ALLOW_SKIP_IMPORT_AUDIT=1.

rem Value must be exactly "1" (a trailing space from `set VAR=1 && build.bat` is tolerated);

rem anything else means the gate stays on.

set "SKIP_AUDIT_VAR=%SNCOM_ALLOW_SKIP_IMPORT_AUDIT%"

if defined SKIP_AUDIT_VAR for /f "tokens=1" %%A in ("%SKIP_AUDIT_VAR%") do if "%%A"=="1" goto audit_skip_ok

echo [FAILED] python not found - the Win7 import audit is a mandatory gate.

echo          Install Python (3.x in PATH) and re-run, or, at your own risk,

echo          set SNCOM_ALLOW_SKIP_IMPORT_AUDIT=1 to skip it.

endlocal

exit /b 1



:audit_skip_ok

echo [WARN] Win7 import audit SKIPPED on purpose (SNCOM_ALLOW_SKIP_IMPORT_AUDIT=1).

:audit_done



for %%F in (dist\sncom.exe) do echo [OK] %%~fF   %%~zF bytes

endlocal

exit /b 0



:no_vcvars

echo [ERROR] vcvarsamd64_x86.bat not found:

echo         %VCVARS%

echo         Please fix the VS path at the top of this script.

endlocal

exit /b 1



:fail_vcvars

echo [ERROR] vcvars init failed.

endlocal

exit /b 1

:fail_toolset

echo [FAILED] MSVC v142 (14.29) toolset not active - install the VS2019 v142 build tools

echo          component, or fix the -vcvars_ver argument above.

endlocal

exit /b 1



:fail_res

echo [FAILED] resource compile.

endlocal

exit /b 1



:fail_cl

echo [FAILED] compile or link.

endlocal

exit /b 1

:fail_asm

echo [FAILED] assembling src/platform/win7_shim.asm.

endlocal

exit /b 1

:fail_audit

echo [FAILED] Win7 import audit: Win8+ static import present (see above).

endlocal

exit /b 1

