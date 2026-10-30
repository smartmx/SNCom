// protofile.cpp —— §129 XMODEM/YMODEM 发送端状态机（docs/17 §2 表格口径的唯一实现源）
// 控制字节：SOH=0x01 STX=0x02 EOT=0x04 ACK=0x06 BS=0x08 NAK=0x15 CAN=0x18 SUB=0x1A 'C'=0x43
#include "protofile.h"
#include <cstdio>
#include <cstring>

namespace sncom {

static const uint8_t kSoh = 0x01, kStx = 0x02, kEot = 0x04, kAck = 0x06,
                     kBksp = 0x08, kNak = 0x15, kCan = 0x18, kSub = 0x1A,
                     kC = 0x43;
// §131 续：超时重发上限由 Start(maxRetries) 传入（默认 10），统一用于数据块/块 0/EOT 三处

// CRC-16/XMODEM：poly 0x1021、初值 0、不反射、无终值异或（"123456789"→0x31C3 自测守 Vector）
static unsigned Crc16Xmodem(const uint8_t* d, size_t n) {
    unsigned crc = 0;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (unsigned)d[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xFFFF;
    }
    return crc;
}

void ProtoSender::Consume(size_t n) {
    if (n >= tx_.size()) tx_.clear();
    else tx_.erase(tx_.begin(), tx_.begin() + (std::ptrdiff_t)n);
}

void ProtoSender::SetStage(const wchar_t* s) { pg_.stage = s; }

void ProtoSender::Fail(const wchar_t* why) {
    err_ = why;
    st_ = ProtoState::Error;
    tx_.clear();
}

void ProtoSender::Start(ProtoKind kind, std::vector<uint8_t> data, const std::string& nameUtf8,
                        XCheck check, XBlk blk, unsigned maxRetries) {
    kind_ = kind;
    check_ = check;
    xblk_ = blk;
    maxRtx_ = (maxRetries == 0) ? 10 : maxRetries;   // §131 续：0 防呆→默认 10
    data_ = std::move(data);
    name_ = nameUtf8;
    st_ = ProtoState::Running;
    ph_ = PhWaitKick;
    // 初档（§129 续修3/4）：校验=Auto 等首踢定档 / 固定模式即刻定档；块大小=Auto 随首踢
    // （'C'→1024/NAK→128）/ 显式 1K/128 固定（校验方式独立生效，允许 1K+校验和 等组合）。
    crcMode_ = (check_ != XCheck::Checksum);
    lastWasCan_ = false;
    if (kind_ == ProtoKind::Ymodem) blkSz_ = (blk == XBlk::Blk128) ? 128 : 1024;   // 续修5：YMODEM 数据块可 128
    else if (blk == XBlk::Blk1k) blkSz_ = 1024;
    else if (blk == XBlk::Blk128) blkSz_ = 128;
    else blkSz_ = (check_ == XCheck::Crc16) ? 1024 : 128;   // Auto×Auto：NAK 踢落 128
    blk_ = 1;
    retries_ = hdrRetries_ = eotTries_ = 0;
    dataOff_ = 0;
    rxq_.clear();
    tx_.clear();
    lastHdr_.clear();
    err_.clear();
    pg_ = ProtoProgress{};
    pg_.total = data_.size();
    if (kind_ == ProtoKind::Ymodem) {
        BuildHeaderBlock();                    // 组好待 'C'（不排队——'C' 前不发任何字节）
        pg_.blocks = (unsigned)((data_.size() + blkSz_ - 1) / blkSz_);
    }
    SetStage(L"等待对端启动");
}

void ProtoSender::BuildHeaderBlock() {
    // 「名\0长度\0」必须整体放得下：先按长度域位数预留，名字截到 nameMax——名字占满时
    // 终止符恰落 [127]，绝不再写 [128]（R23-P2-1：长名 1B 堆越界 + 长度字段被挤没）。
    char szs[24];
    snprintf(szs, sizeof(szs), "%llu", (unsigned long long)data_.size());
    size_t szLen = strlen(szs);
    size_t nameMax = 128 - 1 - (szLen + 1);    // 名字可用字节（名\0 + 长度域\0 另计）
    lastHdr_.assign(128, 0);
    size_t pos = 0;
    for (unsigned char c : name_) {
        if (pos >= nameMax) break;
        lastHdr_[pos++] = c;
    }
    lastHdr_[pos++] = 0;                       // 名终止符（名字占满时 pos==127）
    for (size_t i = 0; i < szLen; ++i) lastHdr_[pos++] = (uint8_t)szs[i];
    lastHdr_[pos] = 0;                         // 长度域终止符（pos≤127 恒成立）
}

// 块封包：SOH/STX + 块号 + 补码 + 数据域（调用方保证长度=块长，末块 0x1A 填充）+ CRC16/校验和。
// hdr=块 0（恒 SOH+128+CRC16）；数据块按 blkSz_ 定档——**块号回绕到线序 0 的数据块仍是数据块**
// （301 块的文件第 256 块线序为 0，不能用 num==0 判头）。tx_ 先清空：重发路径可能带着上块
// 残量进来（接收端对半块必然 NAK/超时，整块重发是协议口径）。
void ProtoSender::SendBlock(unsigned num, const uint8_t* blk, bool hdr) {
    size_t sz = hdr ? 128 : blkSz_;
    bool use1k = !hdr && blkSz_ == 1024;
    tx_.clear();
    uint8_t h[3] = { use1k ? kStx : kSoh, (uint8_t)(num & 0xFF),
                     (uint8_t)(255 - (num & 0xFF)) };
    tx_.insert(tx_.end(), h, h + 3);
    tx_.insert(tx_.end(), blk, blk + sz);
    if (hdr || crcMode_ || kind_ == ProtoKind::Ymodem) {
        unsigned c = Crc16Xmodem(blk, sz);
        tx_.push_back((uint8_t)(c >> 8));      // 高字节在前（XMODEM CRC 线序）
        tx_.push_back((uint8_t)(c & 0xFF));
    } else {
        unsigned sum = 0;
        for (size_t i = 0; i < sz; ++i) sum += blk[i];
        tx_.push_back((uint8_t)(sum & 0xFF));
    }
    pg_.block = num;
    pg_.retries = retries_;
}

void ProtoSender::QueueEot() {
    tx_.clear();
    tx_.push_back(kEot);
}

// 新数据块：dataOff_ 起取一块（末块 0x1A 填充）；文件发完/空文件 → 直接进 EOT 相位。
void ProtoSender::SendNextDataBlock() {
    if (dataOff_ >= data_.size()) {
        QueueEot();
        ph_ = PhEot;
        eotTries_ = 0;
        SetStage(L"结束传输");
        return;
    }
    std::vector<uint8_t> blk(blkSz_, kSub);
    size_t n = data_.size() - dataOff_;
    if (n > blkSz_) n = blkSz_;
    memcpy(blk.data(), data_.data() + dataOff_, n);
    SendBlock(blk_, blk.data(), false);
    retries_ = 0;
    pg_.retries = 0;
    SetStage(L"传输数据");
}

// 同块重发：dataOff_ 不动（确认偏移只在 ACK 时推进）。
void ProtoSender::ResendCurrentBlock() {
    std::vector<uint8_t> blk(blkSz_, kSub);
    size_t n = data_.size() - dataOff_;
    if (n > blkSz_) n = blkSz_;
    if (n > 0) memcpy(blk.data(), data_.data() + dataOff_, n);
    SendBlock(blk_, blk.data(), false);
    SetStage(L"传输数据");
}

void ProtoSender::HandleCtl(uint8_t b) {
    if (b == kAck) {
        switch (ph_) {
        case PhHdrAck:                           // YMODEM 块 0 确认 → 等第二个 'C'
            ph_ = PhWaitKick2;
            SetStage(L"等待接收就绪");
            break;
        case PhDataAck: {                        // 数据块确认 → 下一块或 EOT
            dataOff_ += blkSz_;
            if (dataOff_ > data_.size()) dataOff_ = data_.size();
            pg_.confirmed = dataOff_;
            blk_ = (blk_ + 1) & 0xFF;
            retries_ = 0;
            pg_.retries = 0;
            if (dataOff_ >= data_.size()) {
                QueueEot();
                ph_ = PhEot;
                eotTries_ = 0;
                SetStage(L"结束传输");
            } else {
                SendNextDataBlock();
            }
            break;
        }
        case PhEot:
            if (kind_ == ProtoKind::Xmodem) {    // XMODEM：ACK 收尾
                st_ = ProtoState::Done;
                pg_.confirmed = data_.size();
            } else {                             // YMODEM：个别收端首 EOT 即 ACK（宽松口径）
                ph_ = PhWaitC3;                  //   → 直接等 'C' 收批
                SetStage(L"收批");
            }
            break;
        case PhEot2:                             // YMODEM 双确认第二步完成 → 等 'C'
            ph_ = PhWaitC3;
            SetStage(L"收批");
            break;
        case PhFinAck:                           // 空块 0 确认 → 批结束
            st_ = ProtoState::Done;
            pg_.confirmed = data_.size();
            break;
        default:
            break;                               // 等踢相位的 ACK=杂散，丢弃
        }
        return;
    }
    if (b == kNak) {
        switch (ph_) {
        case PhWaitKick:
            if (kind_ == ProtoKind::Xmodem) {
                if (check_ == XCheck::Auto) {    // NAK 首踢 → 校验和模式（Auto 才降级）
                    crcMode_ = false;
                    if (xblk_ == XBlk::Auto)     // 块大小显式指定时保持用户档
                        blkSz_ = 128;
                }
                // 固定 CRC/校验和模式：首踢仅放行，保持 Start 初档（docs/17 §2 变体口径）
                pg_.blocks = (unsigned)((data_.size() + blkSz_ - 1) / blkSz_);
                SendNextDataBlock();
                ph_ = PhDataAck;
            }
            break;                               // YMODEM 等 'C' 阶段的 NAK=乱码，丢弃
        case PhHdrAck:
        case PhFinAck:
            if (++hdrRetries_ > maxRtx_) { Fail(L"块 0 重发超限"); return; }
            pg_.retries = hdrRetries_;
            SendBlock(0, lastHdr_.data(), true);
            break;
        case PhDataAck:
            if (++retries_ > maxRtx_) { Fail(L"重发超限"); return; }
            pg_.retries = retries_;
            ResendCurrentBlock();
            break;
        case PhEot:
        case PhEot2:
            if (++eotTries_ > maxRtx_) { Fail(L"EOT 重发超限"); return; }
            QueueEot();
            if (ph_ == PhEot && kind_ == ProtoKind::Ymodem) ph_ = PhEot2;  // 双确认第一步
            break;
        default:
            break;
        }
        return;
    }
    if (b == kC) {
        switch (ph_) {
        case PhWaitKick:
            if (kind_ == ProtoKind::Ymodem) {    // 'C' → 发块 0（此时才允许上线）
                SendBlock(0, lastHdr_.data(), true);
                ph_ = PhHdrAck;
                SetStage(L"发送文件头");
            } else {                             // XMODEM：'C' 首踢
                if (check_ == XCheck::Auto) {    // 自动：CRC 模式 + 块大小随首踢升 1K
                    crcMode_ = true;
                    if (xblk_ == XBlk::Auto)     // 块大小显式指定时保持用户档
                        blkSz_ = 1024;
                } else if (check_ == XCheck::Crc16) {
                    crcMode_ = true;             // 固定 CRC：块大小按用户档（无校验和降级路径）
                }
                // 固定 Checksum：保持 1B 和（Start 初档），'C' 仅放行
                pg_.blocks = (unsigned)((data_.size() + blkSz_ - 1) / blkSz_);
                SendNextDataBlock();
                ph_ = PhDataAck;
            }
            break;
        case PhWaitKick2:                        // 块 0 ACK 后的第二个 'C' → 首个数据块
            blk_ = 1;
            retries_ = 0;
            pg_.retries = 0;
            SendNextDataBlock();
            ph_ = PhDataAck;
            break;
        case PhWaitC3:                           // EOT 双确认后的 'C' → 空块 0 收批
            lastHdr_.assign(128, 0);
            SendBlock(0, lastHdr_.data(), true);
            ph_ = PhFinAck;
            break;
        default:
            break;                               // 杂散 'C' 丢弃
        }
        return;
    }
    // 其余字节=乱码前导，丢弃（docs/17 §2 容错；孤立 CAN 同此处理）
}

void ProtoSender::Feed(const uint8_t* d, size_t n) {
    if (st_ != ProtoState::Running) return;
    rxq_.insert(rxq_.end(), d, d + n);
    size_t idx = 0;
    while (idx < rxq_.size() && st_ == ProtoState::Running) {
        uint8_t b = rxq_[idx];
        if (b == kCan) {
            // CAN CAN 中止：同缓冲相邻两 CAN 或「上拍末字节是 CAN + 本拍任一 CAN」
            // （R23-P3-5：串口分片读会把两个 CAN 劈进两次 Feed，孤立 CAN 需粘性记账）
            if (lastWasCan_ || (idx + 1 < rxq_.size() && rxq_[idx + 1] == kCan)) {
                rxq_.clear();
                tx_.clear();                     // 在途残块一并作废
                err_ = L"对端取消";
                st_ = ProtoState::Aborted;
                return;
            }
            lastWasCan_ = true;
            ++idx;                               // 孤立 CAN：按乱码前导丢弃，粘住等下一拍
            continue;
        }
        lastWasCan_ = false;                     // 非 CAN 字节断开粘性
        rxq_.erase(rxq_.begin(), rxq_.begin() + (std::ptrdiff_t)(idx + 1));
        idx = 0;
        HandleCtl(b);
    }
    if (idx) rxq_.erase(rxq_.begin(), rxq_.begin() + (std::ptrdiff_t)idx);
}

void ProtoSender::OnTimeout() {
    if (st_ != ProtoState::Running) return;
    switch (ph_) {
    case PhWaitKick:
    case PhWaitKick2:
    case PhWaitC3:
        return;                                  // 等首踢/第二踢不限时（docs/17 §2；界面可取消）
    case PhHdrAck:
    case PhFinAck:
        if (++hdrRetries_ > maxRtx_) { Fail(L"块 0 重发超限"); return; }
        pg_.retries = hdrRetries_;
        SendBlock(0, lastHdr_.data(), true);
        break;
    case PhDataAck:
        if (++retries_ > maxRtx_) { Fail(L"重发超限"); return; }
        pg_.retries = retries_;
        ResendCurrentBlock();
        break;
    case PhEot:
    case PhEot2:
        if (++eotTries_ > maxRtx_) { Fail(L"EOT 重发超限"); return; }
        QueueEot();
        break;
    }
}

void ProtoSender::Cancel() {
    if (st_ != ProtoState::Running) return;
    tx_.clear();
    const uint8_t abortSeq[4] = { kCan, kCan, kBksp, kBksp };
    tx_.assign(abortSeq, abortSeq + 4);          // CAN CAN BS BS（docs/17 §2 取消序列）
    err_ = L"已取消";
    st_ = ProtoState::Aborted;
}

bool ProtoSender::WaitingAck() const {
    return st_ == ProtoState::Running && tx_.empty() &&
           (ph_ == PhHdrAck || ph_ == PhDataAck || ph_ == PhEot || ph_ == PhEot2 ||
            ph_ == PhFinAck);
}

}  // namespace sncom
