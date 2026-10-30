// sncom/src/ui/rxview.cpp —— 自绘接收视图（H-2：直渲 builder，零行表拷贝）
// 数据源 = 宿主行源回调（SetLineSource）；视图只持滚动位置/选区/宽度缓存。
#include "rxview.h"
#include <windowsx.h>
#include <imm.h>                            // §112 IME：组合窗/候选窗跟随光标
#include <cwchar>

namespace sncom {

static const UINT kTimerMerge = 1;       // 33ms 合并刷新（≈30fps，对应 PyCom rxview）
static const UINT kTimerEdge = 2;        // 拖选边缘自动滚屏（C2：PyCom 用定时器）
static const UINT kTimerReflow = 3;      // 折行重排防抖（resize 拖动期间沿用旧宽，释放 150ms 后重排）
static const UINT kTimerBlink = 4;       // §112 终端光标闪烁（随焦点起停）
static const UINT kTimerTermFollow = 5;  // §126：终端输入期临时跟随的静默回退（2s 无键入）
static const int kLongLine = 200;        // 超过此字符数的行 = 长行（宽表仅可见窗口驻留）
static int HexHeadCells(const std::vector<int>& cw, int cellW, int head);   // §110：前向声明（定义在后）

// ---------------- 注册与创建 ----------------
bool RxView::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ProcThunk;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = nullptr;                       // 全部自绘（双缓冲）
    wc.lpszClassName = L"SNComRxView";
    return RegisterClassExW(&wc) != 0;
}

bool RxView::Create(HWND parent, int id, HINSTANCE hInst) {
    inst_ = hInst;
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"SNComRxView", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
                            0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, this);
    return hwnd_ != nullptr;
}

// ---------------- 字体 / 颜色 / 行源 ----------------
void RxView::SetFont(HFONT f) {
    font_ = f;
    if (!hwnd_ || !f) return;
    HDC dc = GetDC(hwnd_);
    HFONT old = (HFONT)SelectObject(dc, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SIZE xsz{};
    GetTextExtentPoint32W(dc, L"X", 1, &xsz);   // 格宽 = 单 ASCII 字符宽（§65 格数制）
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    lineH_ = tm.tmHeight + 2;
    textAscent_ = tm.tmAscent;
    cellW_ = xsz.cx > 2 ? xsz.cx : 6;
    charW_.clear();
    cwCache_.clear();                                 // 字体变 → 全部宽度缓存失效
    KillTimer(hwnd_, kTimerReflow);
    reflowPend_ = false;
    ReflowAll();                                      // 字体变 → 全量重排（§62）
}

// §68：收/发时间戳字体（宿主由显示字体+各自粗/斜/下划线派生）——前缀按行方向取用
void RxView::SetTsFonts(HFONT rx, HFONT tx) {
    tsRxFont_ = rx;
    tsTxFont_ = tx;
    if (!hwnd_) return;
    tsRxCharW_.clear();
    tsTxCharW_.clear();
    cwCache_.clear();                              // 前缀宽随 ts 字体变 → 整行宽表失效
    KillTimer(hwnd_, kTimerReflow);
    reflowPend_ = false;
    ReflowAll();
}

void RxView::SetColors(COLORREF bg, COLORREF text, COLORREF ts, COLORREF txDir, COLORREF rxDir, COLORREF sel) {
    colBg_ = bg; colText_ = text; colTs_ = ts; colTx_ = txDir; colRx_ = rxDir; colSel_ = sel;
    MarkDirty();
}

bool RxView::GetLine(int viewIdx, Line& out) {
    if (!getFn_) return false;
    if (!getFn_(getCtx_, viewIdx, out)) return false;
    out.tsCol = out.markCol = out.isTx ? colTx_ : colTs_;   // §68：[ts]+标记整体一色，收/发各自一份
    out.bodyCol = colText_;
    out.hasColor = true;
    return true;
}

void RxView::SyncBase(int newBase) {
    if (newBase == base_) return;
    int shift = newBase - base_;
    base_ = newBase;
    // 被裁掉的行（abs < newBase）宽度缓存成垃圾：清掉（一次性，突发时量大但 O(n) 一次）
    if (shift > 0) cwCache_.erase(cwCache_.begin(), cwCache_.upper_bound(newBase - 1));
    // 折行索引同步（§62）：头部 shift 行的显示行从索引移除，其余条目平移；
    // firstLine_（显示行坐标）同步平移；逻辑行坐标（选区）平移见下方原有逻辑
    if (shift > 0) {
        if ((int)pre_.size() > shift) {
            int erased = pre_[(size_t)shift];
            pre_.erase(pre_.begin(), pre_.begin() + shift);
            for (auto& v : pre_) v -= erased;
            totalRows_ = pre_.back();
            firstLine_ = firstLine_ > erased ? firstLine_ - erased : 0;
        } else {
            pre_.assign(1, 0);
            totalRows_ = 0;
            firstLine_ = 0;
        }
    }
    // 视图内逻辑行坐标（选区 = 逻辑行+字符）整体 -shift；被裁掉（abs < newBase）则钳 0
    if (prevFirst_ >= 0) {
        prevFirst_ = prevFirst_ > shift ? prevFirst_ - shift : 0;
        prevLast_ = prevLast_ > shift ? prevLast_ - shift : 0;
    }
    if (hasSel_) {
        bool aGone = (long long)selA_.line < (long long)shift;
        bool bGone = (long long)selB_.line < (long long)shift;
        if (aGone) selA_ = Pos{ 0, 0 };
        else selA_.line -= shift;
        if (bGone) selB_ = Pos{ 0, 0 };
        else selB_.line -= shift;
        if (aGone || bGone) hasSel_ = false;          // 选区跨被裁行：取消（PyCom 同款）
    }
    if (FollowNow()) firstLine_ = FollowFirst();
    UpdateScrollbar();
    MarkDirty();
}

void RxView::NotifyDataChanged() {
    if (wrapCap_ == 0) {                              // 首次：取客户区宽（无 WM_SIZE 前的数据）
        RECT rc{}; GetClientRect(hwnd_, &rc);
        wrapCap_ = rc.right > 50 ? rc.right : 50;
        UpdateCapCells();
    }
    AppendNewRows();
    UpdateLastLineRows();
    // §126：终端输入期收到数据（对端回复等）也续期临时跟随——回车发出后对端回复常在键入
    // 静默 2s 之后才到，不续期则回复行与光标空行停在视口之下（SSCOM 基准：收发后光标恒在
    // 底部空行）。仅在跟随已激活时续期，不打扰明确上滚回看的用户（follow_/termFollow_ 双灭）。
    if (termOn_ && termFollow_) TermFollowTouch();
    if (FollowNow()) firstLine_ = FollowFirst();
    UpdateScrollbar();
    MarkDirty();
}

void RxView::Reflow() {
    ReflowAll();
}

void RxView::SetHexUnits(bool on) {
    if (hexUnits_ == on) return;
    hexUnits_ = on;
    ReflowAll();                                       // 切分口径变化 → 全量重排
}

void RxView::Clear() {
    firstLine_ = 0;
    hasSel_ = false;
    cwCache_.clear();
    pre_.assign(1, 0);
    totalRows_ = 0;
    lastLenSeen_ = -1;
    if (FollowNow()) firstLine_ = FollowFirst();
    UpdateScrollbar();
    MarkDirty();
}

void RxView::RevealLine(int viewLine) {
    // 查找跳转（逻辑行）→ 该逻辑行首个显示行滚到可见区上 1/3 处
    int disp = 0;
    if (viewLine >= 0 && viewLine < (int)pre_.size()) disp = pre_[(size_t)viewLine];
    else disp = MaxFirst();
    SetFollow(false);
    ScrollTo(disp - VisibleRows() / 3);
}

void RxView::SetFollow(bool on) {
    if (follow_ == on) return;
    follow_ = on;
    if (followFn_) followFn_(followCtx_, follow_);    // 宿主同步勾选框/配置
    if (on) { firstLine_ = MaxFirst(); UpdateScrollbar(); MarkDirty(); }
}

// ---------------- §110 终端输入（M6 首增量） ----------------
void RxView::SetTerminal(bool on, bool keyMode, bool echo) {
    // echo 只影响宿主（回显入管线）——本视图绘制只看 on/keyMode；开关或模式变化才重绘
    if (on == termOn_ && keyMode == termKeyMode_) { termEcho_ = echo; return; }
    termOn_ = on;
    termKeyMode_ = keyMode;
    termEcho_ = echo;
    if (!termOn_) { termBuf_.clear(); termPendSur_ = 0; TermBlinkSet(false); }
    else if (GetFocus() == hwnd_) TermBlinkSet(true);   // 开启即聚焦 → 光标闪烁起
    MarkDirty();
}

void RxView::DrawTermTail(HDC mem, int clientW) {
    // 尾巴/光标画在「末显示行」末尾（终端语义：输入跟在最后一个字符之后）。
    // 尾巴不入行表、不参与折行与滚动——末行不在可见带内（用户回看）时不画。
    std::vector<TermSeg> segs;
    int cx = 0, cy = -1;
    TermTailLayout(mem, clientW, segs, cx, cy);
    if (cy < 0) return;                                 // 尾巴隐藏（未开终端/末行滚出屏）
    SetTextColor(mem, colText_);
    if (!segs.empty()) {
        HFONT of = font_ ? (HFONT)SelectObject(mem, font_) : nullptr;
        const int baseAdj = textAscent_ + 1;
        for (const TermSeg& s : segs)
            TextOutW(mem, s.x, s.y + baseAdj, termBuf_.c_str() + s.off, s.len);
        if (of) SelectObject(mem, of);
    }
    if (GetFocus() == hwnd_ && termBlinkOn_ && clientW > 4) {
        int x = cx;
        if (x > clientW - 2) x = clientW - 2;
        RECT cr = { x, cy + 2, x + 1, cy + lineH_ - 2 };
        HBRUSH cb = CreateSolidBrush(colText_);
        FillRect(mem, &cr, cb);
        DeleteObject(cb);
    }
}

// §112：输入尾巴「按行宽折行」布局。返回各绘制段（客户区坐标，y=行顶）与段末光标（行顶）。
// 尾巴隐藏（未开终端/空屏无内容/末行滚出可见带）时 segs 为空且 cy<0。绘制与 IME 定位共用。
void RxView::TermTailLayout(HDC dc, int clientW, std::vector<TermSeg>& segs, int& caretX, int& caretY) {
    segs.clear();
    caretX = 0; caretY = -1;
    if (!termOn_) return;
    const bool hasTail = !termBuf_.empty();
    if (totalRows_ <= 0 && !hasTail) return;
    // §111 P2-2：空屏（一行数据都没有）时尾巴要画在左上角——totalRows_==0 时末显示行视为 0，
    // 且「末行滚出可见带」的隐藏判定只在真有行时生效（否则 -1 < firstLine_ 恒真 → 空屏什么都不画）。
    int lastDisp = totalRows_ > 0 ? totalRows_ - 1 : 0;
    // §126（用户拍板）：回车模式的输入尾巴 = **下一空行行首**（上行已随回车发出并收行，
    // 光标落新行——终端语义；此前画在末显示行末尾会粘在回显行尾）。按键模式仍贴末行末尾。
    const bool newRow = totalRows_ > 0 && !termKeyMode_;
    if (totalRows_ > 0 && lastDisp + (newRow ? 1 : 0) < firstLine_) return;   // 尾巴行滚出可见带 → 隐藏
    int y0 = (lastDisp - firstLine_ + (newRow ? 1 : 0)) * lineH_;
    int x0 = 0;
    if (totalRows_ > 0 && !newRow) {
        int k = LineOfRow(lastDisp);
        Line l;
        if (GetLine(k, l)) {
            EnsureMetrics(l, base_ + k);
            const std::vector<int>& cw = l.cw;
            int n = (int)cw.size() - 1;
            int rowBase = (k < (int)pre_.size()) ? pre_[(size_t)k] : 0;
            int startRow = lastDisp - rowBase;          // 该逻辑行的第几个显示行
            int head = HeadOf(l);
            if (head > n) head = n;
            int rs = 0, re = 0, hp = 0, padPx = 0;
            bool firstRow = false;
            int idx = 0;
            WalkWrapRows(l, capCells_, [&](int a, int b) -> bool {
                if (idx == startRow) {
                    rs = a;
                    re = b;
                    firstRow = (idx == 0);
                    hp = firstRow ? HexHeadCells(cw, cellW_, head) : 0;
                    if (firstRow && head > 0 && cellW_ > 0) {
                        padPx = hp * cellW_ - cw[head];
                        if (padPx < 0) padPx = 0;
                    }
                    return false;
                }
                ++idx;
                return true;
            });
            if (hexUnits_ && rs >= head)
                x0 = (firstRow ? hp : 0) * cellW_ + (re - (firstRow ? head : rs)) * cellW_;
            else
                x0 = (firstRow ? padPx : 0) + (cw[re] - cw[rs]);
        }
    }
    if (!hasTail) { caretX = x0; caretY = y0; return; }   // 空尾巴：光标停在起始位
    // 折行：首行从 x0 起，换行后回到 0；纵向越出客户区（行顶在底边之上）的段不再布局
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const int bottom = rc.bottom;
    HFONT of = font_ ? (HFONT)SelectObject(dc, font_) : nullptr;
    int off = 0, n = (int)termBuf_.size(), x = x0, y = y0;
    while (off < n && y <= bottom - 4) {
        int avail = clientW - x - 2;
        int fit = 0;
        if (avail >= 4) {
            SIZE sz{};
            GetTextExtentExPointW(dc, termBuf_.c_str() + off, n - off, avail, &fit, nullptr, &sz);
        }
        if (fit <= 0) {
            if (x > 0) { x = 0; y += lineH_; continue; }  // 行尾放不下 → 换行重试
            break;                                        // 整行放不下一个字（极窄窗）→ 停止
        }
        if (off + fit < n && fit > 1 &&
            termBuf_[(size_t)(off + fit)] >= 0xDC00 && termBuf_[(size_t)(off + fit)] <= 0xDFFF)
            --fit;                                        // 不把代理对切在两行之间
        SIZE esz{};
        GetTextExtentPoint32W(dc, termBuf_.c_str() + off, fit, &esz);
        segs.push_back(TermSeg{ x, y, off, fit });
        caretX = x + esz.cx;
        caretY = y;
        off += fit;
        x = 0; y += lineH_;
    }
    if (of) SelectObject(dc, of);
}

// §112：组合窗/候选窗跟随光标（IME 中文输入）。位置=尾巴段末；回看中尾巴隐藏时贴底。
void RxView::TermImeSyncPos() {
    if (!termOn_ || !hwnd_) return;
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;
    RECT rc{}; GetClientRect(hwnd_, &rc);
    HDC dc = GetDC(hwnd_);
    std::vector<TermSeg> segs;
    int cx = 0, cy = -1;
    TermTailLayout(dc, rc.right, segs, cx, cy);
    ReleaseDC(hwnd_, dc);
    if (cy < 0) cy = rc.bottom - lineH_;               // 尾巴隐藏（回看中）→ 贴底，别跑到屏外
    if (cx > rc.right - 8) cx = rc.right - 8;
    if (cy > rc.bottom - lineH_) cy = rc.bottom - lineH_;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (font_) {                                       // 组合窗字形 = 接收字体（与尾巴一致）
        LOGFONTW lf{};
        if (GetObjectW(font_, sizeof(lf), &lf)) ImmSetCompositionFontW(himc, &lf);
    }
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = cx;
    cf.ptCurrentPos.y = cy;
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM cd{};
    cd.dwIndex = 0;
    cd.dwStyle = CFS_CANDIDATEPOS;                     // 候选窗贴光标
    cd.ptCurrentPos.x = cx;
    cd.ptCurrentPos.y = cy;
    ImmSetCandidateWindow(himc, &cd);
    CANDIDATEFORM ce{};
    ce.dwIndex = 0;
    ce.dwStyle = CFS_EXCLUDE;                          // 且避开尾巴占位矩形
    ce.ptCurrentPos.x = cx;
    ce.ptCurrentPos.y = cy;
    ce.rcArea.left = cx; ce.rcArea.top = cy;
    ce.rcArea.right = cx + 8; ce.rcArea.bottom = cy + lineH_;
    ImmSetCandidateWindow(himc, &ce);
    ImmReleaseContext(hwnd_, himc);
}

// §112：单字符入终端通道（WM_CHAR 与 IME 提交路径共用；吞掉不可见控制字符）。
// 返回 false = 未消费（终端关闭或特殊键——调用方决定是否交回默认处理）。
// §126：终端键入期「自动滚屏一直有效」——用户此前上翻回看时，键入要立刻跳回底部跟随，
// 否则尾巴/光标画在屏外等于没输入（用户报）。实现=**临时跟随旗 termFollow_**：不打扰
// follow_（勾选框/配置/宿主回调零接触），跟随判定统一走 FollowNow()；2s 无键入（期间
// 收发数据会续期，见 NotifyDataChanged）或失焦清除临时跟随（停留当前位置；follow_ 本身
// 从未被改动，无需还原——§126 续2 复核删除原 prevFollow_ 死状态，R21 P3-2）。
// §126：回车模式输入尾巴占「下一空行」（TermTailLayout 新行语义）——该行不在 totalRows_
// 行表内，但视口必须容纳它（否则被底边裁半：VisibleRows=clientH/lineH 向下取整，
// MaxFirst+1 行正好落进余数区）。按键模式尾巴贴末行末尾不占行；空屏画左上角不占行。
int RxView::TailRows() const {
    return (termOn_ && !termKeyMode_ && totalRows_ > 0) ? 1 : 0;
}

// §126：跟随态目标视口首行 = 有效总行数（含尾巴占位行）- 可见行数，下限 0。
// 直接 MaxFirst()+TailRows() 会在「内容少于一屏」时过度滚动（把仅有几行滚出视口——
// 实测 AB 行滚出只剩尾巴一条带）。内容+尾巴不足一屏时回 0（全部可见）。
int RxView::FollowFirst() const {
    int eff = totalRows_ + TailRows();
    int mf = eff - VisibleRows();
    return mf > 0 ? mf : 0;
}

// §129 续修5：协议会话「将收发内容显示到窗口」勾选期间视图跟随底部（否则自动滚屏关闭时
// 新内容在视口下方堆积，用户感知=「没实时显示」）。会话结束/取消勾选解除跟随（停留当前位置，
// 与 termFollow 还原语义一致）。follow_/勾选框零接触。
void RxView::ProtoFollow(bool on) {
    if (protoFollow_ == on) return;
    protoFollow_ = on;
    if (on) { firstLine_ = FollowFirst(); UpdateScrollbar(); }
    MarkDirty();
}

void RxView::TermFollowTouch() {
    if (!termOn_) return;
    termFollow_ = true;
    // §126：键入即跳底（无条件）——「终端输入时自动滚屏一直有效」。注意 SB_TOP/ScrollTo
    // 不取消 follow_（只有滚轮/拖选/拖条会），故不能只依赖 follow_ 判定；
    // 跳底目标含尾巴占位行（FollowFirst），否则尾巴/光标被客户区底边裁半（用户实测）；
    // 2s 静默或失焦后清除临时跟随（停留当前位置；不读/不改 follow_）。
    firstLine_ = FollowFirst();
    UpdateScrollbar();
    MarkDirty();
    SetTimer(hwnd_, kTimerTermFollow, 2000, nullptr);
}

bool RxView::TermInputChar(wchar_t c) {
    if (!termOn_) return false;
    if (c == L'\r' || c == L'\n' || c == L'\x08') return false;   // 特殊键在 WM_KEYDOWN 路径
    if (c < 0x20) return false;                                   // Tab 等见 WM_KEYDOWN 口径
    TermFollowTouch();                                            // §126：输入期临时跟随
    if (termKeyMode_) {
        // 代理对合并：高代理暂存，凑齐一对（或遇独立字符）才发送，避免半字编码
        if (termPendSur_) {
            wchar_t pair[2] = { termPendSur_, c };
            termPendSur_ = 0;
            if (c >= 0xDC00 && c <= 0xDFFF) { TermKeySend(pair, 2, 0); return true; }
            TermKeySend(pair, 1, 0);                 // 暂存位是孤儿：先发掉
        }
        if (c >= 0xD800 && c <= 0xDBFF) { termPendSur_ = c; return true; }
        TermKeySend(&c, 1, 0);
        return true;
    }
    termBuf_.push_back(c);                           // 回车模式：本地行编辑（带回显）
    MarkDirty();
    return true;
}

// §112：光标闪烁定时器（聚焦起、失焦停；周期取系统光标闪烁时间）。
void RxView::TermBlinkSet(bool on) {
    if (!hwnd_) return;
    if (on) {
        UINT bt = GetCaretBlinkTime();
        if (bt == (UINT)-1 || bt < 100 || bt > 2000) bt = 530;   // INFINITE/异常值 → 默认
        termBlinkOn_ = true;
        SetTimer(hwnd_, kTimerBlink, bt, nullptr);
    } else {
        KillTimer(hwnd_, kTimerBlink);
        termBlinkOn_ = true;                            // 相位归零：回焦即可见
    }
    MarkDirty();
}

// ---------------- 度量 ----------------
void RxView::EnsureMetrics(Line& l, int absKey) {
    std::wstring text = l.text();
    auto it = cwCache_.find(absKey);
    if (it != cwCache_.end() && it->second.size() == text.size() + 1) {
        l.cw = it->second;
        return;
    }
    HDC dc = GetDC(hwnd_);
    if (!dc) return;
    // §68：前缀（ts+mark）按行方向用收/发 tsFont_ 测宽（粗/斜/下划线变体宽度不同），正文用 font_
    const int head = (int)(l.ts.size() + l.mark.size());
    HFONT tsF = l.isTx ? tsTxFont_ : tsRxFont_;
    HFONT cur = font_;
    if (cur) SelectObject(dc, cur);
    std::vector<int> cw;
    cw.assign(text.size() + 1, 0);
    int acc = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        HFONT want = ((int)i < head && tsF) ? tsF : font_;
        if (want != cur) { if (want) SelectObject(dc, want); cur = want; }
        std::map<wchar_t, int>& cache = ((int)i < head) ? (l.isTx ? tsTxCharW_ : tsRxCharW_) : charW_;
        if (i + 1 < text.size() && text[i] >= 0xD800 && text[i] <= 0xDBFF &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            // 代理对合并计宽（C3：低代理格宽 0，选区/光标不再漂移）
            SIZE sz{};
            GetTextExtentPoint32W(dc, &text[i], 2, &sz);
            acc += sz.cx;
            cw[i + 1] = acc;                   // 低代理边界：宽 0（不可切分）
            ++i;
            cw[i + 1] = acc;
            continue;
        }
        auto cit = cache.find(text[i]);
        int w;
        if (cit != cache.end()) w = cit->second;
        else {
            SIZE sz{};
            GetTextExtentPoint32W(dc, &text[i], 1, &sz);
            w = sz.cx;
            cache[text[i]] = w;
        }
        acc += w;
        cw[i + 1] = acc;
    }
    ReleaseDC(hwnd_, dc);
    cwCache_[absKey] = cw;                     // 按绝对行号驻留（TrimWidthTables 淘汰长行）
    l.cw = std::move(cw);
}

// 长行宽表仅可见窗口驻留（C3）：paint 时对本窗口与上窗口的对称差里的长行清宽表。
// 普通短行宽表常驻（几十字节/行，50 万行可控）；长行单行可达 400KB。
void RxView::TrimWidthTables(int first, int last) {
    if (prevFirst_ < 0) { prevFirst_ = first; prevLast_ = last; return; }   // 首帧不清理
    auto clearRange = [&](int a, int b) {
        for (int i = a; i <= b; ++i) {
            auto it = cwCache_.find(base_ + i);
            if (it != cwCache_.end() && (int)it->second.size() - 1 > kLongLine)
                cwCache_.erase(it);
        }
    };
    if (first > prevLast_ + 1 || last < prevFirst_ - 1) {       // 完全跳走：清整个旧窗口
        clearRange(prevFirst_, prevLast_);
    } else {
        if (first > prevFirst_) clearRange(prevFirst_, first - 1);
        if (last < prevLast_) clearRange(last + 1, prevLast_);
    }
    prevFirst_ = first;
    prevLast_ = last;
}

int RxView::VisibleRows() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    int h = rc.bottom;
    return h > 0 ? h / lineH_ : 1;
}

// ---------------- 折行（§71 两种模式同一网格：前缀补格 + 3 格整数倍预算） ----------------
// 切分与绘制同源：一律基于 EnsureMetrics 的整串累宽 cw（GetTextExtent 行首起测），
// 格号 = ceil(cw[i]/cellW_)——比例字体字距不会累积漂移；单空间字体下格号=字符序号。
//
// 模型（§65 四规则 + §71 统一）：
//  ① cellW_=单 ASCII 字符格宽，容量 cap=客户区宽可容纳格数；
//  ② 首行前缀（时间戳+分包标记）占格向上补齐到 3 的倍数（H，可视补格），
//     **HEX 开/关都补**——两种模式数据前距离一致、上下行同处 3 格网格；
//  ③ 首行数据预算 = (cap−H) 向下取整到 3 的倍数，续行预算 = cap 向下取整到 3 的倍数；
//  ④ HEX 数据 = 3 字符单元严格切分（单元不劈开）；文本数据 = 按格贪心装入预算
//     （ASCII 1 格，宽字符 ceil(Δcw/cellW_)），行宽同样是 3 格的整数倍。
// HEX 首行前缀占格 H（前缀格数向上补齐到 3 的倍数；无前缀=0）。走段/绘制/命中共用。
static int HexHeadCells(const std::vector<int>& cw, int cellW, int head) {
    if (head <= 0 || cellW <= 0 || (int)cw.size() <= head) return 0;
    int hc = (cw[head] + cellW - 1) / cellW;
    return (hc + 2) / 3 * 3;
}

void RxView::WalkWrapRows(const Line& l, int cap,
                          const std::function<bool(int, int)>& fn) const {
    const std::vector<int>& cw = l.cw;
    int n = (int)cw.size() - 1;
    if (n <= 0) { fn(0, n); return; }
    if (cap < 6) cap = 6;
    int head = HeadOf(l);
    if (head > n) head = n;
    int H = HexHeadCells(cw, cellW_, head);
    int budget1 = ((cap - H) / 3) * 3;             // 首行数据预算（3 格整数倍）
    if (budget1 < 0) budget1 = 0;
    int budget = (cap / 3) * 3;                    // 续行数据预算
    if (budget < 3) budget = 3;
    if (hexUnits_) {
        // HEX：数据全 ASCII（1 格/字符），切点在 3 字符单元边界（预算格数=预算字符数）
        int i = head + budget1;
        if (i >= n) { fn(0, n); return; }
        // 防御：行内混入宽字符（切 HEX 显示前收到的文本行）→ 按实际占格收缩单元数；
        // 纯 ASCII（hex 常态）格号=字符序号，此循环不触发
        while (i > head && CellX(cw[i]) - CellX(cw[0]) > cap) i -= 3;
        if (!fn(0, i)) return;
        while (i < n) {
            int e = i + budget;
            if (e > n) e = n;
            while (e > i + 3 && CellX(cw[e]) - CellX(cw[i]) > cap) e -= 3;
            if (e <= i) e = (i + 3 <= n) ? i + 3 : n;   // 极窄兜底：保证前进不 stall
            if (!fn(i, e)) return;
            i = e;
        }
        return;
    }
    // 文本模式（§71）：数据按格贪心装入 3 格整数倍预算；首行数据从 head 起算
    // （前缀+补格合计占 H 格），续行从格 0 起
    int segS = 0;
    int b = budget1;
    while (segS < n) {
        int dataStart = segS > head ? segS : head;
        int segE = dataStart < n ? dataStart + 1 : n;   // 至少 1 数据字符/行
        while (segE < n && CellX(cw[segE + 1] - cw[dataStart]) <= b) ++segE;
        if (!fn(segS, segE)) return;
        segS = segE;
        b = budget;
    }
}

int RxView::MeasureRows(const Line& l, int cap) {
    int rows = 0;
    WalkWrapRows(l, cap, [&](int, int) { ++rows; return true; });
    return rows > 0 ? rows : 1;
}

// 行首非折行前缀字符数（时间戳+标记）
int RxView::HeadOf(const Line& l) const {
    return (int)(l.ts.size() + l.mark.size());
}

// 全量重排：逐行重量显示行数重建前缀索引（一次 O(全部字符)；实测见 docs §62）
void RxView::ReflowAll() {
    if (!hwnd_) return;
    RECT rc{}; GetClientRect(hwnd_, &rc);
    wrapCap_ = rc.right > 50 ? rc.right : 50;
    UpdateCapCells();
    int n = countFn_ ? countFn_(countCtx_) : 0;
    pre_.assign(1, 0);
    pre_.reserve((size_t)n + 1);
    for (int i = 0; i < n; ++i) {
        Line l;
        int rows = 1;
        if (GetLine(i, l)) {
            EnsureMetrics(l, base_ + i);               // 折行切分与绘制同源（整串累宽 cw）
            rows = MeasureRows(l, capCells_);
        }
        pre_.push_back(pre_.back() + rows);
    }
    totalRows_ = (int)pre_.size() > 1 ? pre_.back() : 0;
    lastLenSeen_ = -1;
    if (FollowNow()) firstLine_ = FollowFirst();
    else if (firstLine_ > MaxFirst()) firstLine_ = MaxFirst();
    UpdateScrollbar();
    MarkDirty();
}

// 增量：新落行的显示行数入索引（ EnsureMetrics 顺带建宽表，短行常驻）
void RxView::AppendNewRows() {
    int n = countFn_ ? countFn_(countCtx_) : 0;
    if (n < 0) n = 0;
    // 索引与逻辑行数不同步（清窗残留/裁剪/首建等任何原因）→ 一律全量重建（幂等），
    // 不做截断式防御——残留前缀截断会把 totalRows 置成错误值（§64 实测 view 欠绘）。
    if ((int)pre_.size() != n + 1) {
        pre_.assign(1, 0);
        pre_.reserve((size_t)n + 1);
        for (int i = 0; i < n; ++i) {
            Line l;
            int rows = 1;
            if (GetLine(i, l)) {
                EnsureMetrics(l, base_ + i);
                rows = MeasureRows(l, capCells_);
            }
            pre_.push_back(pre_.back() + rows);
        }
        totalRows_ = pre_.back();
        lastLenSeen_ = -1;
    }
    // 活动行续写增量
    if (n <= 0 || (int)pre_.size() < 2) return;
    Line l;
    if (!GetLine(n - 1, l)) return;
    int len = (int)l.text().size();
    if (len == lastLenSeen_) return;
    EnsureMetrics(l, base_ + n - 1);
    int rows = MeasureRows(l, capCells_);
    int prevRows = pre_.back() - pre_[pre_.size() - 2];
    pre_.back() += rows - prevRows;
    totalRows_ = pre_.back();
    lastLenSeen_ = len;
}

// 活动行续写：仅当文本长度变化时重量末行（EnsureMetrics 宽表按长度键增量）
void RxView::UpdateLastLineRows() {
    int n = countFn_ ? countFn_(countCtx_) : 0;
    if (n <= 0 || (int)pre_.size() < 2) return;
    Line l;
    if (!GetLine(n - 1, l)) return;
    int len = (int)l.text().size();
    if (len == lastLenSeen_) return;
    EnsureMetrics(l, base_ + n - 1);
    int rows = MeasureRows(l, capCells_);
    int prevRows = pre_.back() - pre_[pre_.size() - 2];
    pre_.back() += rows - prevRows;
    totalRows_ = pre_.back();
    lastLenSeen_ = len;
}

// 容量（格）= 客户区宽 / 单 ASCII 字符宽；cellW_ 未量（SetFont 前来数据）时现场量
void RxView::UpdateCapCells() {
    if (cellW_ <= 0) {
        int w = 6;
        if (hwnd_) {
            HDC dc = GetDC(hwnd_);
            HFONT old = font_ ? (HFONT)SelectObject(dc, font_) : nullptr;
            SIZE sz{};
            GetTextExtentPoint32W(dc, L"X", 1, &sz);
            if (old) SelectObject(dc, old);
            ReleaseDC(hwnd_, dc);
            if (sz.cx > 2) w = sz.cx;
        }
        cellW_ = w;
    }
    capCells_ = wrapCap_ / cellW_;
    if (capCells_ < 6) capCells_ = 6;
}

// 二分：显示行 → 逻辑视图行（pre_[k] ≤ disp < pre_[k+1] ⇒ k）
int RxView::LineOfRow(int dispRow) const {
    size_t lo = 0, hi = pre_.empty() ? 0 : pre_.size() - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (pre_[mid] <= dispRow) lo = mid; else hi = mid - 1;
    }
    return (int)lo;
}

int RxView::MaxFirst() const {
    int maxF = TotalRows() - VisibleRows();
    return maxF > 0 ? maxF : 0;
}

void RxView::ScrollTo(int first) {
    int maxF = MaxFirst();
    if (first < 0) first = 0;
    if (first > maxF) first = maxF;
    if (first == firstLine_) return;
    firstLine_ = first;
    UpdateScrollbar();
    MarkDirty();
}

void RxView::ScrollBy(int delta) { ScrollTo(firstLine_ + delta); }

void RxView::UpdateScrollbar() {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    si.nMin = 0;
    si.nMax = TotalRows() - 1;
    si.nPage = VisibleRows() + 1;
    si.nPos = firstLine_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

// ---------------- 命中测试 / 选区 ----------------
RxView::Pos RxView::HitTest(int x, int y) const {
    Pos p{ 0, 0 };
    int total = totalRows_;
    int disp = y / lineH_ + firstLine_;
    if (disp < 0) { p.line = 0; p.ch = 0; return p; }
    if (disp >= total) {
        int last = total - 1;
        if (last < 0) { p.line = 0; p.ch = 0; return p; }
        disp = last;
        p.line = (size_t)LineOfRow(disp);
        return p;                                      // 底部以下：末行行首
    }
    int k = LineOfRow(disp);
    int rowIn = disp - pre_[(size_t)k];
    p.line = (size_t)k;
    Line l;
    if (!const_cast<RxView*>(this)->GetLine(k, l)) return p;
    const_cast<RxView*>(this)->EnsureMetrics(l, base_ + k);
    const std::vector<int>& cw = l.cw;
    const std::wstring text = l.text();
    int n = (int)cw.size() - 1;
    int head = HeadOf(l);
    if (head > n) head = n;
    // 走到该显示行对应的折行段 [segS, segE)（走段器与 MeasureRows 同一切分）
    int segS = 0, rowIdx = 0, segE = n;
    const_cast<RxView*>(this)->WalkWrapRows(l, capCells_, [&](int rs, int re) {
        if (rowIdx == rowIn) { segS = rs; segE = re; return false; }
        ++rowIdx;
        return true;
    });
    // 段内取最近字符边界（x 为段内相对量：段起绘制于 x=0）。
    // HEX 模式数据区按 3 格网格定位（首行前缀补格 H，§65），前缀区仍按 cw。
    int best = segS;
    if (hexUnits_) {
        int H = HexHeadCells(cw, cellW_, head);         // 首行前缀补格后占格
        int ds = (rowIn == 0 ? head : segS);           // 该行数据首字符
        int db = (rowIn == 0 ? H : 0) * cellW_;        // 该行数据起始 x
        if (rowIn == 0 && x <= cw[head] - cw[segS]) {
            for (int i = segS; i < head; ++i) {        // 前缀区：cw 最近边界
                int edge = cw[i] - cw[segS];
                int next = cw[i + 1] - cw[segS];
                if (x <= edge) { best = i; break; }
                if (x < next) { best = (x - edge <= next - x) ? i : i + 1; break; }
                best = i + 1;
            }
        } else {
            int kk = (x - db + cellW_ / 2) / cellW_;   // 数据区：最近格边界
            if (kk < 0) kk = 0;
            if (kk > segE - ds) kk = segE - ds;
            best = ds + kk;
        }
    } else {
        // 文本模式：首行数据带补格距离（§71/§72）——命中 x 超过前缀末即减去补格宽度
        int padPx = 0;
        if (rowIn == 0 && head > 0 && cellW_ > 0) {
            int hp2 = HexHeadCells(cw, cellW_, head);
            padPx = hp2 * cellW_ - cw[head];
            if (padPx < 0) padPx = 0;
        }
        if (padPx > 0 && x > cw[head] - cw[segS]) {
            int pfxEnd = cw[head] - cw[segS];
            int dx = x - padPx;
            x = dx > pfxEnd ? dx : pfxEnd;         // 补格空白区 → 前缀末边界
        }
        for (int i = segS; i < segE; ++i) {
            int edge = cw[i] - cw[segS];
            int next = cw[i + 1] - cw[segS];
            if (x <= edge) { best = i; break; }
            if (x < next) {
                best = (x - edge <= next - x) ? i : i + 1; // 取较近边界
                break;
            }
            best = i + 1;
        }
    }
    if (best > segE) best = segE;
    p.ch = (size_t)best;
    // 代理对不可切分：命中低代理位 → 回退到高代理位（C3）
    if (p.ch > 0 && p.ch < text.size() && text[p.ch] >= 0xDC00 && text[p.ch] <= 0xDFFF &&
        text[p.ch - 1] >= 0xD800 && text[p.ch - 1] <= 0xDBFF)
        --p.ch;
    return p;
}

static bool PosLessImpl(size_t la, size_t ca, size_t lb, size_t cb) {
    return la < lb || (la == lb && ca < cb);
}

bool RxView::PosLess(const Pos& a, const Pos& b) {
    return PosLessImpl(a.line, a.ch, b.line, b.ch);
}

std::wstring RxView::SelectedText() const {
    if (!hasSel_) return L"";
    Pos a = selA_, b = selB_;
    if (PosLess(b, a)) { Pos t = a; a = b; b = t; }
    std::wstring out;
    for (size_t li = a.line; li <= b.line; ++li) {
        Line l;
        if (!const_cast<RxView*>(this)->GetLine((int)li, l)) break;
        // 勾选"复制含时间戳和收发标记"时，从行首（列 0）起被选中的行带出注记；
        // 半途起选（列 > 0）不带（用户没选到行首）——PyCom 同款语义。
        // §69 修正：注记替代选区内的前缀字符（此前注记 + 选区内前缀字符 = 前缀
        // 复制两份，选时间戳复制会多出两段）；选区终点仍在前缀内 → 所选即前缀
        // 片段本身，原样复制不附加注记。
        const int head = HeadOf(l);
        bool withAnno = copy_with_anno && (li != a.line || a.ch == 0);
        std::wstring text = l.text();
        size_t c0 = (li == a.line) ? a.ch : 0;
        size_t c1 = (li == b.line) ? (b.ch < text.size() ? b.ch : text.size()) : text.size();
        if (c0 > text.size()) c0 = text.size();
        if (c1 < c0) c1 = c0;
        if (!out.empty()) out += L"\r\n";
        if (withAnno && c1 > (size_t)head) {
            size_t b0 = (c0 < (size_t)head) ? (size_t)head : c0;
            out += (l.ts + l.mark) + text.substr(b0, c1 - b0);
        } else {
            out += text.substr(c0, c1 - c0);
        }
    }
    return out;
}

void RxView::CopySelection() {
    std::wstring text = SelectedText();
    if (text.empty()) return;
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        // §124（审查二十轮 P3-4）：GlobalLock 可能失败——失败须 GlobalFree（数据尚未交系统，
        // 且不得把空块 SetClipboardData 出去）；同型补偿 mainwindow.cpp §99 已有。
        if (wchar_t* p = (wchar_t*)GlobalLock(h)) {
            memcpy(p, text.c_str(), text.size() * sizeof(wchar_t));
            p[text.size()] = 0;
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

// ---------------- 窗口过程 ----------------
LRESULT CALLBACK RxView::ProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    RxView* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (RxView*)cs->lpCreateParams;
        self->hwnd_ = h;                   // WM_CREATE 里 SetTimer 要用 hwnd，此刻必须已回填
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (RxView*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT RxView::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd_, kTimerMerge, 33, nullptr);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kTimerMerge);
        KillTimer(hwnd_, kTimerEdge);
        KillTimer(hwnd_, kTimerReflow);
        KillTimer(hwnd_, kTimerBlink);
        KillTimer(hwnd_, kTimerTermFollow);
        return 0;

    case WM_TIMER:
        if (wp == kTimerMerge && dirty_) {
            dirty_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (wp == kTimerBlink) {                // §112：光标闪烁相位翻转
            termBlinkOn_ = !termBlinkOn_;
            MarkDirty();
        } else if (wp == kTimerTermFollow) {           // §126：输入/收发静默 2s → 还原用户滚屏原态
            KillTimer(hwnd_, kTimerTermFollow);
            if (termFollow_) {
                termFollow_ = false;                   // 停留当前位置：若用户原态=不跟随，
                UpdateScrollbar();                     // 刚输入的内容在屏外则按原态自然滚出
                MarkDirty();
            }
        } else if (wp == kTimerReflow && reflowPend_) {
            reflowPend_ = false;
            KillTimer(hwnd_, kTimerReflow);
            RECT rc{}; GetClientRect(hwnd_, &rc);
            if (wrapCap_ != rc.right) ReflowAll();     // 宽度确实变了才重排
        } else if (wp == kTimerEdge && selecting_ && edgeDir_ != 0) {
            ScrollBy(edgeDir_);                            // 拖选边缘持续滚屏（C2）
            RECT rc{}; GetClientRect(hwnd_, &rc);
            selB_ = HitTest(0, edgeDir_ < 0 ? 0 : rc.bottom - 1);
            hasSel_ = !(selA_.line == selB_.line && selA_.ch == selB_.ch);
            MarkDirty();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;                                       // 双缓冲：擦除与绘制合一

    case WM_SIZE: {
        RECT rc{}; GetClientRect(hwnd_, &rc);
        // §62：宽度变化 → 防抖重排（拖动期间沿用旧宽，停 150ms 后一次性重排）
        if (rc.right != wrapCap_ && !reflowPend_) {
            reflowPend_ = true;
            SetTimer(hwnd_, kTimerReflow, 150, nullptr);
        }
        if (FollowNow()) firstLine_ = FollowFirst();
        UpdateScrollbar();
        MarkDirty();
        return 0;
    }

    case WM_SETFONT:
        SetFont((HFONT)wp);
        return 0;

    case WM_PAINT: {
        // 用 GetDC 全客户区 blit + ValidateRect（不用 BeginPaint）：
        // BeginPaint 只能画到无效区（rcPaint），拖选/滚动合并刷新时无效区可能只覆盖
        // 局部，屏幕残留中途状态（表现为选区带"漏选/空蓝块"）。mem 每次都是全量
        // 重建的，直接整幅上屏 + 手工校验，彻底杜绝屏幕与最后一次 mem 不一致。
        HDC dc = GetDC(hwnd_);
        RECT rc{}; GetClientRect(hwnd_, &rc);
        int cw_ = rc.right, chh = rc.bottom;
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cw_ > 0 ? cw_ : 1, chh > 0 ? chh : 1);
        HBITMAP obmp = (HBITMAP)SelectObject(mem, bmp);
        HFONT of = font_ ? (HFONT)SelectObject(mem, font_) : nullptr;
        HBRUSH bg = CreateSolidBrush(colBg_);
        RECT all = { 0, 0, cw_, chh };
        FillRect(mem, &all, bg);
        DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT);
        // 基线对齐：baseY 是基线，字形盒 [baseY-ascent, baseY+descent) 正好落在行带
        // [y, y+lineH) 内。默认 TA_TOP 会把字形盒放到 [baseY, baseY+height)——越出本行
        // 带 ~10px 落进下一行（无选区时只是整体下移看不出来；选区带暴露错位）
        SetTextAlign(mem, TA_BASELINE);

        Pos a = selA_, b = selB_;
        if (hasSel_ && PosLess(b, a)) { Pos t = a; a = b; b = t; }

        int total = totalRows_;
        int rows = VisibleRows() + 1;
        int lastDisp = firstLine_ + rows - 1;
        if (lastDisp > total - 1) lastDisp = total - 1;
        if (firstLine_ <= lastDisp && total > 0) {
            int k0 = LineOfRow(firstLine_);
            int k1 = LineOfRow(lastDisp);
            TrimWidthTables(k0, k1);           // C3：长行宽表仅可见窗口驻留（逻辑视图行窗）
            for (int k = k0; k <= k1; ++k) {
                Line l;
                if (!GetLine(k, l)) break;
                EnsureMetrics(l, base_ + k);
                std::wstring text = l.text();
                const std::vector<int>& cw = l.cw;
                int n = (int)cw.size() - 1;
                int rowBase = (k < (int)pre_.size()) ? pre_[(size_t)k] : 0;
                int startRow = (k == k0) ? (firstLine_ - rowBase) : 0;
                // 该行选区（逻辑行+字符口径，跨显示行自然生效）
                size_t selC0 = (hasSel_ && k >= (int)a.line && k <= (int)b.line)
                               ? ((size_t)k == a.line ? a.ch : 0) : (size_t)-1;
                size_t selC1 = (hasSel_ && k >= (int)a.line && k <= (int)b.line)
                               ? ((size_t)k == b.line ? b.ch : text.size()) : (size_t)-1;
                struct Seg { size_t s, e; COLORREF c; };
                Seg segs[3] = {
                    { 0,               l.ts.size(),               l.hasColor ? l.tsCol : colText_ },
                    { l.ts.size(),     l.ts.size() + l.mark.size(), l.hasColor ? l.markCol : colText_ },
                    { l.ts.size() + l.mark.size(), text.size(),   l.hasColor ? l.bodyCol : colText_ },
                };
                int head = HeadOf(l);
                if (head > n) head = n;
                // 走段器产出显示行（§65：hexUnits_ 时 3 格单元对齐），绘制落在可见带内的行
                int rowIdx = 0;
                WalkWrapRows(l, capCells_, [&](int rs, int re) {
                    int disp = rowBase + rowIdx;
                    if (disp > lastDisp) return false;
                    if (disp >= firstLine_) {
                        int y = (disp - firstLine_) * lineH_;
                        int baseY = y + textAscent_ + 1;
                        bool firstRow = (rowIdx == 0);
                        // §71：两种模式都补格——首行数据从补格网格位置 hp×cellW_ 起；
                        // padPx = 网格位置 − 前缀自然宽度（§72 修正：此前用取整格数
                        // hp−hc 计补格，ceil 误差致文本补格比 HEX 少 0~1 格）
                        int hp = firstRow ? HexHeadCells(cw, cellW_, head) : 0;
                        int padPx = 0;
                        if (firstRow && head > 0 && cellW_ > 0) {
                            padPx = hp * cellW_ - cw[head];
                            if (padPx < 0) padPx = 0;
                        }
                        HFONT tsF = l.isTx ? tsTxFont_ : tsRxFont_;   // §68 前缀字体按行方向
                        // 段内字符 x（与切分同源 §71）：HEX 数据区走 3 格网格
                        // （首行从补格 H 起、续行从格 0 起，各单元列上下对齐）；
                        // 文本首行数据在 cw 自然位置上加补格距离，其余按 cw 累宽像素
                        auto px = [&](size_t ii) -> int {
                            if (hexUnits_ && (int)ii >= head)
                                return (firstRow ? hp : 0) * cellW_ +
                                       ((int)ii - (firstRow ? head : rs)) * cellW_;
                            if (firstRow && (int)ii >= head)
                                return cw[ii] - cw[rs] + padPx;
                            return cw[ii] - cw[rs];
                        };
                        for (const Seg& sg : segs) {
                            size_t s0 = sg.s < (size_t)rs ? (size_t)rs : sg.s;
                            size_t e0 = sg.e > (size_t)re ? (size_t)re : sg.e;
                            if (s0 >= e0) continue;
                            SetTextColor(mem, sg.c);
                            // §68：前缀段（ts+mark）用收/发 tsFont_（同字体+样式变体），正文用 font_
                            if ((int)s0 < head && tsF) SelectObject(mem, tsF);
                            else SelectObject(mem, font_);
                            TextOutW(mem, px(s0), baseY, &text[s0], (int)(e0 - s0));
                        }
                        // 选中区间光栅合成（蓝底白字）：与折行前同款，x 相对段起
                        if (selC0 != (size_t)-1 && selC1 > selC0) {
                            size_t c1 = selC1 > (size_t)re ? (size_t)re : selC1;
                            size_t c0 = selC0 < (size_t)rs ? (size_t)rs : selC0;
                            if (c1 > c0) {
                                int sx = px(c0), sr = px(c1);
                                if (sr > sx) {
                                    int w = sr - sx, h = lineH_;
                                    HDC mdc = CreateCompatibleDC(mem);
                                    HBITMAP mbm = CreateCompatibleBitmap(mem, w, h);
                                    HGDIOBJ mob = SelectObject(mdc, mbm);
                                    BitBlt(mdc, 0, 0, w, h, mem, sx, y, NOTSRCCOPY);    // 黑字白底→白笔画黑底
                                    HBRUSH sb = CreateSolidBrush(colSel_);
                                    RECT hl = { sx, y, sr, y + lineH_ };
                                    FillRect(mem, &hl, sb);                             // 蓝底（FillRect 可靠）
                                    DeleteObject(sb);
                                    BitBlt(mem, sx, y, w, h, mdc, 0, 0, SRCPAINT);      // 蓝底 OR 白笔画
                                    SelectObject(mdc, mob);
                                    DeleteObject(mbm);
                                    DeleteDC(mdc);
                                }
                            }
                        }
                    }
                    ++rowIdx;
                    return true;
                });
            }
        }
        if (termOn_) DrawTermTail(mem, cw_);            // §110：终端输入尾巴+光标（画在末行末尾）
        if (of) SelectObject(mem, of);
        BitBlt(dc, 0, 0, cw_, chh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, obmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(hwnd_, dc);
        ValidateRect(hwnd_, nullptr);
        return 0;
    }

    case WM_GETDLGCODE:
        // §110：按键模式要吃掉全部键（方向键=ANSI 序列、回车/退格=终端语义）；
        // 回车模式仍只要方向键（保留滚屏），文本键走 WM_CHAR 天然可收。
        return (termOn_ && termKeyMode_) ? DLGC_WANTALLKEYS : DLGC_WANTARROWS;

    case WM_SETFOCUS:
        if (termOn_) TermBlinkSet(true);                // §110/§112：光标显隐 + 闪烁起停
        return 0;
    case WM_KILLFOCUS:
        if (termOn_) {
            TermBlinkSet(false);
            KillTimer(hwnd_, kTimerTermFollow);        // §126：失焦=输入结束 → 立即还原原态
            if (termFollow_) { termFollow_ = false; UpdateScrollbar(); MarkDirty(); }
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        SetCapture(hwnd_);
        selecting_ = true;
        hasSel_ = false;
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            selA_ = HitTest(pt.x, pt.y);
            selB_ = selA_;
        }
        MarkDirty();
        return 0;

    case WM_MOUSEMOVE:
        if (selecting_) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc{}; GetClientRect(hwnd_, &rc);
            // 边缘自动滚屏：定时器驱动（C2，← PyCom）；回到客户区内即停
            if (pt.y < 0 || pt.y >= rc.bottom) {
                int dir = pt.y < 0 ? -1 : 1;
                if (edgeDir_ != dir) {
                    edgeDir_ = dir;
                    SetTimer(hwnd_, kTimerEdge, 50, nullptr);
                }
            } else if (edgeDir_ != 0) {
                edgeDir_ = 0;
                KillTimer(hwnd_, kTimerEdge);
            }
            selB_ = HitTest(pt.x, pt.y < 0 ? 0 : (pt.y > rc.bottom - 1 ? rc.bottom - 1 : pt.y));
            bool formed = !(selA_.line == selB_.line && selA_.ch == selB_.ch);
            if (formed && !hasSel_) SetFollow(false);              // 拖拽形成选区 → 取消自动滚屏（PyCom）
            hasSel_ = formed;
            MarkDirty();
        }
        return 0;

    case WM_LBUTTONUP:
        if (selecting_) {
            ReleaseCapture();
            selecting_ = false;
            if (edgeDir_ != 0) { edgeDir_ = 0; KillTimer(hwnd_, kTimerEdge); }
            if (follow_) { ScrollTo(MaxFirst()); }                 // 拖选到底部则继续跟随
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == 0 || lines > 100) lines = 3;
        int step = (int)lines;
        // 滚轮上翻即取消自动滚屏（PyCom 语义：内容不满一屏时同样取消勾选）
        if (delta > 0) SetFollow(false);
        ScrollBy(delta > 0 ? -step : step);
        return 0;
    }

    case WM_VSCROLL: {
        int code = LOWORD(wp);
        switch (code) {
        case SB_LINEUP:         ScrollBy(-1); break;
        case SB_LINEDOWN:       ScrollBy(1); break;
        case SB_PAGEUP:         ScrollBy(-(VisibleRows() - 1)); break;
        case SB_PAGEDOWN:       ScrollBy(VisibleRows() - 1); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            if ((int)si.nTrackPos < MaxFirst()) SetFollow(false);  // 拖条松手不在底部 → 取消（PyCom）
            ScrollTo(si.nTrackPos);
            break;
        }
        case SB_TOP: ScrollTo(0); break;
        case SB_BOTTOM: ScrollTo(MaxFirst()); break;
        }
        return 0;
    }

    case WM_KEYDOWN:
        // §110：终端模式下的特殊键（回车/退格/方向键）。派生 WM_CHAR 在 WM_CHAR 分支吞掉，
        // 保证每个键只产生一次发送。
        if (termOn_) {
            if (wp == VK_BACK) {
                TermFollowTouch();                     // §126：退格也是输入
                if (termKeyMode_) TermKeySend(nullptr, 0, 2);       // 按键模式：发 0x08
                else if (!termBuf_.empty()) {
                    // §111 P3-5：代理对整体退格（pop 一个等于删半个字，发送时编码器会出替换符）
                    if (termBuf_.size() >= 2 &&
                        termBuf_[termBuf_.size() - 1] >= 0xDC00 && termBuf_[termBuf_.size() - 1] <= 0xDFFF &&
                        termBuf_[termBuf_.size() - 2] >= 0xD800 && termBuf_[termBuf_.size() - 2] <= 0xDBFF)
                        termBuf_.resize(termBuf_.size() - 2);
                    else
                        termBuf_.pop_back();
                    MarkDirty();
                }
                return 0;
            }
            if (wp == VK_TAB && termKeyMode_) { TermKeySend(L"\t", 1, 0); return 0; }
            // 回车方式吞 Tab（本地行不能显示制表符，见 docs/03 §D）；WM_CHAR 侧 c<0x20 兜底
            if (wp == VK_ESCAPE) {
                // §115 P3-4：ESC 口径定版——按键方式发 0x1B（真实终端语义）；回车方式清空本地行
                // （行编辑器语义，ESC 不做「取消输入」以外的动作）。HOME/END/PgUp/PgDn 仍留给
                // 视图滚动（不上链路，docs/03 §D 已列明）。
                if (termKeyMode_) TermKeySend(L"\x1b", 1, 0);
                else if (!termBuf_.empty()) {
                    termBuf_.clear();
                    termPendSur_ = 0;
                    MarkDirty();
                }
                return 0;
            }
            if (wp == VK_RETURN) {
                TermFollowTouch();                     // §126：回车也是输入（含按键模式 CR）
                TermKeySend(termBuf_.c_str(), (int)termBuf_.size(), 1);
                // §111 P2-1：行已消费——不清空会导致连按回车重复发上一行、续打字符粘接上一行
                termBuf_.clear();
                termPendSur_ = 0;
                MarkDirty();
                return 0;
            }
            if (termKeyMode_) {
                const wchar_t* esc = nullptr;
                switch (wp) {
                case VK_UP:    esc = L"\x1b[A"; break;
                case VK_DOWN:  esc = L"\x1b[B"; break;
                case VK_RIGHT: esc = L"\x1b[C"; break;
                case VK_LEFT:  esc = L"\x1b[D"; break;
                }
                if (esc) { TermFollowTouch(); TermKeySend(esc, 3, 0); return 0; }
            }
        }
        switch (wp) {
        case VK_UP:    ScrollBy(-1); break;
        case VK_DOWN:  ScrollBy(1); break;
        case VK_PRIOR: ScrollBy(-(VisibleRows() - 1)); break;
        case VK_NEXT:  ScrollBy(VisibleRows() - 1); break;
        case VK_HOME:  ScrollTo(0); break;
        case VK_END:   ScrollTo(MaxFirst()); break;
        case VK_INSERT:
            if (GetKeyState(VK_CONTROL) < 0) CopySelection();
            break;
        }
        return 0;

    case WM_CHAR: {
        wchar_t c = (wchar_t)wp;
        if (c == 0x03) { CopySelection(); return 0; }    // Ctrl+C
        TermInputChar(c);                               // §112：终端开→入终端通道；关/特殊键→忽略
        return 0;
    }

    // ---------------- §112 IME 中文输入（M6 第二增量；组合窗/候选窗跟随光标） ----------------
    case WM_IME_STARTCOMPOSITION:
        TermImeSyncPos();                               // 组合窗开在尾巴末尾
        return 0;

    case WM_IME_COMPOSITION: {
        // 提交串自行插入并吞掉消息：返回 0 后 DefWindowProc 不再把结果逐字转成 WM_CHAR（防双插入）
        if (lp & GCS_RESULTSTR) {
            HIMC himc = ImmGetContext(hwnd_);
            if (himc) {
                LONG cb = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                if (cb > 0) {
                    std::wstring s((size_t)(cb / (LONG)sizeof(wchar_t)), L'\0');
                    LONG got = ImmGetCompositionStringW(himc, GCS_RESULTSTR, &s[0], cb);
                    if (got > 0) {
                        s.resize((size_t)(got / (LONG)sizeof(wchar_t)));
                        for (wchar_t ch : s) TermInputChar(ch);
                    }
                }
                ImmReleaseContext(hwnd_, himc);
            }
        }
        if (lp & GCS_COMPSTR) TermImeSyncPos();         // 组合中：跟随光标（候选窗随之）
        return 0;
    }

    case WM_IME_ENDCOMPOSITION:
        return 0;

    case WM_IME_CHAR:
        // 部分 IME/输入法以 WM_IME_CHAR 投递已提交字符（Unicode 窗口下 wParam=完整字符）。
        // 入同一通道并吞掉，避免 DefWindowProc 再转 WM_CHAR 双插入；终端关闭时交回默认处理。
        if (TermInputChar((wchar_t)wp)) return 0;
        break;

    case WM_IME_NOTIFY:
        if (wp == IMN_OPENCANDIDATE || wp == IMN_CHANGECANDIDATE) TermImeSyncPos();
        break;

    // ---------------- 右键菜单（对齐 PyCom：复制/全选 + 复制/保存含注记两开关 + 清除窗口） ----------------
    case WM_CONTEXTMENU: {
        enum { IDM_RCOPY = 100, IDM_RCOPYANNO, IDM_RSAVEANNO, IDM_RCLEAR, IDM_RSELALL };
        HMENU pm = CreatePopupMenu();
        AppendMenuW(pm, MF_STRING | (hasSel_ ? 0 : MF_GRAYED), IDM_RCOPY, L"复制(&C)");
        AppendMenuW(pm, MF_STRING, IDM_RSELALL, L"全选(&A)");          // §73：全选接收数据
        AppendMenuW(pm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(pm, MF_STRING | (copy_with_anno ? MF_CHECKED : 0),
                    IDM_RCOPYANNO, L"复制含时间戳和收发标记(&T)");
        AppendMenuW(pm, MF_STRING | (save_with_anno ? MF_CHECKED : 0),
                    IDM_RSAVEANNO, L"保存含时间戳和收发标记(&S)");
        AppendMenuW(pm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(pm, MF_STRING, IDM_RCLEAR, L"清除窗口(&L)");
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int cmd = TrackPopupMenu(pm, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                 x, y, 0, hwnd_, nullptr);
        DestroyMenu(pm);
        switch (cmd) {
        case IDM_RCOPY:     CopySelection(); break;
        case IDM_RSELALL: {                            // §73：全选（viewIdx 坐标整段）
            int n = countFn_ ? countFn_(countCtx_) : 0;
            if (n > 0) {
                Line l;
                size_t lastLen = 0;
                if (GetLine(n - 1, l)) lastLen = l.text().size();
                selA_ = Pos{ 0, 0 };
                selB_ = Pos{ (size_t)(n - 1), lastLen };
                hasSel_ = !(n == 1 && lastLen == 0);   // 空内容不置选
                MarkDirty();
            }
            break;
        }
        case IDM_RCOPYANNO:
            copy_with_anno = !copy_with_anno;
            if (annoFn_) annoFn_(menuCtx_, copy_with_anno, save_with_anno);
            break;
        case IDM_RSAVEANNO:
            save_with_anno = !save_with_anno;
            if (annoFn_) annoFn_(menuCtx_, copy_with_anno, save_with_anno);
            break;
        case IDM_RCLEAR:
            if (clearFn_) clearFn_(menuCtx_);
            break;
        }
        return 0;
    }

    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace sncom
