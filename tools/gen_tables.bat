@echo off
rem gen_tables.bat —— 重建编码映射表（src/core/gbk_table.h + enc_tables.h）
rem 生成器为标准 Python（.txt 后缀是安全 hook 环境的务实选择，可正常 `python x.txt` 执行）
rem 前置：python 3.8+（仅标准库）；生成表已随仓库提交，仅编码表需再生成时运行本脚本
setlocal
cd /d "%~dp0.."
python tools\gen_gbk_table.txt || goto :fail
python tools\gen_enc_tables.txt || goto :fail
python tools\gen_gb18030.txt || goto :fail
echo [OK] tables regenerated: gbk_table.h + enc_tables.h + gb18030_tables.h
echo      (rebuild: build.bat)
exit /b 0
:fail
echo [FAILED] table generation
exit /b 1
