// sncom/src/core/display.h —— 显示行构建器（← PyCom display.py LineBuilder 忠实移植）
//
// 语义（与 PyCom 逐条对齐，docs/01 §5 行为规格的基准）：
// - 分包：空闲超 split_ms 切包；包首行带 "[HH:MM:SS.mmm] " 前缀（前缀随行记录，
//   是否显示/带出由 split 开关决定——显示、复制、保存各自独立选择）
// - 方向交替立即切包；ASCII 模式未收尾行跨包延续；持续流增量续写（pending 上限 1MB）
// - 真实连续换行保留空行；\r 挂起跨 chunk 合并 \r\n；单 \r 也视为换行
// - 行表上限：超限裁掉最旧 1/8 并推进基址（绝对行号 = base + 下标）；
//   转存游标（log_keep）与 trim_before_abs（跟随字节缓冲裁剪）均不得裁未写盘行
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "encoding.h"

namespace sncom {

// （kHexRow=16 常量已废除：§63 连续 hex 流对齐 SSCOM，不再按 16 字节/行拆分）
const size_t kPendMax = 1u << 20;       // pending 缓冲上限（字节）：持续流下超限立即落行

std::wstring TsPrefix(double ts);       // "[HH:MM:SS.mmm] "
const wchar_t* DirMarker(bool is_tx);   // 接收 "收←◆" / 发送 "发→◇"
double NowSeconds();                    // 墙钟秒（时间戳前缀用）

struct DispLine {
    bool is_tx = false;
    double ts = 0;                      // 包起始时间戳
    std::wstring prefix;                // "[HH:MM:SS.mmm] "（包首行才有）
    std::wstring text;                  // 可见文本（不含换行）
    uint64_t start_abs = 0;             // 本行首字节绝对偏移（§63 连续 hex 行=整包跨度）
    int nbytes = 0;                     // 本行对应字节数
    int written = 0;                    // 已转存到日志的字符数（0=整行未写）
    bool nl_done = false;               // 日志中是否已写行终止符
};

class LineBuilder {
public:
    LineBuilder(const std::wstring& enc, bool hex_mode, bool split, int split_ms,
                size_t max_lines = 500000);

    // ---- 配置 ----
    void set_encoding(const std::wstring& enc);   // 旧编码半截尾巴先落成替换字符
    void set_hex_mode(bool on);
    void set_split(bool on, int ms);              // 只决定前缀/标识是否带出，不改包边界
    void clear();

    // ---- 行表（绝对行号 = base + 下标）----
    int base() const { return base_; }
    size_t finished() const { return lines_.size(); }          // 已完结行数
    int line_count() const;                                    // 含未收尾行
    const DispLine* line_at(int abs) const;                    // 越界/裁剪 → nullptr
    DispLine* line_at(int abs) { return const_cast<DispLine*>(static_cast<const LineBuilder*>(this)->line_at(abs)); }
    const DispLine* open() const { return has_open_ ? &open_ : nullptr; }
    std::wstring open_line_text() const;                       // [ts]收←◆文本（split 决定带出）
    std::wstring open_line_delta() const;                      // 未转存尾部（首段含前缀标识）
    void commit_open(bool nl_written);                         // 日志写入成功后推进游标
    void set_log_keep(int abs) { log_keep_ = abs; }            // 转存游标（-1 = 无）

    // ---- 输入 ----
    void feed(bool is_tx, const uint8_t* d, size_t n, double ts, uint64_t abs_start);
    void feed_terminal_line(bool is_tx, const std::wstring& text);   // §126：终端回显整行（无 ts 前缀/
                                                                     // 无分包，\n 立即收行；不碰 pending/open）
    bool maybe_flush(double now);          // UI 定时调用：持续流续写 + 空闲结算
    void flush();                          // 结算当前包
    void trim_before_abs(uint64_t abs_off, int keep_after_line);  // 跟随字节缓冲裁剪

    bool split = true;                     // 前缀/标识是否带出（显示、日志共用此开关）

private:
    void ConsumePending();
    void EmitText(bool is_tx, double ts, const std::wstring& prefix,
                  const std::wstring& text, uint64_t abs0);
    void CloseOpen(bool keep_empty);
    void TrimLines();

    std::vector<DispLine> lines_;
    size_t max_lines_;
    int base_ = 0;
    std::wstring encoding_;
    bool hex_mode_ = false;
    int split_ms_;
    IncrementalDecoder dec_rx_, dec_tx_;
    bool pend_tx_ = false;                // 当前包方向（false=无包）
    bool has_pend_ = false;
    std::vector<uint8_t> pend_buf_;
    double pend_ts_ = 0;
    uint64_t pend_abs_ = 0;
    bool packet_stamped_ = false;
    uint64_t term_abs_ = 1;               // §126：终端回显行自有记量（从 1 起，避开 buf 偏移 0 的真实包）
    bool cr_hold_ = false;
    size_t open_max_chars_ = 100000;      // 单行长度上限：超限强制折行（可变以保拷贝赋值）
    double last_feed_ts_ = -1;
    bool has_open_ = false;
    DispLine open_;
    int log_keep_ = -1;
};

} // namespace sncom
