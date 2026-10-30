# SNCom 探针套件（仓库外）

自 `develop/_probe/` 整体迁出（2026-10-25，决策记录 §50）。git 仓库（develop/）只保留产品源码与构建工具；
探针与取证截图留在仓库外，Mimosa 提交门不再扫到测试工具的动态路径/随机数模式（详见 §迁移说明）。

## 运行约定

- 工作目录任意：脚本内部一律 `HERE = dirname(abspath(__file__))` 锚定，`DIST = HERE/../develop/dist`，文档在 `HERE/../docs`。
- GUI 探针启动 `develop/dist/sncom.exe` 后按 PID+窗口类（SNComMain/SNComExt）定位控件（GetDlgItem），不靠坐标猜。
- 依赖：Python 3 + pywin32 + Pillow（截图类）。

## 方法学附录（P3-6 实证沉淀，写新探针前先读）

1. **跨进程禁传字符串指针**：`WM_SETTEXT` 传外部指针会崩或静默失败——输入用
   `EM_SETSEL` + `WM_CHAR` 逐字符注入；读回用 `CB_GETLBTEXTLEN` / `CB_GETCURSEL` /
   `WM_GETTEXTLENGTH` 等长度/索引类消息，不取文本指针。
2. **屏幕取证必须先置顶**：`SetForegroundWindow` 受前台锁限制会被拒——用
   `HWND_TOPMOST` + `PrintWindow`(flag 2) 截窗，且必须配**漂移帧校验**
   （连截两帧比对，遮挡环境下曾出现 9,820~16,008 px 假差异）；控制组必须同样配漂移帧。
3. **表头/列表定位**：HDN_* 通知经 LISTVIEW 转发后 `idFrom` 是表头控件 ID（0），
   不是 LISTVIEW 的 ID；跨表区分靠 `LVM_GETHEADER` 取 hwndFrom。
4. **合成拖拽免指针**：列宽拖拽用 `WM_LBUTTONDOWN/MOUSEMOVE/BUTTONUP` + `MAKELONG`
   合成到表头 HWND，配 `SetCursorPos`；不依赖真实鼠标焦点。
5. **启动等待**：RefreshNics/串口枚举等启动开销 1–3s——等待用轮询至稳定（≥3s），
   启动 0.8s 内读到的几何是旧值（假 FAIL）。
6. **PowerShell 探针**：WinPS 5 无 BOM 的 .ps1 中文字面量按 ANSI 解析 → `-like`
   判定假阴性；判定表达式避免中文字面量或存 UTF-8 BOM。
7. **探针自写 vs 第三方对照**：随机流差分同时保留自写版与第三方实现两份，防同源盲区。
8. **PE 导入表判据（§105）**：Win7 兼容性不靠真机也能量化——`develop/tools/win7_import_audit.py`
   直读 exe 导入表，黑名单覆盖 30+ 条 Win8+/Win10+ API 与 `api-ms-win-*` 伪 DLL；新判据先在
   修复前构建上跑红（旧 exe 留 `_probe/out/r104_old/` 作红对照，见 `verify_r105.py` B 案）。
   ⚠️ 解析名字条目前必须跳过 2 字节 Hint（`{WORD Hint; CHAR Name[]}`），否则名字首字节被污染、
   精确匹配全失效（§105 自查踩过：审计脚本一度"永远绿"）。
9. **批处理脚本三坑（§105 实证）**：①**纯 ASCII**——UTF-8 中文注释在 cmd 的 OEM 代码页下乱码
   会直接炸脚本；②**CRLF**；③**含中文的绝对路径不要写进 bat**（本工程路径带中文）——用
   `%~dp0` 相对定位。另：在 Git Bash 里 `python - <<EOF` 传补丁脚本时，JSON/Bash 转义会把
   `\\` 变 `\`、`\n` 变真换行 → 长补丁改用 Write 落一个 .py（r-string）再执行。
10. **热插拔刷新可合成（§104）**：跨进程 `PostMessage(hwnd, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0)`
    即可触发 `RefreshPorts`（§104-A3 用它验证「缺失端口占位在刷新后保持」）；系统消息 < WM_USER
    跨进程发送安全。
11. **单口环境不 SKIP（§104/§105）**：串口探针默认「≥2 个可独占口」，本机常被外部进程占掉
    一个 → 断言按可用口数**自动裁剪**（`free(nm)` 视空名为空闲；配置口=唯一口时"非首项"判别力
    由占位断言承担并 NOTE），勿让整组 SKIP。
12. **外部程序启动类断言（§107）**：投递菜单命令（WM_COMMAND）后按「窗口类集快照差分 + 标题
    关键词」双断言新窗口（`MMCMainFrame`＝设备管理器、`CabinetWClass`/`ExploreWClass`＝此电脑/
    文件资源管理器）——别只看进程名/返回值。清理：**MMC 跨完整性拒收 WM_CLOSE**（"拒绝访问"）
    → 探针须在 `WM_CLOSE` 失败后按窗口 PID `taskkill /F` 兜底，否则污染后续窗口枚举。
    `ShellExecuteW` 返回 >32 即成功；shell 命名空间 GUID 作 lpFile 可直达「我的电脑/此电脑」。
13. **跨进程消息三类红线（§108 实验定案）**：①**WM_NOTIFY 是指针类消息，跨进程 Send/Post 一律
    被内核拒绝**（`ERROR_ACCESS_DENIED`=5，即使 lParam=0；VirtualAllocEx 让指针在目标进程有效
    也不放行）——消息直投判据只能选非指针消息（WM_COMMAND/CB_*/WM_MOUSEWHEEL/LVM_*）；
    ②滚轮类**免鼠标路径** = `AttachThreadInput`+`SetFocus`（非合成聚焦，跨进程 GetFocus 生效）
    + posted `WM_MOUSEWHEEL`（**坐标必须落控件矩形内**——comctl32 列表据此判定滚动目标）；
    配套「对照组先行」：ASCII 列表须滚动，否则"不改选"读数不可信；判别力用 red control
    验证（`git worktree add <旧提交>` 重建旧 exe，`SNCOM_EXE`/探针内置 R 案，如 r56_old）；
    ③**提权外部程序（mmc）非提权 shell 无法回收**（WM_CLOSE 与 taskkill 均被拒）→ 探针须
    核算残留 PID 并提示人工处理（`verify_r107.py` 收尾 NOTE + `--no-devmgmt` 批跑开关），
    不得静默。

14. **探针一律沙箱运行，禁止以 `develop\dist` 为工作目录（§114 红线）**：历史事故——11 支
    探针（r62/r65/r77~r80/r87/r89/r94/r97/r46）在 `develop\dist` 里启动被测应用并先
    `os.remove(dist\sncom.json)`（**删掉用户「保存参数」且无备份**）、把 `SNCom-Log-*.txt`
    写进 dist。现在统一改法：`BIN = probes\_probe\out\<name>`，前置块「重建沙箱 + 从
    `develop\dist` 拷 exe 新鲜副本」（只需 exe 与探针自写配置）。**新探针照此写**；
    核对法：跑完探针后 `develop\dist` 不应新增/删除任何文件。
    **§116 续迁**：`r54/r56/r67/r73` 并入沙箱——r56 早退路径的 WM_CLOSE（会把配置写回 cwd）
    随之消除；`r67` 颜色 fixture 改**探针自写**沙箱 `sncom.json`（退场「外部 bash 预写」）。
    **§120 补漏**：`r77_cfg` 原以 dist 为 cwd 且 WM_CLOSE 让应用把 crc32/4 写进用户
    `dist\sncom.json`（§114 漏网）→ 沙箱化 + fixture（checksum_kind=add8/start=9）自写。
    仍以 `cwd=dist` 启动的 r41 族/r45 族 9 支为**登记遗留**（只用默认配置读路径，批跑前后
    dist 指纹无差）；`r41_find` 因 §89 查找条迁扩展窗改**显式 SKIP**（缺控件时 pywin32
    `GetDlgItem` 抛 1421，勿裸调）。
15. **环境性失败 = SKIP + 计数（§116 口径，§120 全量统一）**：桌面焦点/剪贴板竞争
    （`SetCursorPos` 被拒、`OpenClipboard 拒绝访问`）不是产品缺陷——各探针内置 `skips`
    计数，SUMMARY 输出 `skip=N` 与 `SKIPPED:` 明细（旧 `chk(...,True,"SKIP")` 口径已并入；
    **r54/r56/r67/r73/r77/r79/r83/r77_cfg 全部就位**）。批跑日志里「没测到」与「测了没过」
    必须可分（新探针照写）。
    另（§120）：判据类假红的根治法=**先做旧 exe 同签名对照再改判据**（r65 相位断言：
    旧 exe 3/3 复现同失败签名 → 判据过宽非回归 → 改众数簇 ±4px、≤2 带偏离；改后新旧
    exe 双绿、§63 真 bug 劈两簇仍红=判别力保留）；探针支持 `SNCOM_EXE` 指定被测 exe
    （红/绿对照标准姿势）。
16. **IME 类判据的两层写法（§112）**：跨进程无法伪造真实 IME 组合（无 IME 上下文），所以：
    ①**字节级**只测可投递路径——posted `WM_IME_CHAR`（非指针类）走终端通道后经回环落转存日志，
    GBK 逐字节断言（'中'=D6 D0、'文'=CE C4）；②**组合窗/候选窗跟随光标**只能源码不变式
    （WM_IME_* + `ImmSetCompositionWindow/CandidateWindow/CompositionFont`）**+ 真机目检**
    （docs/16 第 21 项）。红对照法同 13③：`git worktree add` 旧提交重建 exe（r112 旧 exe 留存
    `_probe\out\r112_old_sncom.exe`），探针 `--rev` 反向期望验证判据判别力。
    同类先例：接收区"光标闪烁"用夺焦后 8 帧首行带**逐像素**扫描（x 步长 1，防 1px 竖线被步长 2
    跳过），暗像素计数波动 ≥6 即闪烁生效（实测 27↔16 方波）。
17. **批跑 FAIL 必须单跑复核（§120/§121 三例 + R21 P3-3 第四次实证）**：跨探针干扰（桌面焦点/
    剪贴板/前台窗口）会使个别判据在批跑中假红——R21 实测 r41-S5（`h0` 基线帧被并发窗口污染，
    批跑 31/32）与 r54（剪贴板竞争，3/4）单跑分别 32/32、2/2 全绿。批跑器逐支 `taskkill`
    间隔应 ≥1.5s；**批跑 FAIL 一律先单跑复核再定性**，勿直接判回归。
    **等待一律条件化，勿固定 sleep（R23 P3-4 定案）**：控件显形/NC 重绘等 UI 迟滞事件
    （r41-S6 编辑器显形、r97 还原后菜单行重绘）偶发晚于固定 0.5~0.8s——「失败与通过两次
    矩形完全相同」即计时瞬态铁证。统一改「轮询等待条件成立（≤2s，10~50ms 步进），条件达成
    或超时后取判据帧」，与 §126/r117 既有轮询写法一致。
18. **转存日志核实须「关闭应用后读」（R21 P3-4 实证）**：应用运行期中读 `SNCom-Log-*.txt`，
    `GetFileSize` 可能连续返回 0（实测 10s 假零——应用 `RxLogPoll` 已写但运行期句柄/缓存视图
    未反映；同一文件应用退出后读**正常 41 B**）。核实转存内容的探针一律**关闭应用后读**
    （或重开句柄校验），勿把运行期零长读数判为「转存失败」。
19. **CPU 探针须避 GetProcessTimes 量子记账（R22 §128 定性）**：`GetProcessTimes/GetThreadTimes`
    以 **15.625ms 调度量子**粒度记账——仪器实测 **wall=46µs 的消息分派被记 cpu=15625µs**
    （单线程 CPU≤墙钟，故为量化非实测）。短窗（8s）读数只落 {0, 0.195, 0.391, 0.586, 0.781}%
    格点，用 1~4 个小窗的中位数/方差下判会假红（R22 C 组 0.59 = 真值 0.3% 的「3 量子/窗」巧合）。
    判据取**累计均值**（量子误差 ≤15.625ms/总时长）或 `QueryProcessCycleTime` 周期计数真值
    （60s 实测两法收敛：0.338% vs 0.304%）；采样窗口 ≥48s 才能压稳量化格点。
20. **扩展窗自动化三坑（§129 实证）**：①控件定位一律 `GetDlgItem(id)`——`IDC_BASE=3000`
    主窗序列与扩展窗匿名枚举值（IDC_EXT_TAB=1、PROTO_FILE=15/START=17/STATUS=19…）稳定；
    **文本匹配跨进程不可靠**（「扩展」按钮文本匹配在两个脚本里结果不一致）。②切页 =
    `TCM_SETCURSEL(0x130C)` + `PostMessage(ext, WM_APP+0x111, 页, 0)`（r89 先例，应用 ：4376
    处理器）——**页签点击跨进程不选中**（posted 与真鼠标均实测无效；真鼠标还受 z-order 摆布）。
    ③`TCM_GETITEMRECT` 跨进程传指针会打崩目标进程（§108 指针红线的 tab 版实例）。
    ④XMODEM 1K 帧首字节是 **STX(0x02)**（128B 才是 SOH）——E2E 收端帧起点须双字节识别，
    只认 0x01 会错位一字节提前触发后续逻辑。
    ⑤**「子窗被父窗擦除覆盖」类缺陷禁用 PrintWindow 取证**（§129 续修2）：PrintWindow 会强制
    全部子窗重画一遍，把真实屏幕上的"消失"洗成"正常"（实测组合框 PrintWindow 正常、屏幕缺失）。
    此类缺陷用**真实屏幕抓图**（`PIL.ImageGrab.grab(bbox)`；被测窗置顶用
    `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE)`——`SetForegroundWindow` 受前台锁限制）。
    ⑥**菜单跨进程只可用句柄类判据**（§129 续修6）：`GetMenuItemInfoW` 的字符串/子菜单指针
    字段在跨位宽（64 位探针 → 32 位应用）下取不到——r=0 全空。可行姿势：
    `GetMenuItemCount`（顶层与子菜单均可，断言项数）+ `GetMenuItemInfo` 仅取 `MIIM_SUBMENU`
    句柄拿 HMENU 再数项数；菜单项 id 从源码枚举推导后直投 `WM_COMMAND` 观察行为
    （⚠️ 中间隔着其它枚举段时别心算——IDM_SEND_HELP 实为 2216，2206 是 IDM_VIEW_BUF）。
21. **故意不 ACK 的探针段必须排空读端（§131 实证）**：协议 E2E 里「对端不 ACK，放任发端
    超时重发」的段（如 verify_r129 C1）**必须**在段末 `recv` 排空 TCP 积压（发端重发的块
    会全部堆在接收缓冲没人读）并取消会话——否则下一段的新收端从残留块流开始读：重复帧触发
    **DUP 补 ACK**，发端多出的迟到 ACK 会**把它当成 EOT 的确认提前 Done**（XMODEM ACK 无
    块号，协议固有；表象=发端状态行「完成」而新收端 `done=False eot=1`）。定性手法先例：
    `diag_r131_eot.py` 双轮（勾选/不勾选）+ wire 逐事件（frame/EOT/ctl）对照。

22. **跨进程读 EDIT/COMBO 文本用 WM_GETTEXT 缓冲读，不用 GetWindowText（§132 实证）**：
    探针（Python 进程）对被测应用（另一进程）的 EDIT 控件调 `win32gui.GetWindowText` 会**假空**
    ——GetWindowText API 对**非本进程且无标题**的窗口直接返回空串（r132 首轮 A5/A6/A7/D1
    共 4 红全此因；而落盘核对 C7/C1 全绿，证明应用侧读写正确）。正解：`SendMessage(WM_GETTEXTLENGTH)`
    + `ctypes.create_unicode_buffer` + `SendMessage(WM_GETTEXT, n+1, buf)`——WM_GETTEXT 属
    USER32 代管缓冲封送的系统消息，跨进程安全（同族先例：§108 红线里 WM_NOTIFY 指针类消息
    才禁，WM_GETTEXT 是特例白名单）。STATIC 标签文本量小、GetWindowText 可用但**勿依赖**。

23. **期望窗口尺寸/位置类判据前，先核对配置未被「最小化态关闭」污染（§132 实证）**：
    应用在**最小化**状态收到 WM_CLOSE 时，若保存 `GetWindowRect` 会得到**图标矩形**
    （实测 160×28）——r41 S1 曾因此假红（win=292x395，实际是应用钳到最小）；dist 配置
    被真实用户会话写成 160×28（§132 已修：IsIconic 时取 rcNormalPosition）。探针侧守则：
    凡断言「启动尺寸/布局」前，若读配置见 win_w/win_h 明显异常（<600），先判为环境性
    → 复位配置或标注，别直接红判应用。

24. **屏幕 DC 抓图三件套：免激活置顶 + 抓图前置诊断 + 遮挡自检（§133 定版）**：
    ①`SetForegroundWindow` 受**前台锁**限制——从后台 shell/agent 启动时静默失败，屏幕
    BitBlt 截到**遮挡窗口**像素（r59 实测整幅 (40,44,52) 深色、含竖分隔线=别的应用 UI；
    旧断言由此全乱）。正解= `SetWindowPos(HWND_TOPMOST, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)`
    → 抓图 → 撤 TOPMOST（规则 20⑤ 配方固化）。⚠️ 置顶会被**别的置顶窗间歇回顶**——
    须**重试环**：置顶后按「屏幕中心点所属顶层根窗是否=被测窗」（GetAncestor GA_ROOT）
    判定，未命中最多 4 次 ×0.5s（r59 §133 实测：无环时 3 跑 2 跑被顶、有环 3 连跑全过）。
    ②抓图前打印一行诊断（rect/vis/iconic/WindowFromPoint→类名/root_ours），失败可回溯。
    ③**遮挡自检**：应用无深色主题，状态栏/客户区必为浅底——整幅暗占比 >50% 判**环境性
    SKIP**（规则 15 计数），不得红判产品。判别力自验=合成三夹具（健康/压进条带/边界）
    各判 PASS/FAIL/PASS（r59 §133 现场三夹具）。
    ④附：`SBARS_SIZEGRIP` 手柄**自身**在条带内画斜点线三角（100% DPI：944×22 bar、
    grip=17、暗点 8 个占 y=18..21 逐行右移）——凡「条带内不得有暗像素」类断言**必然
    不可满足**（R24-P3-1 根因）；几何判据应排除最低 `dot_rows=5` 行后取全宽最右暗像素，
    断言 < W-grip-2。

25. **下拉框选端口的探针别用 CB_SELECTSTRING，改用「索引扫描 + CB_SETCURSEL」（§135 实证）**：
    r70 原代码 `SendMessageW(cmb, 0x0143, -1, buf)  # CB_SELECTSTRING` —— **0x0143 实为
    CB_ADDSTRING**（消息常量抄错）：「选中」从未生效，探针一直开的是**配置文件里的端口**；
    该口（COM3）被用户应用（sscom5131）占着时必假红（打开失败→自动重连→按钮「取消」；
    r70 实测 3/5、单跑稳定复现、与字体/代码改动无关——规则 17 复核红之后还要查**探针自身
    语义**）。正解：`CB_GETCOUNT` → 逐项 `CB_GETLBTEXTLEN/CB_GETLBTEXT` 扫描（项文本带
    「 - 描述」后缀，用 `== name or startswith(name + " - ")` 精确匹配，防 COM1/COM10
    前缀歧义）→ `CB_SETCURSEL`，并**回读 CB_GETCURSEL 断言选中生效**（失败即 assert
    自诊断）。⚠️ ctypes 发消息前设 `SendMessageW.argtypes/restype`（默认 32 位 c_int 会
    截参数/返回值；bash 内联 python 拼 `\\.\COMx` 反斜杠也易被转义层吃错——路径务必 repr 核对）。

26. **字体换代后，含「旧字体度量常量」的布局判据一律改字体无关语义（§135 实证）**：
    r80「校验位窗 == 50+2ch」的 50 是旧外壳字体下 (左列实测宽−固定件) 的残留量（§80 公式
    `cmbW(L"无校验",64)+2ch` 经 `eat()` 填充到左列右缘，实际宽随左列实测宽走）——换宋体后
    左列宽 -2px、残差变 52，原式必红（实测 w=64 want=62）而**产品语义未变**（校验位仍是
    三窗最宽、行右缘仍 == 发送按钮右缘）。正解：度量常量翻译成**意图判据**（"比停止位宽
    ≥2ch"）+ 让已有的**结构不变量**（右缘对齐）承担总宽不变；改字体/DPI/控件 padding 前
    先 grep 探针里的裸像素常量。

27. **就地编辑类流程：合成 RETURN 不关编辑窗，后续断言前必须用普通点击触发失焦提交（§135 实证）**：
    r83 的 A1 向就地编辑 post `WM_KEYDOWN VK_RETURN` 后编辑窗**仍可见**（1.5s 轮询实测；§75
    同款教训——合成按键不走真实输入路径），窗口滞留到 A2「点发送列」的首击才「提交+关窗」，
    而该首击**是否同时触发发送是竞态**（同一构建 4 连红 / 5 连绿；日志只剩点前既有内容的
    尾巴，长度 18B）。正解：A1 后向列表**表头**（LVM_GETHEADER 子窗）post 一组普通
    LBUTTONDOWN/UP 触发失焦提交，再**条件化等待**编辑窗 `IsWindowVisible` 转假（≤1.5s）
    后才点发送列——修复后 3 连跑稳定 3/3。推论：凡「先编辑、再点同一窗另一控件」的探针
    都要先把编辑态显式收尾；合成 RETURN/ESC 一律不信。

28. **「点击 A → 读 B 状态」一律条件化等待，固定 sleep 不得作为同步手段（R25 实证）**：
    r59 断言 2 在 `BM_CLICK` HEX发送 后 `sleep(0.4)` 再读「加回车换行」的 `IsWindowEnabled`，
    审查方复跑 5 次 4×4/4、1×3/4（约 1/5 假红）。产品同步路径正确（`IDC_CHK_HEXSEND` 分支
    内即时 `EnableWindow`），但**消息处理用时上界实测可达 488ms**（codex
    `review_r25_r59_hexnewline` 8 连跑：40/47/61/61/184/244/305/488ms）——固定睡眠短于该
    上界必偶发假红。正解：**20ms 轮询目标状态、≤2s 超时**（`wait_state` 形态），并把实测
    用时写进断言 detail（修后 r59 5 连跑 4/4，实测 41/61/447/508/511ms——3 次超旧 0.4s
    睡眠，坐实根因）。范围：凡「先操作 A 再读 B」的探针（勾选/单选/下拉/按钮触发的使能、
    文本、选中态变化）一律同法；`sleep` 只允许留在「等窗口出现」这类无终态可轮询的场合，
    且必须带上限与重试（与规则 17 的条件化等待同源）。

29. **转存/落盘取证的载荷必须自带行结束符（§137 期实证）**：转存 poll 只写**收尾行**
    （`RxLogPoll` 的写上限 = `builder_.finished()`，悬在行表尾的未收尾行**永不落盘**）；
    r83 的载荷 `"SENDCOL83"` 无行尾，其回显悬尾——是否被写取决于**后续回显数据是否恰好
    把它闭合**（实测同构建 2/5~5/5 漂移、新旧构建同病，曾被误判为「点击竞态」）。修=载荷
    改 §77 转义 `"SENDCOL83\n"`（MsSendRow 与发送框共用 `TextWithEscapesToBytes`）→ 回显
    自闭合，修后 5 连跑 3/3×5。凡用接收转存做断言的探针（r83/r129/E2E 类），载荷末尾一律
    带 `\r`/`\n` 转义；另注意：新数据会闭合**上一条**悬尾行（日志先冒出上一帧），「日志
    增长」判据要容忍这个时序，最终判据仍以载荷字节为准。

## 目录内容

- `verify_*.py` — 各轮审查/修复的验收探针（编号对应 docs/00 决策记录轮次）；
  `verify_r41*.py` 系列是收起态停刷（P0-1）守卫，改 Layout 后必跑。
- `_quarantine\20261027.zip` — §116 隔离区归档（**移出仓库不删除**，169 文件=dist 旧日志
  24+1 + codex 中间目录 63；原目录已删，`_archive_20261027.py` 留档操作脚本）。
- `*.png` / `*.ps1` / `*.cpp` / `build_*.bat` — 各轮取证截图与一次性辅助脚本（历史原样保留）。
- `fix_*.py` / `write_docs*.py` — 当轮一次性修补/写档脚本（历史原样保留，不代表当前代码状态）。

## 留存策略（§123 定）

- **`_probe\out\` = 瞬态沙箱**：探针前置块自会「rmtree + 重建 + 拷新鲜 exe」，跑完即弃、
  不留副本（§123 已清理 idle_base/idle_new 等基准副本；基准对照可随时用
  `git worktree` + `build.bat` 重建，方法学 13③）。**尺寸随回归自然重建**（§124 实测：
  批跑前 7.6 MB → 跑完 31 支约 12.5 MB；「4.3M」是 §123 清理当日快照而非稳态）——
  只有红对照资产与 `r109_zips` 属长期物。
- **长期保留（登记在案的红对照/夹具资产，勿删）**：`r104_old\`（§105 Win8+ 导入红对照）、
  `r56_old\`（§108 滚轮红对照）、`r112_old_sncom.exe`（§112 IME/闪烁/折行红对照）、
  `r117_old\`（§115+ 前快照）、`r109_zips\`（verify_r109 D2/D3 负对照夹具：过期包/缺 exe 包）。
- **批跑/测量日志**（`m7_auto_20261027.log`、`regr_r111_r112.log` 等）与操作脚本
  （`m7_run.sh`、`idle_cpu.py`）留档：报告引用其数字，删了证据链断。
- **`out\`（截图/PNG）**：取证线，原样保留。

## 迁移说明（为何搬出仓库）

Mimosa L3 提交门对 `develop/_probe/*.py` 的「路径穿越/不安全随机数」判定（rand_gb18030_diff /
fuzz_gbk / verify_r46 三处 high）经人工复核均为**测试工具误报**：写入对象全是程序合成的本地
测试语料与被测应用自身配置，路径无外部输入；`random` 用于模糊测试分布（非密码学）。
静态门无法登记误报结论，按「测试工具与产品树分离」处置：整体迁出 git 仓库，产品树对门保持干净。
