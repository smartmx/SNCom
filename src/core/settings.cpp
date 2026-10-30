// sncom/src/core/settings.cpp
#include "settings.h"
#include <windows.h>
#include <cmath>

namespace sncom {

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ReadFontFrom(const JsonPtr& o, const wchar_t* name, FontSpec& f) {
    if (!o) return;
    const JsonPtr* p = o->Get(name);
    if (!p || !*p || !(*p)->isObject()) return;
    const JsonPtr& fo = *p;
    f.family    = fo->S(L"family", f.family);
    f.size      = fo->I(L"size", f.size);
    f.bold      = fo->B(L"bold", f.bold);
    f.italic    = fo->B(L"italic", f.italic);
    f.underline = fo->B(L"underline", f.underline);
    if (f.size < 4)  f.size = 4;          // 与 PyCom 的字体归一化口径一致（越界收敛）
    if (f.size > 72) f.size = 72;
    if (f.family.empty()) f.family = L"宋体";
}

void Config::FromJson(const JsonPtr& root) {
    if (!root || !root->isObject()) return;

    win_w = root->I(L"win_w", win_w);
    win_h = root->I(L"win_h", win_h);
    if (win_w < 200) win_w = 200;
    if (win_h < 200) win_h = 200;

    mode = root->S(L"mode", mode);
    port = root->S(L"port", port);
    baud = root->I(L"baud", baud);
    databits = root->I(L"databits", databits);
    stopbits = root->D(L"stopbits", stopbits);
    if (!(stopbits == 1.0 || stopbits == 1.5 || stopbits == 2.0)) stopbits = 1.0;
    parity = root->S(L"parity", parity);
    flow   = root->S(L"flow", flow);
    dtr = root->B(L"dtr", dtr);
    rts = root->B(L"rts", rts);
    auto_reconnect = root->B(L"auto_reconnect", auto_reconnect);
    reconnect_ms = root->I(L"reconnect_ms", reconnect_ms);
    open_on_start = root->B(L"open_on_start", open_on_start);
    remote_ip = root->S(L"remote_ip", remote_ip);
    remote_port = root->I(L"remote_port", remote_port);
    local_ip = root->S(L"local_ip", local_ip);
    local_port = root->I(L"local_port", local_port);

    encoding = root->S(L"encoding", encoding);
    hex_mode = root->B(L"hex_mode", hex_mode);
    auto_scroll = root->B(L"auto_scroll", auto_scroll);
    split_show = root->B(L"split_show", split_show);
    split_ms = root->I(L"split_ms", split_ms);
    copy_with_anno = root->B(L"copy_with_anno", copy_with_anno);
    save_with_anno = root->B(L"save_with_anno", save_with_anno);
    // §68：收/发时间戳各自独立样式（§69 移除加粗）；旧 ts_* 单份键迁移作种（收发同色起步）
    rx_ts_color     = root->S(L"rx_ts_color", root->S(L"ts_color", rx_ts_color));
    rx_ts_italic    = root->B(L"rx_ts_italic", root->B(L"ts_italic", rx_ts_italic));
    rx_ts_underline = root->B(L"rx_ts_underline", root->B(L"ts_underline", rx_ts_underline));
    tx_ts_color     = root->S(L"tx_ts_color", root->S(L"ts_color", tx_ts_color));
    tx_ts_italic    = root->B(L"tx_ts_italic", root->B(L"ts_italic", tx_ts_italic));
    tx_ts_underline = root->B(L"tx_ts_underline", root->B(L"ts_underline", tx_ts_underline));
    buffer_cap = root->I(L"buffer_cap", buffer_cap);

    // 字体：优先新键 font；否则兼容 PyCom 的 recv_font（其次 send_font）
    ReadFontFrom(root, L"font", font);
    if (!root->Get(L"font")) {
        // 旧键合并：整体取 recv_font（接收侧优先，缺项回默认），无 recv_font 才用 send_font
        const JsonPtr* rp = root->Get(L"recv_font");
        if (rp && *rp && (*rp)->isObject()) {
            FontSpec f;
            ReadFontFrom(root, L"recv_font", f);
            font = f;
        } else {
            ReadFontFrom(root, L"send_font", font);
        }
    }
    bg_color   = root->S(L"bg_color", bg_color);
    text_color = root->S(L"text_color", root->S(L"fg_color", text_color));   // 兼容 fg_color

    send_hex = root->B(L"send_hex", send_hex);
    send_interval_ms = root->I(L"send_interval_ms", send_interval_ms);
    send_newline = root->B(L"send_newline", send_newline);
    checksum_kind = root->S(L"checksum_kind", checksum_kind);
    checksum_start = root->I(L"checksum_start", checksum_start);
    checksum_tail  = root->I(L"checksum_tail", checksum_tail);
    proto_timeout_ms = root->I(L"proto_timeout_ms", proto_timeout_ms);
    proto_retries = root->I(L"proto_retries", proto_retries);
    proto_kind   = root->I(L"proto_kind", proto_kind);           // §132：协议页 5 项界面态持久化
    proto_xcheck = root->I(L"proto_xcheck", proto_xcheck);
    proto_xblk   = root->I(L"proto_xblk", proto_xblk);
    proto_show   = root->B(L"proto_show", proto_show);
    proto_file   = root->S(L"proto_file", proto_file);
    send_timed   = root->B(L"send_timed", send_timed);           // §132：定时发送勾选态
    main_text = root->S(L"main_text", main_text);
    file_delay_ms = root->I(L"file_delay_ms", file_delay_ms);
    multisend_loop = root->B(L"multisend_loop", multisend_loop);
    term_enable     = root->B(L"term_enable", term_enable);     // §140
    term_enter_send = root->B(L"term_enter_send", term_enter_send);
    term_key_send   = root->B(L"term_key_send", term_key_send);
    term_key_echo   = root->B(L"term_key_echo", term_key_echo);
    term_lf_send    = root->B(L"term_lf_send", term_lf_send);   // §110

    // §99（审查 U2）：数值出口统一钳制——配置可被手工改坏（reconnect_ms=0 会令 WM_TIMER
    // 以 0ms 周期忙轮询、buffer_cap 超限失去内存上限）；UI 输入框另有各自的即时校验。
    // 钳制口径：时钟类 1ms~1h；缓冲与档位上下限一致（500K~100M）；窗口 200~16384。
    win_w          = ClampI(win_w, 200, 16384);
    win_h          = ClampI(win_h, 200, 16384);
    baud           = ClampI(baud, 300, 50000000);   // §100（审查 A）：与 §77/§79 自定义波特率区间
                                                    // （300..50M，UI 弹窗同区间）一致——§99 曾误收窄到
                                                    // 12M，用户合法的 20M/50M 设置重启被静默改回
    databits       = ClampI(databits, 5, 8);
    reconnect_ms   = ClampI(reconnect_ms, 100, 3600000);
    remote_port    = ClampI(remote_port, 0, 65535);
    local_port     = ClampI(local_port, 0, 65535);
    split_ms       = ClampI(split_ms, 1, 3600000);
    buffer_cap     = ClampI(buffer_cap, 500 * 1024, 100 * 1024 * 1024);
    send_interval_ms = ClampI(send_interval_ms, 1, 3600000);
    proto_timeout_ms = ClampI(proto_timeout_ms, 100, 60000);   // §131
    proto_retries = ClampI(proto_retries, 1, 100);             // §131 续
    proto_kind   = ClampI(proto_kind, 0, 1);                   // §132（下拉索引，防手改越界）
    proto_xcheck = ClampI(proto_xcheck, 0, 2);
    proto_xblk   = ClampI(proto_xblk, 0, 2);
    file_delay_ms  = ClampI(file_delay_ms, 0, 3600000);
    checksum_start = ClampI(checksum_start, 1, 1024);
    checksum_tail  = ClampI(checksum_tail, 1, 1024);

    // 透传：多条字符串原样保留；其余未知键（含 theme）原样保留、不改写
    const JsonPtr* ms = root->Get(L"multisend_rows");
    multisend_rows = (ms && *ms) ? *ms : JsonPtr();
    passthrough = JsonValue::Obj();
    for (const auto& kv : root->obj) {
        if (kv.first == L"multisend_rows") continue;
        if (kv.first == L"font" || kv.first == L"text_color") continue;
        // 已管理的键不重复写入透传
        bool managed = false;
        const wchar_t* names[] = { L"win_w", L"win_h", L"mode", L"port", L"baud", L"databits", L"stopbits",
            L"parity", L"flow", L"dtr", L"rts", L"auto_reconnect", L"reconnect_ms", L"open_on_start",
            L"remote_ip", L"remote_port", L"local_ip", L"local_port", L"encoding", L"hex_mode", L"auto_scroll",
            L"split_show", L"split_ms", L"copy_with_anno", L"save_with_anno",
            L"rx_ts_color", L"rx_ts_italic", L"rx_ts_underline",
            L"tx_ts_color", L"tx_ts_italic", L"tx_ts_underline", L"buffer_cap",
            L"bg_color", L"send_hex", L"send_interval_ms", L"send_newline", L"checksum_kind",
            L"checksum_start", L"checksum_tail", L"main_text", L"file_delay_ms", L"multisend_loop",
            L"term_enable", L"term_enter_send", L"term_key_send", L"term_key_echo", L"term_lf_send",
            L"proto_timeout_ms", L"proto_retries",
            L"proto_kind", L"proto_xcheck", L"proto_xblk", L"proto_show", L"proto_file",
            L"send_timed" };   // §110 / §131 / §132
        for (const wchar_t* nm : names) if (kv.first == nm) { managed = true; break; }
        if (!managed) passthrough->Set(kv.first.c_str(), kv.second);
    }
}

JsonPtr Config::ToJson() const {
    JsonPtr o = JsonValue::Obj();
    // 保持与 PyCom 相近的键顺序，便于人工比对
    o->Set(L"win_w", JsonValue::Num(win_w));
    o->Set(L"win_h", JsonValue::Num(win_h));
    o->Set(L"mode", JsonValue::Str(mode));
    o->Set(L"port", JsonValue::Str(port));
    o->Set(L"baud", JsonValue::Num(baud));
    o->Set(L"databits", JsonValue::Num(databits));
    o->Set(L"stopbits", JsonValue::Num(stopbits));
    o->Set(L"parity", JsonValue::Str(parity));
    o->Set(L"flow", JsonValue::Str(flow));
    o->Set(L"dtr", JsonValue::Bool(dtr));
    o->Set(L"rts", JsonValue::Bool(rts));
    o->Set(L"auto_reconnect", JsonValue::Bool(auto_reconnect));
    o->Set(L"reconnect_ms", JsonValue::Num(reconnect_ms));
    o->Set(L"open_on_start", JsonValue::Bool(open_on_start));
    o->Set(L"remote_ip", JsonValue::Str(remote_ip));
    o->Set(L"remote_port", JsonValue::Num(remote_port));
    o->Set(L"local_ip", JsonValue::Str(local_ip));
    o->Set(L"local_port", JsonValue::Num(local_port));
    o->Set(L"encoding", JsonValue::Str(encoding));
    o->Set(L"hex_mode", JsonValue::Bool(hex_mode));
    o->Set(L"auto_scroll", JsonValue::Bool(auto_scroll));
    o->Set(L"split_show", JsonValue::Bool(split_show));
    o->Set(L"split_ms", JsonValue::Num(split_ms));
    o->Set(L"copy_with_anno", JsonValue::Bool(copy_with_anno));
    o->Set(L"save_with_anno", JsonValue::Bool(save_with_anno));
    o->Set(L"rx_ts_color", JsonValue::Str(rx_ts_color));       // §68 收/发时间戳独立样式（§69 无加粗）
    o->Set(L"rx_ts_italic", JsonValue::Bool(rx_ts_italic));
    o->Set(L"rx_ts_underline", JsonValue::Bool(rx_ts_underline));
    o->Set(L"tx_ts_color", JsonValue::Str(tx_ts_color));
    o->Set(L"tx_ts_italic", JsonValue::Bool(tx_ts_italic));
    o->Set(L"tx_ts_underline", JsonValue::Bool(tx_ts_underline));
    o->Set(L"buffer_cap", JsonValue::Num(buffer_cap));

    JsonPtr f = JsonValue::Obj();
    f->Set(L"family", JsonValue::Str(font.family));
    f->Set(L"size", JsonValue::Num(font.size));
    f->Set(L"bold", JsonValue::Bool(font.bold));
    f->Set(L"italic", JsonValue::Bool(font.italic));
    f->Set(L"underline", JsonValue::Bool(font.underline));
    o->Set(L"font", f);
    o->Set(L"bg_color", JsonValue::Str(bg_color));
    o->Set(L"text_color", JsonValue::Str(text_color));

    o->Set(L"send_hex", JsonValue::Bool(send_hex));
    o->Set(L"send_interval_ms", JsonValue::Num(send_interval_ms));
    o->Set(L"send_newline", JsonValue::Bool(send_newline));
    o->Set(L"checksum_kind", JsonValue::Str(checksum_kind));
    o->Set(L"checksum_start", JsonValue::Num(checksum_start));
    o->Set(L"checksum_tail", JsonValue::Num(checksum_tail));
    o->Set(L"proto_timeout_ms", JsonValue::Num(proto_timeout_ms));   // §131
    o->Set(L"proto_retries", JsonValue::Num(proto_retries));         // §131 续
    o->Set(L"proto_kind", JsonValue::Num(proto_kind));               // §132
    o->Set(L"proto_xcheck", JsonValue::Num(proto_xcheck));
    o->Set(L"proto_xblk", JsonValue::Num(proto_xblk));
    o->Set(L"proto_show", JsonValue::Bool(proto_show));
    o->Set(L"proto_file", JsonValue::Str(proto_file));
    o->Set(L"send_timed", JsonValue::Bool(send_timed));
    o->Set(L"main_text", JsonValue::Str(main_text));
    o->Set(L"file_delay_ms", JsonValue::Num(file_delay_ms));
    o->Set(L"multisend_rows", multisend_rows ? multisend_rows : JsonValue::Arr());
    o->Set(L"multisend_loop", JsonValue::Bool(multisend_loop));
    o->Set(L"term_enable", JsonValue::Bool(term_enable));       // §140
    o->Set(L"term_enter_send", JsonValue::Bool(term_enter_send));
    o->Set(L"term_key_send", JsonValue::Bool(term_key_send));
    o->Set(L"term_key_echo", JsonValue::Bool(term_key_echo));
    o->Set(L"term_lf_send", JsonValue::Bool(term_lf_send));   // §110

    if (passthrough && passthrough->isObject())        // 未管理键原样写回（如 theme）
        for (const auto& kv : passthrough->obj) o->Set(kv.first.c_str(), kv.second);
    return o;
}

bool Config::Load(const std::wstring& path) {
    // §99（审查 U1）/§100（审查 P3）：本工程显式支持多实例——另一实例可能正持句柄写此文件。
    // 「读失败」只置提示不改名（可能是瞬时共享冲突/超 >64MB 上限，误改名会伤及并发写）；
    // 「解析失败」= 内容确证不可读（含深嵌套超限）→ 改名 .bad 备份，两者都提示「已重置为默认」。
    bool exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    std::wstring text;
    if (!ReadFileUtf8(path, text)) {
        if (exists) wasCorrupt = true;
        return false;
    }
    std::wstring err;
    JsonPtr root = JsonValue::Parse(text, &err);
    if (!root) {
        std::wstring bad = path + L".bad";
        MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING);
        wasCorrupt = true;
        return false;
    }
    FromJson(root);
    return true;
}

bool Config::Save(const std::wstring& path) const {
    return WriteFileUtf8(path, ToJson()->Dump(1));
}

} // namespace sncom
