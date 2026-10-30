@echo off
rem ============================================================
rem  SNCom release packer (S109 / M7)
rem  Runs the full build (5 steps incl. the Win7 import gate), then verifies
rem  the version chain (version.h <-> exe VERSIONINFO) and writes
rem  dist\release\SNCom-V<ver>-win32.zip with:
rem    sncom.exe + README.md + LICENSE + CHANGELOG.md
rem  Usage: double-click, or run tools\pack_release.bat from anywhere.
rem ============================================================
setlocal
cd /d "%~dp0.."

call build.bat
if errorlevel 1 (
    echo [FAILED] build - release packaging aborted.
    endlocal
    exit /b 1
)

echo.
echo [pack] verifying version chain and packing the release zip ...
python -X utf8 tools\pack_release.py
if errorlevel 1 (
    echo [FAILED] pack_release.py
    endlocal
    exit /b 1
)
endlocal
exit /b 0
