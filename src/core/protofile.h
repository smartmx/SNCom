// protofile.h —— §129 协议文件发送（XMODEM / YMODEM，仅发送端；docs/17 §2 口径）
// 纯状态机：零线程、零 Win32 依赖（文件名由调用方转 UTF-8 传入），可被 tools/protofileself
// 直接驱动。停等式：在途最多一个块；对端字节经 Feed() 喂入（容忍 TCP 粘包/串口分片的半块
// 与乱码前导）；待发字节经 tx()/Consume() 对接链路（Write 部分接受语义，§99）。
// 超时不内藏时钟：UI 层计时到点调 OnTimeout()（自测可确定性驱动）。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sncom {

enum class ProtoKind { Xmodem, Ymodem };
enum class ProtoState { Idle, Running, Done, Aborted, Error };

// XMODEM 校验方式（§129 续修3 用户拍板）：Auto=按对端首踢自适应（NAK→校验和/'C'→CRC+1K）；
// Crc16=无论首踢为何一律 CRC16（1K 首块不升级）；Checksum=一律 1B 校验和（128B）。
// YMODEM 恒 CRC16，此参数不参与。
enum class XCheck { Auto, Crc16, Checksum };

// XMODEM 块大小（§129 续修4 用户拍板）：Auto=随首踢自适应（'C'→1024/NAK→128，与校验方式
// 联动同前）；Blk1k/Blk128=固定块长（校验方式仍独立生效——出现 1K+校验和 / 128+CRC 组合，
// 非标但收端可按帧头 STX/SOH 自行解析）。
// §129 续修5：**YMODEM 也支持块大小**（用户拍板）——YMODEM 数据块按此参数定档（Auto/1K=1024、
// 128=SOH+128 数据块；块 0 恒 SOH+128 协议标准），仅校验方式不参与（恒 CRC16）。
enum class XBlk { Auto, Blk1k, Blk128 };

struct ProtoProgress {
    unsigned block = 0;                 // 在途/最近块号（YMODEM 批含块 0）
    unsigned blocks = 0;                // 数据块总数（XMODEM 首踢定档后才可知，前为 0）
    unsigned retries = 0;               // 当前块重发次数
    unsigned long long confirmed = 0;   // 对端已确认数据字节
    unsigned long long total = 0;       // 文件总字节
    const wchar_t* stage = L"";         // 阶段描述（状态行展示用）
};

class ProtoSender {
public:
    // 载入文件并进入 Running：XMODEM 等对端首踢（check=Auto：NAK=校验和 128B / 'C'=CRC16
    // 且自动升 1K；Crc16/Checksum=固定模式，首踢仅作放行、不改档）；YMODEM 等 'C' → 块 0
    // （SOH+128+CRC16：文件名\0 + 长度十进制 ASCII\0 + 0x00 补齐）。块 0 只在 'C' 到来后
    // 入队——'C' 前不发任何字节（YMODEM 收端先踢后收）。
    // §131 续：maxRetries=超时重发上限（用户可设，默认 10）——统一作用于数据块/块 0/EOT
    // 三处超时重发（超限 → Error：重发超限 / 块 0 重发超限 / EOT 重发超限）。
    void Start(ProtoKind kind, std::vector<uint8_t> data, const std::string& nameUtf8,
               XCheck check = XCheck::Auto, XBlk blk = XBlk::Auto, unsigned maxRetries = 10);
    void Feed(const uint8_t* d, size_t n);      // 对端字节流（乱码前导丢弃；CAN CAN 中止）
    void OnTimeout();                            // 3s 无应答：重发当前块/EOT（超限 → Error）
    void Cancel();                               // 用户取消 → CAN CAN BS BS → Aborted
    bool WaitingAck() const;                     // tx 已排空且在等对端应答（驱动 3s 超时）
    const std::vector<uint8_t>& tx() const { return tx_; }
    void Consume(size_t n);                      // 链路实发 n 字节后回调
    ProtoState state() const { return st_; }
    const ProtoProgress& progress() const { return pg_; }
    const std::wstring& error() const { return err_; }

private:
    enum Phase { PhWaitKick, PhHdrAck, PhWaitKick2, PhDataAck, PhEot, PhEot2, PhWaitC3, PhFinAck };
    void BuildHeaderBlock();                     // YMODEM 块 0 → lastHdr_（名字 + 长度 + 补零）
    void SendBlock(unsigned num, const uint8_t* blk, bool hdr);
    void QueueEot();
    void SendNextDataBlock();                    // dataOff_ 起的新块（推进确认偏移时用）
    void ResendCurrentBlock();                   // 同块重发（dataOff_ 不动；清 tx_ 防残块拼接）
    void HandleCtl(uint8_t b);                   // ACK/NAK/'C' 按相位分流（CAN 在 Feed 里截获）
    void Fail(const wchar_t* why);
    void SetStage(const wchar_t* s);

    ProtoKind kind_ = ProtoKind::Xmodem;
    XCheck check_ = XCheck::Auto;                // §129 续修3：XMODEM 校验方式（用户可选）
    XBlk xblk_ = XBlk::Auto;                     // §129 续修4：XMODEM 块大小（用户可选）
    ProtoState st_ = ProtoState::Idle;
    Phase ph_ = PhWaitKick;
    bool crcMode_ = true;                        // XMODEM：NAK 首踢降级为 1B 校验和
    bool lastWasCan_ = false;                    // R23-P3-5：孤立 CAN 粘性——跨 Feed 分片的 CAN CAN 也识别
    unsigned blk_ = 1;                           // 在途数据块号（mod 256）
    unsigned retries_ = 0, hdrRetries_ = 0, eotTries_ = 0;
    unsigned maxRtx_ = 10;                       // §131 续：超时重发上限（调用方经 Start 传入）
    size_t dataOff_ = 0;                         // 已确认数据偏移
    size_t blkSz_ = 128;                         // 当前块数据域长度（XMODEM 首踢定档）
    std::vector<uint8_t> data_;                  // 文件内容
    std::string name_;                           // 文件名（UTF-8 原样序列）
    std::vector<uint8_t> lastHdr_;               // 最近一次组好的块 0（128B，重发/收批复用）
    std::vector<uint8_t> tx_;                    // 待发字节
    std::vector<uint8_t> rxq_;                   // 对端字节缓冲
    ProtoProgress pg_;
    std::wstring err_;
};

}  // namespace sncom
