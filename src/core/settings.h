// sncom/src/core/settings.h —— 配置（键名与 PyCom pycom.json 对齐；差异见 docs/00-决策记录.md）
#pragma once
#include <string>
#include <memory>
#include "json.h"

namespace sncom {

struct FontSpec {
    std::wstring family = L"宋体";
    int  size      = 10;      // 磅
    bool bold      = false;
    bool italic    = false;
    bool underline = false;
};

struct Config {
    // 窗口
    int win_w = 960, win_h = 720;            // §82（用户拍板）：默认 960×720——工具行在
                                             // 960 下靠 kToolGapTail 收口保证「扩展」完整可见
    // 连接
    std::wstring mode = L"serial";           // serial | tcp_client | tcp_server | udp
    std::wstring port;                       // COMx
    int  baud = 115200, databits = 8;
    double stopbits = 1.0;                   // 1 | 1.5 | 2
    std::wstring parity = L"none";           // none|even|odd|mark|space
    std::wstring flow   = L"none";           // none|rtscts|xonxoff
    bool dtr = true, rts = false;
    bool auto_reconnect = true;
    int  reconnect_ms = 2000;
    bool open_on_start = false;
    std::wstring remote_ip = L"127.0.0.1";
    int  remote_port = 8080;
    std::wstring local_ip = L"0.0.0.0";
    int  local_port = 777;
    // 显示
    std::wstring encoding = L"gbk";
    bool hex_mode = false;
    bool auto_scroll = true;
    bool split_show  = true;
    int  split_ms    = 20;
    bool copy_with_anno = true, save_with_anno = true;
    std::wstring rx_ts_color, tx_ts_color;   // 空 = 随主题默认
    bool rx_ts_italic = false, rx_ts_underline = false;
    bool tx_ts_italic = false, tx_ts_underline = false;   // §69：收/发时间戳各独立样式（加粗移除——
                                                          // 字宽变化破坏折行网格；家族/字号随显示字体）
    int  buffer_cap = 1024 * 1024;           // 默认 1M（§73：档位 500K/1/5/20/50/100M，默认 1M）
    // 字体与颜色（统一一份：text_color 取代 PyCom 的 fg_color；不再有 send_font/recv_font）
    FontSpec font;
    std::wstring bg_color   = L"#FFFFFF";
    std::wstring text_color = L"#000000";
    // 发送
    bool send_hex = false;
    int  send_interval_ms = 1000;
    bool send_newline = true;
    std::wstring checksum_kind = L"none";
    int  checksum_start = 1;
    int  checksum_tail  = 1;
    std::wstring main_text;
    int  file_delay_ms = 0;
    bool multisend_loop = false;
    int  proto_timeout_ms = 1000;                  // §131：协议发送应答超时（默认 1s，范围 100~60000）
    int  proto_retries = 10;                       // §131 续：协议发送超时重发上限（次数，范围 1~100）
    // §132（用户要求全量保存）：协议页界面态 5 项 + 定时发送勾选态
    int  proto_kind = 0;                           // 0=XMODEM 1=YMODEM
    int  proto_xcheck = 0;                         // 0=自动 1=CRC 2=校验和
    int  proto_xblk = 0;                           // 0=自动 1=1024 2=128
    bool proto_show = false;                       // 「将收发内容显示到窗口」
    std::wstring proto_file;                       // 协议发送文件路径
    bool send_timed = false;                       // 定时发送勾选（仅恢复武装态，不自动开跑）
    // 终端（§110：M6 首增量起有消费方——接收区直接输入）
    bool term_enable = false;                // §140：终端仿真总开关（默认关——接收区不响应键盘）
    bool term_enter_send = true;             // 方式A：回车后发送新行（带回显）——与 term_key_send 互斥
    bool term_key_send   = false;            // 方式B：按键立即发送该键值
    bool term_key_echo   = false;            // 本地回显按键值（仅方式B可开）
    bool term_lf_send    = false;            // 发送 0x0d 后自动补 0x0a

    // M3 之前的透传：多条字符串原始 JSON（保存时原样写回，避免丢用户数据）
    JsonPtr multisend_rows;
    // 配置里出现但本版不用的键（theme 等）——读到后原样保留、不改写
    JsonPtr passthrough;
    // §99（审查 U1）/§100：配置读取失败或损坏——**仅解析失败**才改名 .bad 备份（读失败
    // 可能是多实例瞬时共享冲突，不落盘）；本标志供 UI 启动建窗后 Notify「已重置为默认」
    bool wasCorrupt = false;

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
    void FromJson(const JsonPtr& root);
    JsonPtr ToJson() const;
};

} // namespace sncom
