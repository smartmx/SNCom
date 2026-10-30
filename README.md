# SNCom（Win32 原生版）

用 C++17 + Win32 API 从零实现的**串口/网络数据调试器**——SSCOM 5.13.1 的常用功能全集，**无 Python、无 Qt、无任何第三方依赖**，单一 exe（静态 CRT），支持 Win7 SP1 ~ Windows 11。

## 功能

- **串口**：端口枚举与热插拔、300–50,000,000 波特率（含自定义）、数据位/停止位/校验/流控、DTR/RTS、引脚状态与 BREAK、改参数自动重开、断线自动重连
- **网络**：TCP 客户端（自动重连）/ TCP 服务端（≤64 客户端）/ UDP；IPv4/IPv6 单栈选择
- **接收显示**：自绘视图（增量行表 + 可视区渲染，30fps 合并刷新）、缓冲 500K–100M、时间戳与收发方向标记、HEX 折行、12 种预置编码（含 gb18030 全量）、查找/高亮、选择复制
- **发送**：文本/HEX 双模式、转义字符、`Ctrl+Enter` 发送、多条字符串（100 条×注释/HEX/延时/顺序，单击即发/循环）、定时发送、校验 9 算法、发送文件（流式+背压）、协议文件发送（XMODEM/YMODEM，校验与包大小可选）
- **终端仿真**：发送菜单总开关（默认关）；回车/按键两种发送方式、本地回显、CR-LF
- **其他**：扩展小窗（多条发送/进制转换/ASCII 表/查找/协议发送）、参数持久化 `sncom.json`、SSCOM ini 导入

## 构建

```bat
build.bat
```

- 工具链：**MSVC 14.29（v142）** + Windows SDK 10.0.26100，`vcvarsamd64_x86` → **x86 单 exe**
- 产物：`dist\sncom.exe`（`/MT` 静态 CRT，`/SUBSYSTEM:WINDOWS,6.01` → Win7 SP1 ~ Win11）
- 构建自带两道门：资源/汇编/链接全过 + **Win7 导入审计**（`tools\win7_import_audit.py`，导入白名单精确匹配，杜绝 Win8+ 静态导入）
- 编码映射表（GBK/big5/shift_jis/euc_kr/cp1252）已随源提交；仅需再生成时用 `tools\gen_tables.bat`（标准库 Python）

## 运行

```bat
dist\sncom.exe
```

首次运行在 exe 同目录生成 `sncom.json`（保存参数/关窗时写回）。

## 目录结构

```
src/
├─ main.cpp            入口：DPI、ITaskbarList::AddTab、comctl32、消息循环
├─ core/               纯逻辑（可单测）：json/settings/paths/encoding/hexutil/checksum/
│                      bignum/display(增量行表)/msini/protofile(XMODEM·YMODEM 状态机)
├─ platform/           win_util(DPI/任务栏/字体)、serial、net、nics
└─ ui/                 mainwindow（菜单/状态栏/控件树/全部业务）、rxview（自绘接收视图）、layout.h
tools/                 构建与验收工具：win7_import_audit（导入白名单门）、pack_release（出包）、
                       runcross（七段跨实现对拍）、各 selftest 的构建脚本与源、gen_tables
assets/                sncom.ico / sncom-icon.svg
probes/                验收探针（见下）
```

## 验收（probes/）

- `verify_*.py` —— 与各开发轮次一一对应的验收探针（UI 自动化 + 回环链路 + 转存取证 + 像素判据），
  覆盖收发链路、协议状态机 E2E、持久化、终端仿真、任务栏行为等；`verify_r41*.py` 为收起态停刷守卫
- `probe_check.py` —— 全部探针的静态检查（语法/import/未定义名）
- `README.md` —— 探针方法学（批跑/判据/取证规则 1~29）

运行需 `pywin32` + `Pillow`；批跑器在 codex 本地归档（不入库）。修改布局/字体/发送路径后建议至少跑 `verify_r41 / r83 / r126`。

## 文档

详细的逐轮决策记录、需求基线、开发方案、发布矩阵与协议评估文档为**本地归档，不入库**；
本 README 即仓库的唯一说明。历史：项目由参照实现（Python/Qt 版）逐轮重写而来，行为口径以其定版为准。
