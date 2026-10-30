@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
cd /d F:\projects\sscom??????\sncom\develop\_probe
cl /nologo /utf-8 /MT /O2 nicsprobe.cpp >nul
nicsprobe.exe
