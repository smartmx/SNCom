// sncom/src/ui/mainwindow.h —— 主窗口（M0/M1 外壳 + M0.5 spike 接线：自绘接收区/串口引擎/终端输入）
#pragma once
#include <windows.h>
#include <string>
#include <deque>
#include <utility>
#include <vector>
#include "../core/settings.h"
#include "../core/encoding.h"
#include "../core/display.h"
#include "../core/rxbuf.h"
#include "../core/checksum.h"
#include "../core/protofile.h"
#include "rxview.h"
#include "../platform/serial.h"
#include "../platform/net.h"
#include "../platform/nics.h"

namespace sncom {

class MainWindow {
public:
    static bool RegisterClasses(HINSTANCE hInst);
    static HWND Create(HINSTANCE hInst, int nCmdShow, bool selftest = false);
    HWND hwnd() const { return hwnd_; }
    // 滚轮悬停路由（C2a）：鼠标下的窗口若是自绘接收区则返回其句柄（否则 nullptr）
    static HWND WheelTarget(const MSG& msg);

private:
    HINSTANCE    hInst_ = nullptr;
    HWND         hwnd_ = nullptr;
    HWND         status_ = nullptr;
    HWND         progress_ = nullptr;
    HFONT        uiFont_ = nullptr;          // 外壳字体：§135 起 = 宋体 9pt（对齐 SSCOM 实测口径）
    HFONT        sysFont_ = nullptr;         // 系统 UI 字体（状态栏专用——原版状态栏即系统字体）
    HFONT        dataFont_ = nullptr;        // 数据字体：配置里的统一「字体与颜色」
    HFONT        tsRxFont_ = nullptr;        // §68 接收时间戳字体 = 数据字体+样式变体（粗/斜/下）
    HFONT        tsTxFont_ = nullptr;        // §68 发送时间戳字体
    HBRUSH       pinBrush_ = nullptr;        // 线路引脚指示框背景（未置位=灰）
    HBRUSH       breakBrush_ = nullptr;      // §73 线路 BREAK 点亮=黄
    HBRUSH       okBrush_ = nullptr;         // 引脚置位=绿（← PyCom #2ECC40）
    HBRUSH       errBrush_ = nullptr;        // ERR=红（← PyCom #FF4136）
    int          dpi_ = 96;
    bool         narrow_ = false;
    Config       cfg_;
    std::wstring cfgPath_;

    // M0.5 spike + M2-A 显示管线
    RxView           rxv_;                   // 自绘接收视图（替换 M0 占位）
    SerialEngine     engine_;                // overlapped + I/O 线程（串口）
    NetEngine        net_;                   // WSAAsyncSelect 主线程事件驱动（M4 网络）
    LineBuilder      builder_{L"gbk", false, true, 20};   // ← PyCom display.LineBuilder
    RxBuffer         buf_;                   // ← PyCom rxbuf（buffer_cap 默认 10M）
    // H-2 直渲：视图经行源回调直接读 builder（SetLineSource），无推入游标/行表副本
    uint64_t         lastBufStart_ = 0;      // 字节缓冲裁剪水位（驱动行表 trim_before_abs）
    bool             lastHex_ = false, lastSplit_ = true;
    int              lastSplitMs_ = 20;
    // I-1 空闲重绘治理：TickDisplay 变化检测快照（行数/base/活动行长度均不变 → 不通知视图）
    int              lastCnt_ = -1, lastBase_ = -1;
    size_t           lastOpenLen_ = (size_t)-1;
    // J-1 空闲 tick 早退：builder_.feed 的调用点置位；TickDisplay 无新数据且无未收尾行 → 直接 return
    bool             newData_ = false;
    // UI-3/UI-4：最小窗宽（「发 送」按钮右边界）；定时发送两态（勾选=就绪，点发送=运行）
    int              minClientW_ = 560;
    int              minClientH_ = 0;      // §85：最小窗高（客户高）——防"接收区压住面板行"
    int              rxHAvailable_ = 0;    // §85：本次布局接收区的"可用高"（未夹取前）
    // 波特率「自定义」槽位（SSCOM 对齐；§55 恢复 §46 弹窗式）：下拉含「自定义」项，
    // 选中弹输入窗（非模态+主窗禁用，300~12000000 校验/取消恢复）；DROPDOWNLIST 不可直输
    int              baudCustomIdx_ = -1;
    std::wstring     baudLastValid_;
    HWND             baudDlg_ = nullptr, baudEd_ = nullptr;
    void             OpenBaudInput();
    void             BaudApply(int v);
    static LRESULT CALLBACK BaudDlgProc(HWND, UINT, WPARAM, LPARAM);
    int              toolRight = 0;              // 工具行右缘（「扩展」按钮，Layout 实测）
    bool             timedArmed_ = false, timedRunning_ = false;
    HANDLE           logFile_ = INVALID_HANDLE_VALUE;     // 接收数据到文件（转存）
    int              logWritten_ = 0;        // 已写日志行游标（绝对行号）
    bool             connected_ = false;
    bool             loopMode_ = false;      // 软件回环
    bool             wantOpen_ = false;      // 用户打开意图（重连依据；← PyCom want_open）
    bool             reconnecting_ = false;  // 重连中（打开按钮=「取消」）
    bool             linkErr_ = false;       // ERR 灯（kEvtError 置位，连接成功清除）
    bool             pinActive_[5] = {};     // DCD/DSR/CTS/RI/ERR 点亮态
    bool             breakLit_ = false;      // §76：线路 BREAK 黄亮中（chk_break_ ownerdraw 显示，1.5s）
    bool             breakChecked_ = false;  // §76：BREAK 发送选择态（ownerdraw 按钮不维护 BM_*CHECK，自持）
    unsigned         txBytes_ = 0, rxBytes_ = 0;
    unsigned         txLast_ = 0, rxLast_ = 0;
    double           txRate_ = 0, rxRate_ = 0;
    // B2 待发队列（← PyCom _tx_pend：分段 (字节, record)，16MB 上限，断开即丢弃）
    static const size_t kTxPendCap = 16u << 20;
    std::deque<std::pair<std::vector<uint8_t>, bool>> txPend_;
    // 自检（--spike-selftest）
    bool             selftest_ = false;
    int              stFrame_ = 0;
    // §77：建窗期通知闸门——控件创建/灌初值/ApplyFonts(WM_SETFONT) 都会产生
    // EN_CHANGE，那时邻控件可能尚未建好（CB_GETCURSEL=-1），从控件回读会把
    // cfg 里刚载入的持久值覆盖成默认（「校验设置重启即丢」老 bug 的根因）。
    // 控件通知在 WM_CREATE 结束前一律视为构建噪声，不参与 cfg 回写。
    bool             uiInitDone_ = false;

    // 工具行
    HWND btn_clear_ = nullptr, btn_find_ = nullptr, btn_new_ = nullptr;
    HWND chk_autoscroll_ = nullptr;
    HWND btn_openfile_ = nullptr, edit_file_ = nullptr, btn_sendfile_ = nullptr,
         btn_stopfile_ = nullptr, btn_clearsend_ = nullptr;
    HWND btn_collapse_ = nullptr, btn_top_ = nullptr, btn_saveparams_ = nullptr, btn_expand_ = nullptr;

    // 左列：串口连接区
    HWND lbl_port_ = nullptr, cmb_port_ = nullptr, btn_refresh_ = nullptr;
    HWND led_ = nullptr, btn_open_ = nullptr;                     // 连接状态圆点 + 打开/关闭
    HWND lbl_baud_ = nullptr, cmb_baud_ = nullptr;
    HWND lbl_data_ = nullptr, cmb_databits_ = nullptr, lbl_stop_ = nullptr, cmb_stopbits_ = nullptr,
         lbl_parity_ = nullptr, cmb_parity_ = nullptr;
    HWND lbl_flow_ = nullptr, cmb_flow_ = nullptr, chk_dtr_ = nullptr, chk_rts_ = nullptr;
    HWND lbl_line_ = nullptr, pin_[5] = {}, chk_break_ = nullptr;   // §76：BRK 并入 chk_break_（ownerdraw 显示）
    HWND btn_send_ = nullptr;                                     // 大「发 送」按钮（两区共用）
    std::vector<std::wstring> portNames_;                         // 下拉项 ↔ 端口名（显示带描述）

    // 左列：网络连接区（模式≠串口时显示；行序对齐 PyCom 网络面板）
    HWND led2_ = nullptr, lbl_netmode_ = nullptr;                 // 网络 LED + 当前模式标签
    HWND lbl_remote_ = nullptr, edit_rip_ = nullptr, edit_rport_ = nullptr;
    HWND btn_nconn_ = nullptr, btn_ndisc_ = nullptr;
    HWND lbl_nic_ = nullptr, cmb_nic_ = nullptr, btn_nicref_ = nullptr;
    HWND lbl_local_ = nullptr, cmb_lip_ = nullptr, edit_lport_ = nullptr;
    std::vector<NicInfo> nics_;                                   // 当前枚举缓存

    // 中列：接收选项
    HWND chk_hex_show_ = nullptr, btn_save_data_ = nullptr, chk_rx_file_ = nullptr;
    HWND chk_split_ = nullptr, chip_ = nullptr, lbl_timeout_ = nullptr,
         edit_split_ms_ = nullptr, lbl_ms_ = nullptr;

    // 右列：发送选项
    HWND chk_hex_send_ = nullptr, chk_timed_ = nullptr, edit_interval_ = nullptr,
         lbl_timed_unit_ = nullptr, chk_newline_ = nullptr;
    HWND lbl_chk_pre_ = nullptr, edit_chk_start_ = nullptr, lbl_chk_suf_ = nullptr,
         cmb_chk_tail_ = nullptr, lbl_chk_lbl_ = nullptr, cmb_checksum_ = nullptr,
         edit_chk_view_ = nullptr;

    // 发送内容框
    HWND edit_send_ = nullptr;

    // 阶段 D 壳层：置顶/收起/扩展小窗
    bool topmost_ = false, collapsed_ = false, extOpen_ = false;
    HWND extWnd_ = nullptr, extTab_ = nullptr, calcCarr_ = nullptr, asciiList_ = nullptr;
    HWND calcEdits_[10] = {};
    HWND calcLbls_[10] = {};            // 标签句柄（创建时保存）
    HWND calcCarrLbl_ = nullptr;        // 「C 数组」标签（用户反馈：输出框缺标题）
    bool calcLock_ = false;

    // 阶段 D 查找条（← PyCom find_bar：文本/HEX/大小写/计数/跳转，无命中高亮）
    bool findOpen_ = false;                  // §75：= 扩展窗「查找」页激活且扩展窗可见（FindRescan/Jump 守卫）
    std::wstring noticeText_;                // §74：状态栏通知段最近文本（Layout 重发段后重写来源，审查 F3）
    std::wstring rxText_;                    // §77：RX 段最近文本（染色覆盖窗自绘用）
    int rxTintPct_ = 0;                      // §77：显示缓冲占用百分比（≥60 时 RX 段渐染淡红）
    HWND tint_ = nullptr;                    // §77：RX 段染色覆盖窗（ownerdraw STATIC，≥60% 时可见）
    std::wstring sbTxText_, sbRxText_;       // §120：状态栏 TX/RX 段最近文本（同文本不重写→空闲零重绘）
    HDC grayMem_ = nullptr;                  // §120：菜单栏守卫采样缓存 DIB（一次 BitBlt 替代 18 次 GetPixel）
    HBITMAP grayBmp_ = nullptr;
    void* grayBits_ = nullptr;
    HGDIOBJ grayOld_ = nullptr;
    int grayBw_ = 0, grayBh_ = 0;
    int grayTick_ = 0, grayQuiet_ = 0;       // §120：菜单栏守卫自适应节流（连续干净→1/4 采样）
    UINT dispTickMs_ = 33;                   // §138：显示 tick（id4）当前周期（空闲 33→100→250→500 降档）
    int dispIdle_ = 0;                       // §138：连续空闲早退拍数（降档计步）
    UINT guardTickMs_ = 60;                  // §138：守卫 tick（id15）当前周期（安静且无待发→240）
    HMENU mTermMenu_ = nullptr;              // §140：「终端仿真设置」子菜单句柄（总开关灰/亮定位用）
    HWND findEdit_ = nullptr, findCase_ = nullptr, findHex_ = nullptr, findCount_ = nullptr,
         findPrev_ = nullptr, findNext_ = nullptr;   // findClose_ 已删（§89：查找页无独立 ×）
    std::vector<int> findHits_;       // 命中绝对行号
    int findCur_ = 0;

    // §129 协议文件发送（扩展窗第 5 页；XMODEM/YMODEM 发送端，docs/17 口径）。
    // 会话性功能：协议选择/文件路径不持久化（docs/17 §6.4）；会话激活期对端字节只喂协议机
    // （OnSerialRx/OnNetRx 分流，不进显示管线/转存日志），结束恢复原路径。
    HWND cmb_proto_ = nullptr, cmb_xcheck_ = nullptr, cmb_xblk_ = nullptr,
         protoFileEdit_ = nullptr, protoOpenBtn_ = nullptr, protoStartBtn_ = nullptr,
         protoCancelBtn_ = nullptr, protoStatus_ = nullptr, chk_proto_show_ = nullptr;
    HWND lbl_proto_ = nullptr, lbl_xcheck_ = nullptr, lbl_xblk_ = nullptr;   // 续修4：前置说明
    HWND lbl_proto_timeout_ = nullptr, edit_proto_timeout_ = nullptr,
         lbl_proto_timeout_ms_ = nullptr;                                    // §131：应答超时输入
    HWND lbl_proto_retries_ = nullptr, edit_proto_retries_ = nullptr,
         lbl_proto_retries_c_ = nullptr;                                     // §131 续：超时次数输入
    bool protoActive_ = false;
    ProtoSender proto_;                    // 状态机持有文件内容（会话期驻留）
    int protoKindIdx_ = 0;                 // 协议下拉选中索引（会话性，不持久化；页显形时重设）
    int protoXCheckIdx_ = 0;               // XMODEM 校验方式 0=自动 1=CRC 2=校验和（续修3）
    int protoXBlkIdx_ = 0;                 // 包大小 0=自动 1=1024 2=128（续修4；YMODEM 也生效）
    bool protoShow_ = false;               // 续修5：勾选=会话期收发内容入显示窗口
    std::wstring protoStatusText_;         // 状态行同文本不重写（§120 同型）
    int protoWaitTicks_ = 0;               // 等 ACK 计拍（30ms/拍；阈值=protoTimeoutTicks_）
    int protoTimeoutTicks_ = 33;           // §131：应答超时快照（默认 1000ms≈33 拍；会话启动时取）
    void ProtoTick();                      // 定时器 16（§79 追加 id 末尾）：排空 tx/超时/收尾
    void ProtoPump();                      // §131：事件驱动排空 tx（踢到达即写出+显示，不等拍）
    void ProtoStart();
    void ProtoFinish(const wchar_t* why, bool ok);
    void ProtoUpdateStatus();

    // 阶段 E：多条发送（← PyCom multisend.py）
    struct MsRow {
        std::wstring note, text;
        bool hex = false;
        int order = 0, interval = 1000;
    };
    std::vector<MsRow> msRows_;       // ≤100
    HWND msList_ = nullptr, msEdit_ = nullptr, msLoopBtn_ = nullptr, msAddBtn_ = nullptr,
         msDelBtn_ = nullptr, msImpBtn_ = nullptr;
    int msEditRow_ = -1, msEditCol_ = -1;         // 就地编辑目标格
    bool msLoopActive_ = false;
    int msIdx_ = 0;
    int msPressRow_ = -1;                  // 点击发送按钮的按压视觉（SSCOM 对齐）：130ms 凹陷
                                           //（寿命由定时器 12 控制；原 msPressUntil_ 是只写不读的
                                           // 死字段，§83 审查 R2 删除）
    void MsAddRow(); void MsDelRow(); void MsSyncList();
    void MsLoadFromCfg();                 // §132：启动时从 cfg_.multisend_rows 恢复表格
    void MsSyncToCfg();                   // §132：表格 → cfg_.multisend_rows（增删改即时同步）
    void MsEditCell(int row, int col);
    void MsSendRow(int idx);
    void MsLoopNext();
    void MsImportIni();
    void MsApplyPageLayout(int x, int y, int cw, int ch);
    void MsStretchLastColumn();           // §78：多条表末列拉伸填满右缘（消除"空列"观感）

    // 阶段 E：发送文件（← PyCom txfile.py）
    HANDLE txFile_ = INVALID_HANDLE_VALUE;
    unsigned long long txFileSize_ = 0;
    unsigned long long txFilePos_ = 0;
    bool txFileStalled_ = false;                       // §100：串口背压暂停提示只发一次（恢复写入即清）
    std::vector<uint8_t> txFilePend_;                  // 部分接受余量（G2-3，对齐 FileSender._pend）
    void TxFileTick();

    // 阶段 E：定时发送
    void ApplyTimedSend();

    // 阶段 E：直接写入链路+计数（不回显，PyCom 发送文件口径）
    void SendRawNoEcho(const std::vector<uint8_t>& b);

    static LRESULT CALLBACK NoWheelProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    static int  RxCountThunk(void* ctx);               // H-2 行源：视图行数（含活动行）
    static bool RxGetLineThunk(void* ctx, int viewIdx, RxView::Line& out);
    void RxLineSourceInit();                           // rxv_ 接行源（WM_CREATE 后）
    static void FollowThunk(void* ctx, bool on);
    static void AnnoThunk(void* ctx, bool copyAnno, bool saveAnno);
    static void ClearThunk(void* ctx);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // 阶段 D 壳层
    void ToggleTopmost();
    void NewWindow();
    void OpenDeviceManager();     // §107：小工具「设备管理器」→ ShellExecute devmgmt.msc
    void OpenComputerFolder();    // §107：小工具「文件资源管理器」→ 我的电脑(Win7)/此电脑(Win10+)
    void ApplyCollapse();
    void ToggleFind();
    void FindRescanSoon();
    void FindRescan();
    void FindJump(int dir);
    void RevealAbsLine(int absLine);       // 查找跳转：view = abs - builder.base()（直渲天然落点正确）
    void CreateExtWindow();
    void ToggleExt(bool on);
    void ShowExtPage(int sel);
    void ForceListPaint(HWND list);        // §94：列表+表头显式整窗置脏并同步上屏
    void GrayifyMenuBar();                 // §98g：菜单栏底恒灰 240；唯一白块=被点开的顶项
    bool MenubarNeedsGray();               // §98e：菜单栏采样守卫（悬停直绘场景的 tick 判据）
    int  openedMenuItem_ = -1;             // §98g：当前打开的顶菜单项索引（INITMENUPOPUP 记录，
                                           // EXITMENULOOP/ENTERMENULOOP 清除）
    void RepositionExt();
    void CalcRecompute(int src);
    void FillAsciiList();
    static LRESULT CALLBACK ExtProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleExt(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void LayoutExtPage(int x, int y, int cw, int ch);
    void HandleExtCommand(int id, int code, HWND ctl);
    void HandleExtNotify(NMHDR* nm);

    void BuildMenus();
    void BuildChildren();
    void ApplyFonts();
    void Layout(bool atomic = false);   // atomic=结构变化（查找/收起/DPI）：SETREDRAW 原子化防中间态闪烁
    void Notify(const wchar_t* text);
    void SyncMenuChecks();
    void SaveConfig();
    void OnCommandImpl(int id, int code);

    // M0.5 spike 接线
    void RefreshPorts();                    // SetupDi 枚举 → 端口下拉（"COMx - 描述"）
    void RefreshNics();                     // GetAdaptersAddresses → 网卡下拉（全部网卡 + 各接口）
    void RefreshLocalIps();                 // 按网卡选择填本地 IP（联动，PyCom 语义）
    void ApplyModeColumns();                // 模式 → 左列串口区/网络区互斥显示
    SerialParams GatherParams();            // 从控件收集串口参数（纯读，无副作用）
    void SyncSerialCfg();                   // 控件 → cfg 回写（仅打开成功/保存参数时，P3-3）
    void TryOpenSerial(bool allowLoopback); // 打开（true=失败回退软件回环）
    void CloseSerial();
    // M4 网络接线（← PyCom net_link.py + 网络面板）
    void TryOpenNet();                      // 连接/侦听按钮（client 异步连，server/udp 同步开）
    void CloseNet();                        // 断开按钮（撤重连意图 + 关引擎）
    void CloseLink();                       // 按当前模式关链路（模式切换/断开共用）
    bool LinkIsOpen() const;                // 模式感知的链路打开态（发送路径共用）
    int  LinkWrite(const uint8_t* d, size_t n);   // 模式感知写入：serial=全量接受，net=实际接受量
    void OnNetState(bool open);             // kMsgNetSt → 镜像 OnSerialState（重连/待发）
    void OnNetEvent(int code, const wchar_t* text);
    void OnNetRx(const uint8_t* d, size_t n);
    NetParams GatherNetParams() const;      // 网络区控件 → NetParams（纯读）
    void SyncNetCfg();                      // 控件 → cfg 回写（打开成功/保存参数时）
    std::wstring DescribeLink() const;      // 状态栏连接信息（PyCom describe() 口径）
    void ApplyNetLocks();                   // 模式锁死联动（PyCom _sync_net_locks）
    void EnterReconnect();                  // 进入自动重连（timer7 驱动，按钮=「取消」）
    void CancelReconnect();                 // 用户点「取消」中止重连
    void ReopenSerial();                    // 参数变更自动重开（← PyCom _apply_live_baud）
    void ApplyDtrRts();                     // DTR/RTS 热改（EscapeCommFunction）
    void PollPins();                        // 300ms：GetCommModemStatus 点亮引脚灯
    void EnqueueTx(const std::vector<uint8_t>& b, bool record);
    size_t TxPendLen() const;
    void FlushTxPend();                     // 连接恢复/已连接时排空待发
    void OnSerialRx(const uint8_t* d, size_t n);
    void OnSerialEvent(int code, const wchar_t* text);
    void OnSerialState(bool open);
    void OnFollowChanged(bool on);                     // 用户回看取消/恢复自动滚屏 → 同步勾选框
    void OnAnnoToggled(bool copyAnno, bool saveAnno);  // 右键两开关 → 持久化
    void OnClearFromView();                            // 右键清除窗口 → 清显示管线
    void LoopbackDemo();                               // 小工具→回环演示：软件回环 + 自动发一串
    void SendBytes(const std::vector<uint8_t>& bytes);
    // §110：终端仿真（M6 首增量）——接收区直接输入。echo=false 时不入显示管线（按键模式未开回显）
    void SendBytesEx(const std::vector<uint8_t>& bytes, bool echo);
    void TermSend(const wchar_t* text, int len, int kind);   // kind 0=文本 1=回车 2=退格
    static void TermSinkThunk(void* ctx, const wchar_t* text, int len, int kind);
    void ApplyTerminal();                                    // cfg → 视图（方式/回显）
public:
    void SendCurrentEdit();                            // 发送内容框（SendEditProc Ctrl+Enter 回调需公开）
    void SendConvertSelection(bool toHex);             // §54 发送框右键：选区 ↔ HEX（当前编码）
    void SendContextMenu(POINT ptScreen);              // §54 发送框右键菜单（RBUTTONUP 拦截入口）
private:
    void TickDisplay();                                // 33ms：builder flush/trim → SyncBase+脏标记（直渲无推入）
    void RequestFastDisplay();                         // §138：空闲降档后由活动拉回快拍（id4/id15）
    void ApplyViewColors();
    void UpdateConnStatus();
    void StopTimedSend();                  // §70 定时发送随连接关闭/断开停止（按钮复位）
    void UpdateCounters();
    void ResetCounters();                 // §103（用户拍板）：关闭/换口/切 TCP-UDP → RX/TX 计数清零并即时刷新
    void SyncTintOverlay();               // §77：RX 段染色覆盖窗定位/显隐（UpdateCounters 每拍调用）
    static std::wstring TimestampNow();
    void SelftestInit();
    void SelftestTick();
    // M2-A：转存与保存
    void ToggleRxLog(bool on);             // 「接收数据到文件」勾选即生效
    void RxLogPoll();                      // 100ms：增量写已完成行
    void RxLogFlushTail();                 // 未收尾行补写 + 行终止符
    void DetachRxLog();                    // 关句柄 + 复位游标（幂等）
    bool WriteLogText(const std::wstring& s);  // 按当前编码写日志文件
    bool WriteAllTo(HANDLE h, const std::wstring& s);  // 编码文本 → 句柄（转存/保存共用）
    void SaveReceived();                   // 「保存数据」：received.txt / received.bin
    void ClearDisplayAll();                // 清除窗口（管线 + 视图 + 游标）
    // M1 收尾
    std::vector<uint8_t> CurrentSendPayload();          // 发送内容框 → 字节（HEX/编码）
    void ChecksumParamsNow(std::wstring& kind, int& start, int& tail);  // 从控件读校验参数
    void SyncChecksumCfg();                 // 控件 → cfg（持久化口径）
    void UpdateChecksumPreview();          // 校验值实时预览（只读框）
    void ShowFontColorDialog();            // 字体与颜色（ChooseFont：正文颜色 + 字体样式）
    void ShowBgColorDialog();              // §67 背景颜色（独立菜单项 → ChooseColor）
    void ShowTsStyleDialog();              // §67 时间戳样式（色块点击：颜色/加粗/倾斜/下划线）
    void ApplyFontColors();                // 颜色应用到视图（字体走 ApplyFonts）

    void MakeButton(HWND& h, int id, const wchar_t* text, DWORD extra = 0);
    void DisableComboWheel(HWND combo);    // 输入类下拉禁滚轮（用户反馈：滚轮只可手动调整）
    void MakeCheck(HWND& h, int id, const wchar_t* text, bool checked);
    void MakeCombo(HWND& h, int id, DWORD extra = 0);
    void MakeEdit(HWND& h, int id, const wchar_t* text, DWORD extra = 0);
    void MakeLabel(HWND& h, int id, const wchar_t* text);
    void MakePin(HWND& h, int id, const wchar_t* name);   // §73 显式名字（BRK id 不连续）

    // 度量（外壳字体实测，M1 标定口径）
    int TextW(const wchar_t* s) const;
    int BtnW(const wchar_t* s) const;
    int ChkW(const wchar_t* s) const;

    std::wstring CurrentPortName() const;   // 下拉选中项的纯端口名（审查 P0-1）

    static COLORREF ParseColor(const std::wstring& s, COLORREF def);
};

} // namespace sncom
