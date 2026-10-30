@echo off
rem crosscheck pipeline: hexself + calcself + protofileself + dump_native (line/txt/enc/msini) + py baselines + compare
rem prereq: tools\build_dump.bat  tools\build_calcself.bat  (calcself = bignum self-test, sec.97)
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "DUMPDIR=%~dp0_cc_dump"
if not exist "%DUMPDIR%" mkdir "%DUMPDIR%"
del /q "%DUMPDIR%\*.*" 2>nul

echo [1/6] hexself (ParseHex cases + checksum vectors) + calcself (bignum, sec.97)
hexself.exe _cc_hex.txt
if errorlevel 1 goto fail
calcself.exe _cc_calc.txt
if errorlevel 1 goto fail

echo [2/6] protofileself (XMODEM/YMODEM sender state machine, sec.129)
protofileself.exe _cc_proto.txt
if errorlevel 1 goto fail

echo [3/6] fixtures (encoding vectors + ms ini edge cases)
python gen_fixtures.txt
if errorlevel 1 goto fail

echo [4/6] dump_native line/txt x DAT corpus + enc + msini
set "DATDIR=%~dp0..\..\..\sscom5131"
for %%F in ("%DATDIR%\*.DAT") do (
  if %%~zF GTR 0 dump_native.exe line "%%F" "%DUMPDIR%\%%~nF.lines" gbk 0
  if errorlevel 1 goto fail
  if %%~zF LSS 1000000 dump_native.exe txt "%%F" "%DUMPDIR%\%%~nF.savetxt" gbk
  if errorlevel 1 goto fail
)
for %%E in (gbk big5 gb18030 shift_jis euc_jp euc_kr cp1252) do (
  dump_native.exe enc "%DUMPDIR%\enc_%%E.bin" "%DUMPDIR%\enc_%%E.dec.txt" %%E
  if errorlevel 1 goto fail
)
rem H-3 混合向量（含非法序列）：第 5 参 1 = 1 字节分块喂（最苛刻边界）
for %%E in (gbk big5 shift_jis euc_jp euc_kr cp1252 gb18030) do (
  dump_native.exe enc "%DUMPDIR%\mix_%%E.bin" "%DUMPDIR%\mix_%%E.dec.txt" %%E 1
  if errorlevel 1 goto fail
)
dump_native.exe msini "%DATDIR%\sscom51.ini" "%DUMPDIR%\ms_real.json"
if errorlevel 1 goto fail
dump_native.exe msini "%DUMPDIR%\ms_edge.ini" "%DUMPDIR%\ms_edge.json"
if errorlevel 1 goto fail

echo [5/6] python baselines (PyCom display/settings)
pushd "%~dp0..\..\..\Develop\win64"
python "%~dp0pybaseline.py" > "%~dp0_cc_base.json"
if errorlevel 1 popd & goto fail
python "%~dp0msbaseline.py" > "%~dp0_cc_ms.json"
if errorlevel 1 popd & goto fail
popd

echo [6/6] compare
python "%~dp0crosscheck.py"
if errorlevel 1 goto fail
echo [OK] crosscheck all passed
endlocal
exit /b 0
:fail
echo [FAILED] crosscheck
endlocal
exit /b 1
