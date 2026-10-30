// sncom/src/core/display.cpp —— LineBuilder（← display.py 逐语义移植）
#include "display.h"
#include <windows.h>
#include <algorithm>

namespace sncom {

std::wstring TsPrefix(double ts) {
    SYSTEMTIME st{};
    FILETIME ft{}, ftLocal{};
    ULARGE_INTEGER u{};
    u.QuadPart = (ULONGLONG)(ts * 10000000.0 + 116444736000000000.0);
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
    int ms = (int)((ts - (double)(int64_t)ts) * 1000);
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    wchar_t buf[24];
    swprintf(buf, 24, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, ms);
    return buf;
}

const wchar_t* DirMarker(bool is_tx) { return is_tx ? L"发→◇" : L"收←◆"; }

double NowSeconds() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (double)u.QuadPart / 10000000.0;
}

LineBuilder::LineBuilder(const std::wstring& enc, bool hex_mode, bool split_, int split_ms,
                         size_t max_lines)
    : max_lines_(max_lines < 1000 ? 1000 : max_lines), encoding_(enc), hex_mode_(hex_mode),
      split_ms_(split_ms), dec_rx_(enc), dec_tx_(enc) {}

void LineBuilder::set_encoding(const std::wstring& enc) {
    if (enc == encoding_) return;
    // 旧编码缓存的半截多字节序列先落成替换字符（Python final=True errors=replace 的近似：逐字节 U+FFFD）
    std::wstring tailRx = dec_rx_.FlushFinal();
    std::wstring tailTx = dec_tx_.FlushFinal();
    encoding_ = enc;
    dec_rx_ = IncrementalDecoder(enc);
    dec_tx_ = IncrementalDecoder(enc);
    if (!tailRx.empty())
        EmitText(pend_tx_, pend_ts_, L"", tailRx, pend_abs_);
    if (!tailTx.empty())
        EmitText(true, pend_ts_, L"", tailTx, pend_abs_);
}

void LineBuilder::set_hex_mode(bool on) {
    hex_mode_ = on;
    CloseOpen(false);
    flush();
}

void LineBuilder::set_split(bool on, int ms) {
    split = on;
    if (ms > 0) split_ms_ = ms;
}

void LineBuilder::clear() {
    lines_.clear();
    base_ = 0;
    packet_stamped_ = false;
    cr_hold_ = false;
    dec_rx_ = IncrementalDecoder(encoding_);
    dec_tx_ = IncrementalDecoder(encoding_);
    has_pend_ = false;
    pend_buf_.clear();
    pend_ts_ = 0;
    last_feed_ts_ = -1;
    has_open_ = false;
    log_keep_ = -1;
}

int LineBuilder::line_count() const {
    int n = base_ + (int)lines_.size();
    if (has_open_ && (!open_.text.empty() || !open_.prefix.empty())) ++n;
    return n;
}

const DispLine* LineBuilder::line_at(int abs) const {
    int j = abs - base_;
    if (j >= 0 && j < (int)lines_.size()) return &lines_[j];
    if (j == (int)lines_.size() && has_open_ &&
        (!open_.text.empty() || !open_.prefix.empty())) return &open_;
    return nullptr;
}

std::wstring LineBuilder::open_line_text() const {
    if (!has_open_ || (open_.text.empty() && open_.prefix.empty())) return L"";
    const std::wstring& prefix = split ? open_.prefix : L"";
    return prefix + ((split && !prefix.empty()) ? DirMarker(open_.is_tx) : L"") + open_.text;
}

std::wstring LineBuilder::open_line_delta() const {
    if (!has_open_) return L"";
    int written = open_.written;
    if ((int)open_.text.size() <= written) return L"";
    std::wstring prefix = (written == 0 && split) ? open_.prefix : L"";
    std::wstring marker = (written == 0 && !prefix.empty()) ? DirMarker(open_.is_tx) : L"";
    return prefix + marker + open_.text.substr((size_t)written);
}

void LineBuilder::commit_open(bool nl_written) {
    if (has_open_) {
        open_.written = (int)open_.text.size();
        if (nl_written) open_.nl_done = true;
    }
}

void LineBuilder::feed(bool is_tx, const uint8_t* d, size_t n, double ts, uint64_t abs_start) {
    if (!n) return;
    if (has_pend_ && is_tx != pend_tx_) flush();
    if (!has_pend_) {
        has_pend_ = true;
        pend_tx_ = is_tx;
        pend_ts_ = ts;
        pend_abs_ = abs_start;
        packet_stamped_ = false;
    }
    pend_buf_.insert(pend_buf_.end(), d, d + n);
    last_feed_ts_ = ts;
    if (pend_buf_.size() >= kPendMax) ConsumePending();   // pending 上限保护
}

bool LineBuilder::maybe_flush(double now) {
    int n0 = line_count();
    bool idle = last_feed_ts_ >= 0 && (now - last_feed_ts_) >= split_ms_ / 1000.0;
    if (!pend_buf_.empty()) {
        if (idle || pend_buf_.size() >= kPendMax) flush();
        else ConsumePending();                            // 持续流：增量续写当前行
    } else if (idle && (has_pend_ || cr_hold_)) {
        flush();                                           // 持续流消费完后空闲仍要结算包状态
    }
    return line_count() != n0;
}

void LineBuilder::flush() {
    ConsumePending();
    if (cr_hold_) {                                        // 包结束：挂起 \r 落为换行
        cr_hold_ = false;
        std::wstring prefix;
        if (has_pend_ && !packet_stamped_) {
            prefix = TsPrefix(pend_ts_);
            packet_stamped_ = true;
        }
        EmitText(pend_tx_, pend_ts_, prefix, L"\n", pend_abs_);
    }
    // §73：HEX 连续流的分包收行——空闲分包超时/方向切换的"包结束"必须关活动行，
    // 下一包重新开行并打新时间戳（此前活动行跨包延续，第二包起不再显示时间戳）。
    // 持续流（间隔 < split_ms）走 maybe_flush 的增量 ConsumePending 不受影响。
    if (hex_mode_ && has_open_) CloseOpen(false);
    has_pend_ = false;
    pend_ts_ = 0;
}

void LineBuilder::ConsumePending() {
    if (pend_buf_.empty()) return;
    bool is_tx = pend_tx_;
    bool stamp = has_pend_ && !packet_stamped_;
    std::wstring prefix = stamp ? TsPrefix(pend_ts_) : L"";
    std::vector<uint8_t> data;
    data.swap(pend_buf_);
    uint64_t abs0 = pend_abs_;
    pend_abs_ += data.size();

    if (hex_mode_) {
        // §63：连续 hex 流（SSCOM 对齐，用户拍板废除 16 字节/行拆分）——hex 文本持续
        // 写入活动行（包首行带时间戳前缀），包结束（空闲分包 flush）时整包收行；
        // 单行长度上限与文本模式同款（超限强制收行、重开无前缀新行，切分对齐 3 字符
        // 保证字节三元组不劈开）；超宽显示由 rxview 折行负责（§62）。方向交替立即切行。
        static const wchar_t* hx = L"0123456789ABCDEF";
        if (has_open_ && open_.is_tx != is_tx) CloseOpen(false);
        if (!has_open_) {
            has_open_ = true;
            open_ = DispLine();
            open_.is_tx = is_tx;
            open_.ts = pend_ts_;
            open_.prefix = stamp ? prefix : L"";
            open_.start_abs = abs0;
            open_.nbytes = 0;
            if (stamp) packet_stamped_ = true;             // 开行即消耗戳记（存续期间不再打戳）
        }
        std::wstring add;
        add.reserve(data.size() * 3 + 1);
        if (!open_.text.empty()) add.push_back(L' ');  // 续写接缝补分隔空格（§65：块间 join
                                                       // 曾丢空格 → 单元并字、整行相位错 1 格）
        for (size_t j = 0; j < data.size(); ++j) {
            if (j) add.push_back(L' ');
            add.push_back(hx[data[j] >> 4]);
            add.push_back(hx[data[j] & 0xF]);
        }
        size_t pos = 0;
        while (pos < add.size()) {
            size_t room = open_.text.size() < (size_t)open_max_chars_
                              ? (size_t)open_max_chars_ - open_.text.size() : 0;
            room -= room % 3;                              // 切分对齐字节三元组（"XX " 3 字符）
            if (room == 0) {                               // 满行：强制收行、重开无前缀新行
                uint64_t contAbs = open_.start_abs + (uint64_t)open_.nbytes;
                CloseOpen(true);
                has_open_ = true;
                open_ = DispLine();
                open_.is_tx = is_tx;
                open_.ts = pend_ts_;
                open_.start_abs = contAbs;
                room = (size_t)open_max_chars_ - (size_t)open_max_chars_ % 3;
            }
            size_t take = add.size() - pos < room ? add.size() - pos : room;
            open_.text += add.substr(pos, take);
            size_t bytesPut = (take + 2) / 3;              // 向上取整：末段 "…XX" 无尾空格
            open_.nbytes += (int)bytesPut;
            pos += take;
            if (pos < add.size()) {                        // 行满切分：新行起始 = 旧行末字节
                uint64_t contAbs = open_.start_abs + (uint64_t)open_.nbytes;
                CloseOpen(true);
                has_open_ = true;
                open_ = DispLine();
                open_.is_tx = is_tx;
                open_.ts = pend_ts_;
                open_.start_abs = contAbs;
            }
        }
        TrimLines();
    } else {
        std::wstring decoded = (is_tx ? dec_tx_ : dec_rx_).Feed(data.data(), data.size());
        std::wstring raw = cr_hold_ ? (L"\r" + decoded) : decoded;
        cr_hold_ = !raw.empty() && raw.back() == L'\r';
        if (cr_hold_) raw.pop_back();
        // CR-only 行尾兼容：单 \r 也视为换行
        std::wstring text;
        text.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == L'\r') {
                if (i + 1 < raw.size() && raw[i + 1] == L'\n') continue;   // \r\n → \n
                text.push_back(L'\n');                                       // 单 \r → \n
            } else {
                text.push_back(raw[i]);
            }
        }
        if (!text.empty()) {
            EmitText(is_tx, pend_ts_, prefix, text, abs0);
            if (stamp) packet_stamped_ = true;              // 只有真正产出文本才消耗戳记
        }
        TrimLines();
    }
}

void LineBuilder::EmitText(bool is_tx, double ts, const std::wstring& prefix,
                           const std::wstring& text0, uint64_t abs0) {
    if (text0.empty()) return;
    std::wstring text = text0;
    // NUL 防护：0x00 经单字节编码解成 U+0000，绘制层不接受（PyCom 同款防御）
    size_t w = 0;
    for (wchar_t c : text) if (c != 0) text[w++] = c;
    text.resize(w);

    if (has_open_ && open_.is_tx != is_tx) CloseOpen(false);

    // 按 \n 分段：以 \n 收尾的段为完整行（真实空行保留）；尾段留 open 续接
    size_t segStart = 0;
    bool first_piece = true;
    while (segStart <= text.size()) {
        size_t nl = text.find(L'\n', segStart);
        bool terminated = nl != std::wstring::npos;
        size_t segEnd = terminated ? nl : text.size();
        std::wstring seg = text.substr(segStart, segEnd - segStart);
        while (!seg.empty() && seg.back() == L'\r') seg.pop_back();   // rstrip \r（与 Python 一致）

        // 行上限硬切分：任何行都不超 open_max_chars_
        bool segDone = false;
        while (!segDone) {
            size_t open_len = has_open_ ? open_.text.size() : 0;
            size_t room = open_len < open_max_chars_ ? open_max_chars_ - open_len : 0;
            if (seg.size() <= room) {
                if (!has_open_) {
                    if (seg.empty() && !terminated) break;  // 行首且无内容：不产生空游标
                    has_open_ = true;
                    open_ = DispLine();
                    open_.is_tx = is_tx;
                    open_.ts = ts;
                    open_.prefix = first_piece ? prefix : L"";
                    open_.text = seg;
                    open_.start_abs = abs0;
                } else {
                    open_.text += seg;
                }
                segDone = true;
            } else {
                std::wstring head = seg.substr(0, room);
                seg = seg.substr(room);
                if (!head.empty()) {
                    if (has_open_) open_.text += head;
                    else {
                        has_open_ = true;
                        open_ = DispLine();
                        open_.is_tx = is_tx;
                        open_.ts = ts;
                        open_.prefix = first_piece ? prefix : L"";
                        open_.text = head;
                        open_.start_abs = abs0;
                        first_piece = false;
                    }
                    CloseOpen(true);                        // 满行强制成行
                } else {
                    CloseOpen(true);                        // room==0：结算重开
                }
            }
        }
        if (segDone && terminated) CloseOpen(true);
        if (!terminated) break;
        segStart = nl + 1;
        first_piece = false;
        if (segStart == text.size()) break;                 // 文本以 \n 结尾：不留空游标
    }
}

void LineBuilder::CloseOpen(bool keep_empty) {
    if (!has_open_) return;
    // 空活动行两种来源：真实空行（连续换行）保留；包尾换行留下的游标在方向/模式
    // 切换时是伪影，丢弃（keep_empty=false）
    if (keep_empty || !open_.text.empty() || !open_.prefix.empty()) {
        lines_.push_back(open_);
        TrimLines();
    }
    has_open_ = false;
}

// §126：终端回显整行专用通道——无时间戳前缀（prefix 空 → 天然无 [ts]/方向标记/分包语义）、
// 文本带 \n 经 EmitText 立即收行 → 光标/输入尾巴落在下一行（终端语义）。与 pending/open
// 完全隔离：不碰包状态机（先后 flush() 结算在途包，避免方向交错把终端行接进收流）。
// 字节记量：终端行不计 nbytes 口径（nbytes=0），行裁剪/转存游标按行号语义不受影响。
void LineBuilder::feed_terminal_line(bool is_tx, const std::wstring& text) {
    // §126：允许空 text——连按回车=发空行，回显也要落**空行**（§111 P2-1 行为：
    // payloads ['M','M','',''] 里的空行；吞掉会让转存/显示少空行——r110 D 实测回归）。
    flush();                                            // 在途包先结算（方向/包边界干净）
    has_pend_ = false;                                  // flush 后必为空，防御性归零
    // start_abs 取包流之外的自有记量（终端行不属任何包；与 trim_before_abs 的包分组隔离）
    EmitText(is_tx, 0, L"", text + L"\n", term_abs_);
    term_abs_ += text.size() + 1;                       // 记量递进（含合成 \n），仅用于行分组
    TrimLines();
}

void LineBuilder::TrimLines() {
    if (lines_.size() <= max_lines_) return;
    size_t cut = max_lines_ / 8;
    if (cut < 1) cut = 1;
    if (log_keep_ >= 0) {
        if (log_keep_ < base_) log_keep_ = -1;              // 残留游标落后基址：视为无游标
        else {
            size_t m = (size_t)(log_keep_ - base_);
            if (m < cut) cut = m;
            if (cut == 0) return;                            // 未写盘行不许裁：宁超上限
        }
    }
    lines_.erase(lines_.begin(), lines_.begin() + cut);
    base_ += (int)cut;
}

void LineBuilder::trim_before_abs(uint64_t abs_off, int keep_after_line) {
    size_t n = lines_.size();
    size_t i = 0;
    while (i < n) {
        // 包边界：同包所有行 start_abs 相同；用下一包起点表示本包结束
        uint64_t start = lines_[i].start_abs;
        size_t j = i;
        while (j + 1 < n && lines_[j + 1].start_abs == start) ++j;
        uint64_t end = (j + 1 < n) ? lines_[j + 1].start_abs : 0;
        bool last = (j + 1 >= n);
        if (last || end > abs_off) break;                    // 最后一包 / 跨界包：整包保留
        if (keep_after_line >= 0 && base_ + (int)j + 1 > keep_after_line) break;  // 有行没写日志
        i = j + 1;
    }
    if (i) {
        lines_.erase(lines_.begin(), lines_.begin() + i);
        base_ += (int)i;
    }
}

} // namespace sncom
