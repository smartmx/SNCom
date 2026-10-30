// sncom/src/ui/rxview.h —— 自绘接收视图（H-2：直渲 builder，零行表拷贝）
// 双缓冲渲染 + 行滚动 + 选区复制 + 右键菜单 + 边缘自动滚屏定时器。
// 数据源 = 宿主注入的行源回调（mainwindow → LineBuilder.line_at，含未收尾活动行）——
// 视图不再持有行表副本（原 50 万/10 万行双份内存问题彻底消除），可见历史 = 保存/查找
// 范围（builder 现存 50 万行），查找跳转/键盘滚屏/选区坐标一律 viewIdx（= abs - base），
// builder 裁剪推进时宿主调 SyncBase 平移内部坐标。
// 宽度缓存（cw）按绝对行号驻留 map（长行仅可见窗口驻留，TrimWidthTables 同款策略）。
// 历史注：终端输入行与 IME 定位已于 2026-10-25 拍板移除（docs/00 §21.1）。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace sncom {

class RxView {
public:
    static bool RegisterClass(HINSTANCE hInst);
    bool Create(HWND parent, int id, HINSTANCE hInst);

    HWND hwnd() const { return hwnd_; }

    void SetFont(HFONT f);
    // §68：收/发时间戳字体 = 显示字体 + 各自样式变体（粗/斜/下划线由宿主派生），
    // 家族/字号与正文同源；前缀段（ts+mark）按行方向取用。
    void SetTsFonts(HFONT rx, HFONT tx);
    // bg=背景 text=正文 ts=接收时间戳色 txDir=发送时间戳色 rxDir=弃用(传同 ts) sel=选区底色
    void SetColors(COLORREF bg, COLORREF text, COLORREF ts, COLORREF txDir, COLORREF rxDir, COLORREF sel);

    // 行源（H-2 直渲）：宿主提供行数与取行（viewIdx ∈ [0, Count)，含末尾活动行）。
    // thunk 填 ts/mark/body/isTx；颜色由视图按 isTx 上（colTs/colTx/colRx/colText）。
    struct Line {
        std::wstring ts, mark, body;                // 显示串 = ts + mark + body
        COLORREF tsCol = 0, markCol = 0, bodyCol = 0;
        bool hasColor = false;
        bool isTx = false;
        std::vector<int> cw;                        // 累计宽度缓存（懒算；字体变 → 清空）
        const std::wstring text() const { return ts + mark + body; }
    };
    typedef int  (*CountFn)(void* ctx);                       // 视图行数
    typedef bool (*GetFn)(void* ctx, int viewIdx, Line& out); // 取行（false=越界）
    void SetLineSource(void* ctx, CountFn cnt, GetFn get) {
        countCtx_ = ctx; countFn_ = cnt; getCtx_ = ctx; getFn_ = get;
    }
    // builder 裁剪推进 base（viewIdx 原点前移）→ 平移视图内部坐标、清理失效宽度缓存
    void SyncBase(int newBase);
    // 宿主数据变化（feed/flush/trim/clear 后调用）：增量建显示行索引 + follow 跟底 + 滚动条
    void NotifyDataChanged();
    // 全量重排（折行宽度/字体/行文本口径变化时由宿主调用，如 split 切换）
    void Reflow();

    void Clear();                                   // 清选区 + 回底（数据清除在宿主 builder）

    void SetFollow(bool on);                        // 自动滚屏（勾选 → 跳回最底）
    bool IsFollowing() const { return follow_; }
    bool FollowNow() const { return follow_ || termFollow_ || protoFollow_; }   // §126/§129：含终端输入期与协议会话跟随
    void RevealLine(int viewLine);                  // 跳转到指定视图行（查找定位，C2）
    void SetHexUnits(bool on);                      // HEX 显示模式切换（§64：单元对齐折行开关，内部重排）

    // §110 终端仿真（M6 首增量，docs/03 §D）：接收区直接输入。
    // 键盘文本经 termSink_ 交宿主（宿主负责编码/写链路/回显入显示管线）；本视图只维护
    // "输入尾巴"（回车模式下未发送的整行，画在末行末尾——不入行表、不参与折行/滚动，
    // 空屏时画左上角(§111)，回看（底部行不在屏内）时隐藏）与光标（竖线，聚焦显示）。默认关闭 → 既有行为零影响。
    // kind：0=文本（按当前编码发送）1=回车键（宿主按设置发 \r[+\n]，回车模式下整行）2=退格键（0x08）
    typedef void (*TermFn)(void* ctx, const wchar_t* text, int len, int kind);
    void SetTermSink(void* ctx, TermFn fn) { termCtx_ = ctx; termFn_ = fn; }
    void SetTerminal(bool on, bool keyMode, bool echo);

    // 自动滚屏跟随态回调：用户回看（滚轮上翻/拖选/拖条）取消时通知宿主同步勾选框
    typedef void (*FollowFn)(void* ctx, bool on);
    void SetFollowSink(void* ctx, FollowFn fn) { followCtx_ = ctx; followFn_ = fn; }

    // 右键菜单两开关（复制/保存含时间戳和收发标记，默认开）与清除回调（宿主持久化+清管线）
    bool copy_with_anno = true, save_with_anno = true;
    typedef void (*AnnoFn)(void* ctx, bool copyAnno, bool saveAnno);
    typedef void (*ClearFn)(void* ctx);
    void SetMenuSinks(void* ctx, AnnoFn anno, ClearFn clear) { menuCtx_ = ctx; annoFn_ = anno; clearFn_ = clear; }

private:
    bool GetLine(int viewIdx, Line& out);           // 取行 + 上色（包装 getFn_）
    void TrimWidthTables(int first, int last);      // 长行宽表仅可见窗口驻留

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT tsRxFont_ = nullptr;                      // §68 接收时间戳字体（宿主派生，宿主持有）
    HFONT tsTxFont_ = nullptr;                      // §68 发送时间戳字体
    HINSTANCE inst_ = nullptr;
    // 行源（H-2）
    void* countCtx_ = nullptr;
    CountFn countFn_ = nullptr;
    void* getCtx_ = nullptr;
    GetFn getFn_ = nullptr;
    int  base_ = 0;                                 // viewIdx 原点（= builder.base()，SyncBase 同步）
    std::map<int, std::vector<int>> cwCache_;       // abs 行号 → 宽度表（字体变 → 全清）
    mutable std::map<wchar_t, int> charW_;          // 单字符宽度缓存（随字体清空；const 走段器亦写缓存）
    mutable std::map<wchar_t, int> tsRxCharW_;      // §68 接收前缀字符宽缓存（SetTsFonts 清）
    mutable std::map<wchar_t, int> tsTxCharW_;      // §68 发送前缀字符宽缓存
    int lineH_ = 16, textAscent_ = 12;
    int firstLine_ = 0;                             // 首个可见显示行（§62 折行：显示行坐标，非逻辑行）
    bool follow_ = true;
    bool dirty_ = false;
    int  edgeDir_ = 0;                        // 拖选边缘自动滚屏方向（0=停，±1）
    int  prevFirst_ = -1, prevLast_ = -1;     // 上次绘制窗口（长行宽表清理对称差；viewIdx）
    COLORREF colBg_ = RGB(0xFF, 0xFF, 0xFF), colText_ = RGB(0, 0, 0),
             colTs_ = RGB(0x80, 0x80, 0x80), colTx_ = RGB(0x15, 0x65, 0xC0),
             colRx_ = RGB(0x1B, 0x7A, 0x1B), colSel_ = RGB(0x33, 0x99, 0xFF);

    // 选区（line 坐标 = viewIdx）
    bool selecting_ = false;
    bool hasSel_ = false;
    struct Pos { size_t line, ch; };
    Pos selA_{}, selB_{};

    // 回调
    void* followCtx_ = nullptr;
    FollowFn followFn_ = nullptr;
    void* menuCtx_ = nullptr;
    AnnoFn annoFn_ = nullptr;
    ClearFn clearFn_ = nullptr;

    static LRESULT CALLBACK ProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // ---- 折行（§65 格数制：容量按字符数，HEX 固定 3 格单元对齐）----
    // pre_[k] = 视图行 [0,k) 折行后的显示行数（pre_[0]=0，size=逻辑行数+1）；
    // 滚动/绘制/命中的"行"一律 = 显示行；选区/复制/查找跳转仍用逻辑行+字符（语义零改动）。
    // 模型（用户拍板 §65）：① cellW_=单 ASCII 字符格宽，容量 capCells_=客户区宽
    // 可容纳格数；② 数据按格数装行、行末截断换行；③ HEX 显示模式单元固定 3 格
    // （"XX "），行尾不足 3 格直接换行、切点只在单元边界（单元不劈开）；④ 首行前缀
    // （时间戳+标记）占格向上补齐到 3 的倍数，数据从补齐处起——上下行数据同处
    // 3 格网格，开关分包标记不影响对齐。文本模式 = 字符级贪心装格。
    std::vector<int> pre_;
    int  totalRows_ = 0;                // = pre_.back()
    int  wrapCap_ = 0;                  // 客户区宽 px（resize 防抖比较用）
    int  cellW_ = 0;                    // 单 ASCII 字符格宽 px（SetFont 量取）
    int  capCells_ = 0;                 // 折行容量（格）= wrapCap_/cellW_（下限 6）
    int  lastLenSeen_ = -1;             // 活动行上次测量时的文本长（增长才重量）
    bool reflowPend_ = false;
    bool hexUnits_ = false;             // HEX 显示模式（宿主经 SetHexUnits 同步）
    int  MeasureRows(const Line& l, int cap);           // 折行段数（cap=格）
    int  HeadOf(const Line& l) const;   // 行首前缀字符数（ts+mark）
    int  CellX(int px) const {          // 累计像素 → 格号（向上取整；cw 同源不漂移）
        return cellW_ > 0 ? (px + cellW_ - 1) / cellW_ : px;
    }
    void UpdateCapCells();              // wrapCap_/cellW_ → capCells_（cellW_ 缺时现场量）
    void ReflowAll();
    void AppendNewRows();
    void UpdateLastLineRows();
    int  LineOfRow(int dispRow) const;  // 二分：显示行 → 逻辑视图行
    // 折行走段通用入口：对 l 的每个显示行回调 fn(行首字符, 行末字符)（false=停止）。
    // head = 行首非折行前缀字符数（时间戳+标记，仅首显示行携带）。
    void WalkWrapRows(const Line& l, int cap,
                      const std::function<bool(int, int)>& fn) const;

    // 内部
    void MarkDirty() { dirty_ = true; }
    void ScrollTo(int first);
    void ScrollBy(int delta);
    int  MaxFirst() const;
    int  VisibleRows() const;
    int  TotalRows() const { return totalRows_; }   // 显示行总数（折行后）
    void UpdateScrollbar();
    void EnsureMetrics(Line& l, int absKey);
    void CopySelection();
    std::wstring SelectedText() const;
    static bool PosLess(const Pos& a, const Pos& b);
    Pos  HitTest(int x, int y) const;

    // §110 终端输入尾巴/光标（画在末显示行末尾；末行不在屏内则不画）
    void DrawTermTail(HDC mem, int clientW);
    // §112：尾巴布局（按客户区宽度折行的多个绘制段 + 段末光标位置；IME 定位与绘制共用）
    struct TermSeg { int x, y, off, len; };
    void TermTailLayout(HDC dc, int clientW, std::vector<TermSeg>& segs, int& caretX, int& caretY);
    void TermImeSyncPos();                          // §112：组合窗/候选窗贴尾巴末尾（IME 跟随光标）
    bool TermInputChar(wchar_t c);                  // §112：单字符入终端通道（WM_CHAR 与 IME 提交共用）
    void TermBlinkSet(bool on);                     // §112：光标闪烁定时器（焦点获得/失去）
public:
    void ProtoFollow(bool on);                      // §129 续修5：协议会话显示期 → 跟随底部（会话结束/取消勾选解除）
private:
    void TermFollowTouch();                         // §126：终端键入 → 临时跟随（覆盖手动上滚，不动勾选/配置）
    int  TailRows() const;                          // §126：回车模式输入尾巴占位行数（0/1）
    int  FollowFirst() const;                       // §126：跟随态目标 firstLine（含尾巴行，内容少于一屏时为 0）
    bool termBlinkOn_ = true;                       // §112：闪烁相位（聚焦瞬间可见）
    bool termFollow_ = false;                       // §126：终端输入期临时跟随（2s 静默/失焦清除；follow_ 从未被改动）
    bool protoFollow_ = false;                      // §129 续修5：协议会话「显示收发内容」期间视图跟随底部
    void TermKeySend(const wchar_t* s, int len, int kind) {
        if (termFn_) termFn_(termCtx_, s, len, kind);
    }
    bool termOn_ = false, termKeyMode_ = false, termEcho_ = false;
    std::wstring termBuf_;                          // 回车模式下未发送的行（本地回显=尾行文字）
    wchar_t termPendSur_ = 0;                       // 按键模式代理对暂存（半对不发送）
    void* termCtx_ = nullptr;
    TermFn termFn_ = nullptr;
};

} // namespace sncom
