// protofileself.cpp —— §129 XMODEM/YMODEM 发送端自测（docs/17 §4；calcself 先例）
// 用法：protofileself.exe <result.txt>；行 "proto N ok/MISMATCH"（退出码 0/1）。
// 虚拟收端（参考接收器，CRC 复算与发送端相互独立）与被测状态机对拍：正常流/坏块/恒 NAK
// 超限/乱码前导/逐字节喂入/CAN 中止/EOT 双确认/无第二 C 不限时/取消/空文件/块号回绕。
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "../src/core/protofile.h"

using namespace sncom;

static int gPass = 0, gTotal = 0;
static FILE* gFo = nullptr;

static void chk(int idx, bool ok, const char* note) {
    ++gTotal;
    gPass += ok;
    fprintf(gFo, "proto %d %s%s%s\n", idx, ok ? "ok" : "MISMATCH", note[0] ? " " : "", note);
}

static std::vector<uint8_t> MakeData(size_t n, unsigned seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)((i * 7 + seed * 31 + (i >> 8)) & 0xFF);
    return v;   // 含 0x1A 值——YMODEM 按长度截断、XMODEM 按尾部 SUB 裁剪的边界素材
}

// ---------------- 虚拟收端（参考接收器，可注入故障） ----------------
struct VPeer {
    ProtoKind kind = ProtoKind::Xmodem;
    bool checksumKick = false;     // XMODEM：NAK 首踢（校验和模式，128B+1B 和）
    bool corruptOnce = false;      // 收到下一整块后回 NAK 一次
    bool alwaysNak = false;        // 恒 NAK（重发超限测试）
    int  eotNakFirst = 1;          // 首批 EOT 先 NAK 的次数（XMODEM；YMODEM 标准双确认=1）
    bool eotAckFirst = false;      // 首个 EOT 直接 ACK（YMODEM 宽松收端）
    bool noSecondC = false;        // 块 0 ACK 后不发第二个 'C'（卡死等待测试）
    bool noFinalC = false;
    int  garbagePrefix = 0;        // 首踢前注入的乱码字节数
    bool dropKick = false;         // 不主动首踢（超时/手动踢用例）
    bool byteWise = false;         // 发送侧逐字节出队（Consume(1) 分片容错）

    std::vector<uint8_t> txq;      // 收端 → 发送端
    std::vector<uint8_t> acc;      // 帧累积
    std::vector<uint8_t> out;      // 还原的数据
    std::string name;
    unsigned long long size = 0, got = 0;
    unsigned expect = 1;           // 期望块号计数（线序号 = expect & 0xFF，mod 256 回绕）
    int eotSeen = 0;
    bool hdrAcked = false, finalC = false, done = false, aborted = false;
    unsigned dupAcks = 0, naks = 0;

    explicit VPeer(ProtoKind k) : kind(k) { expect = (k == ProtoKind::Ymodem) ? 0 : 1; }

    void Kick() {
        if (dropKick) return;
        for (int i = 0; i < garbagePrefix; ++i) txq.push_back((uint8_t)(0xF0 + i));
        txq.push_back(checksumKick ? 0x15 : 0x43);
    }
    void SendCtl(uint8_t b) { txq.push_back(b); }
    size_t FrameLen() const {
        if (acc.size() < 3) return 0;
        size_t sz = (acc[0] == 0x02) ? 1024 : 128;
        // 校验和模式由【首踢协商】决定（NAK=和），与帧头 SOH/STX 无关——
        // 显式块长组合允许 SOH+1024+和（§129 续修4）
        bool sum = (kind == ProtoKind::Xmodem && checksumKick);
        return 3 + sz + (sum ? 1 : 2);
    }

    void OnFrame(const std::vector<uint8_t>& f) {
        uint8_t ctl = f[0];
        size_t sz = (ctl == 0x02) ? 1024 : 128;
        bool sum = (kind == ProtoKind::Xmodem && checksumKick);   // 首踢协商，见 FrameLen
        if (f.size() < 3 + sz + (sum ? 1 : 2)) return;
        if (alwaysNak) { SendCtl(0x15); ++naks; return; }
        unsigned num = f[1], cnum = f[2];
        if (num != (unsigned)(255 - cnum)) { SendCtl(0x15); ++naks; return; }
        if (sum) {
            unsigned s = 0;
            for (size_t i = 3; i < 3 + sz; ++i) s += f[i];
            if (f[3 + sz] != (uint8_t)(s & 0xFF)) { SendCtl(0x15); ++naks; return; }
        } else {
            unsigned crc = 0;                   // 与发送端相互独立的复算实现
            for (size_t i = 3; i < 3 + sz; ++i) {
                crc ^= (unsigned)f[i] << 8;
                for (int b = 0; b < 8; ++b)
                    crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
                crc &= 0xFFFF;
            }
            if (f[3 + sz] != (uint8_t)(crc >> 8) || f[4 + sz] != (uint8_t)(crc & 0xFF)) {
                SendCtl(0x15); ++naks; return;
            }
        }
        if (corruptOnce) { corruptOnce = false; SendCtl(0x15); ++naks; return; }
        if (num == (expect & 0xFF)) {
            if (num == 0 && kind == ProtoKind::Ymodem && !hdrAcked) {   // YMODEM 块 0
                name.clear();
                size_t p = 3;
                while (p < 3 + sz && f[p]) name += (char)f[p++];
                ++p;
                std::string szs;
                while (p < 3 + sz && f[p]) szs += (char)f[p++];
                size = strtoull(szs.c_str(), nullptr, 10);
                got = 0; out.clear();
                hdrAcked = true;
            } else {
                unsigned long long take = sz;
                if (kind == ProtoKind::Ymodem && got + take > size) take = size - got;
                out.insert(out.end(), f.begin() + 3, f.begin() + 3 + (size_t)take);
                got += take;
            }
            ++expect;
            SendCtl(0x06);
            if (kind == ProtoKind::Ymodem && num == 0 && !noSecondC) SendCtl(0x43);
            return;
        }
        if (num == (unsigned)((expect - 1) & 0xFF)) { SendCtl(0x06); ++dupAcks; return; }
        SendCtl(0x15); ++naks;                  // 序号错乱
    }

    void OnEot() {
        ++eotSeen;
        if (kind == ProtoKind::Xmodem) {
            if (eotSeen <= eotNakFirst) { SendCtl(0x15); return; }
            SendCtl(0x06); done = true;
            return;
        }
        if (eotSeen == 1 && !eotAckFirst) { SendCtl(0x15); return; }   // 双确认第一步
        SendCtl(0x06);
        if (!noFinalC) { SendCtl(0x43); finalC = true; }
    }

    void OnFinalHdr(const std::vector<uint8_t>& f) {   // 空块 0（收批）
        if (f.size() >= 133 && f[1] == 0 && f[2] == 0xFF) { SendCtl(0x06); done = true; }
        else SendCtl(0x15);
    }

    void OnTx(const std::vector<uint8_t>& d) {         // 发送端出队字节 → 收端解析
        for (uint8_t b : d) {
            if (aborted || done) return;
            if (acc.empty()) {                         // 帧间才解释控制字节（真实收端是
                if (b == 0x04) { OnEot(); continue; }  //   帧态感知的：数据域 04/18/08 是数据）
                if (b == 0x18) { aborted = true; continue; }
                if (b == 0x08) continue;               // 取消序列的 BS
                if (b != 0x01 && b != 0x02) continue;  // 乱码前导
            }
            acc.push_back(b);                          // 帧内：数据域任意值（含 04/18/08）
            size_t need = FrameLen();
            if (need && acc.size() >= need) {
                std::vector<uint8_t> f(acc.begin(), acc.begin() + (std::ptrdiff_t)need);
                acc.erase(acc.begin(), acc.begin() + (std::ptrdiff_t)need);
                if (f[1] == 0 && kind == ProtoKind::Ymodem && finalC) OnFinalHdr(f);
                else OnFrame(f);
            }
        }
    }
};

// ---------------- 会话驱动 ----------------
struct Drive {
    ProtoSender* s = nullptr;
    VPeer* v = nullptr;
    bool run() {
        v->Kick();
        for (long it = 0; it < 400000; ++it) {
            if (s->state() != ProtoState::Running) break;
            if (!s->tx().empty()) {
                size_t n = s->tx().size();
                if (v->byteWise) n = 1;
                std::vector<uint8_t> chunk(s->tx().begin(), s->tx().begin() + (std::ptrdiff_t)n);
                v->OnTx(chunk);
                s->Consume(n);
            }
            if (!v->txq.empty()) {               // 收端应答同轮回喂（停等无空窗，防误超时）
                std::vector<uint8_t> r;
                r.swap(v->txq);
                s->Feed(r.data(), r.size());
            } else if (s->WaitingAck()) {
                s->OnTimeout();                  // 收端真静默 → 3s 到点（重试口径案）
            }
        }
        return true;
    }
};

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "proto_self.txt";
    fopen_s(&gFo, out, "w");
    if (!gFo) return 2;

    // 1 XMODEM CRC 1K 正常流（2.5 块，末块 0x1A 填充；收端 CRC 复算独立实现）
    {
        auto d = MakeData(2560, 1);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f1.bin");
        VPeer v(ProtoKind::Xmodem);
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 1024) want.push_back(0x1A);
        chk(1, s.state() == ProtoState::Done && v.out == want, "xmodem crc 1K normal");
    }
    // 2 XMODEM 校验和模式（NAK 首踢，128B+1B 和；收端按和校验）
    {
        auto d = MakeData(300, 2);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f2.bin");
        VPeer v(ProtoKind::Xmodem); v.checksumKick = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 128) want.push_back(0x1A);
        chk(2, s.state() == ProtoState::Done && v.out == want, "xmodem checksum 128B");
    }
    // 3 坏块 NAK 一次 → 同块重发后完成
    {
        auto d = MakeData(1500, 3);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f3.bin");
        VPeer v(ProtoKind::Xmodem); v.corruptOnce = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 1024) want.push_back(0x1A);
        chk(3, s.state() == ProtoState::Done && v.out == want && v.naks == 1 &&
               s.progress().retries <= 1, "xmodem bad-block nak retry");
    }
    // 4 恒 NAK → 同块重发 10 次后 Error（docs/17 §2 重发上限）
    {
        auto d = MakeData(100, 4);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f4.bin");
        VPeer v(ProtoKind::Xmodem); v.alwaysNak = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(4, s.state() == ProtoState::Error, "always-nak retry-exceeded error");
    }
    // 5 ACK 超时无应答 → 重发超限 Error（收端完全静默）
    {
        auto d = MakeData(100, 5);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f5.bin");
        uint8_t c = 0x43;
        s.Feed(&c, 1);                               // 'C' 首踢 → 块 1 在途
        for (int i = 0; i < 100 && s.state() == ProtoState::Running; ++i) s.OnTimeout();
        chk(5, s.state() == ProtoState::Error, "ack-timeout retry-exceeded error");
    }
    // 6 CAN 中途中止 → Aborted 且 tx_ 清空（不残留半块）
    {
        auto d = MakeData(3000, 6);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f6.bin");
        VPeer v(ProtoKind::Xmodem);
        Drive dr; dr.s = &s; dr.v = &v;
        v.Kick();
        for (int it = 0; it < 9000 && s.state() == ProtoState::Running; ++it) {
            if (!s.tx().empty()) { std::vector<uint8_t> c(s.tx()); v.OnTx(c); s.Consume(c.size()); }
            if (!v.txq.empty()) { std::vector<uint8_t> r; r.swap(v.txq); s.Feed(r.data(), r.size()); }
            if (s.state() == ProtoState::Running && s.progress().confirmed > 1200) {
                uint8_t cc[2] = { 0x18, 0x18 };  // 对端中途 CAN CAN（线序直达发送端）
                s.Feed(cc, 2);
            }
        }
        chk(6, s.state() == ProtoState::Aborted && s.tx().empty(),
            "xmodem can abort");
    }
    // 7 逐字节喂入 + 逐字节出队（TCP 粘包/串口半块的极限分片）
    {
        auto d = MakeData(700, 7);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f7.bin");
        VPeer v(ProtoKind::Xmodem); v.byteWise = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 1024) want.push_back(0x1A);
        chk(7, s.state() == ProtoState::Done && v.out == want, "byte-wise feed/drain");
    }
    // 8 乱码前导容忍（首踢前 6 字节垃圾）
    {
        auto d = MakeData(200, 8);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f8.bin");
        VPeer v(ProtoKind::Xmodem); v.garbagePrefix = 6;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(8, s.state() == ProtoState::Done && v.out.size() == 1024,
            "garbage prefix tolerated");
    }
    // 9 XMODEM EOT 被 NAK 两次 → 第三次 ACK 收尾（≤3 重发口径）
    {
        auto d = MakeData(64, 9);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f9.bin");
        VPeer v(ProtoKind::Xmodem); v.eotNakFirst = 2;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(9, s.state() == ProtoState::Done && v.eotSeen == 3, "xmodem eot nak-nak-ack");
    }
    // 10 用户取消 → tx=CAN CAN BS BS + Aborted
    {
        auto d = MakeData(9000, 10);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "f10.bin");
        VPeer v(ProtoKind::Xmodem);
        Drive dr; dr.s = &s; dr.v = &v;
        v.Kick();
        for (int it = 0; it < 9000 && s.state() == ProtoState::Running; ++it) {
            if (!s.tx().empty()) { std::vector<uint8_t> c(s.tx()); v.OnTx(c); s.Consume(c.size()); }
            if (!v.txq.empty()) { std::vector<uint8_t> r; r.swap(v.txq); s.Feed(r.data(), r.size()); }
            if (s.progress().confirmed > 2000) { s.Cancel(); break; }
        }
        chk(10, s.state() == ProtoState::Aborted && s.tx().size() == 4 &&
                s.tx()[0] == 0x18 && s.tx()[1] == 0x18 && s.tx()[2] == 0x08 && s.tx()[3] == 0x08,
            "user cancel canx2 bsx2");
    }
    // 11 CRC-16/XMODEM 已知向量："123456789" → 0x31C3
    {
        unsigned crc = 0;
        const char* p = "123456789";
        for (int i = 0; i < 9; ++i) {
            crc ^= (unsigned)(uint8_t)p[i] << 8;
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
            crc &= 0xFFFF;
        }
        chk(11, crc == 0x31C3, "crc16/xmodem known vector 0x31C3");
    }
    // 12 YMODEM 正常批（块 0 → 第二踢 → 1K 数据 → EOT 双确认 → 空块 0 收批）
    {
        auto d = MakeData(2500, 12);                 // 内容含 0x1A 值（按长度截断的边界）
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "fw.bin");
        VPeer v(ProtoKind::Ymodem);
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(12, s.state() == ProtoState::Done && v.out == d && v.name == "fw.bin" &&
                v.size == d.size(), "ymodem full batch");
    }
    // 13 YMODEM 块 0 被拒一次 → 重发后继续
    {
        auto d = MakeData(300, 13);
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "a.bin");
        VPeer v(ProtoKind::Ymodem); v.corruptOnce = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(13, s.state() == ProtoState::Done && v.out == d && v.name == "a.bin" &&
                v.naks == 1, "ymodem hdr nak retry");
    }
    // 14 YMODEM 空文件（长度 0）→ 块 0 + EOT 双确认 + 空块 0 收批
    {
        ProtoSender s; s.Start(ProtoKind::Ymodem, std::vector<uint8_t>(), "empty.bin");
        VPeer v(ProtoKind::Ymodem);
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(14, s.state() == ProtoState::Done && v.size == 0 && v.out.empty() &&
                v.name == "empty.bin", "ymodem empty file");
    }
    // 15 YMODEM 无第二 'C' → 卡「等待接收就绪」不限时（口径：等待、界面可取消）
    {
        auto d = MakeData(100, 15);
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "w.bin");
        VPeer v(ProtoKind::Ymodem); v.noSecondC = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        for (int i = 0; i < 50; ++i) s.OnTimeout();  // 等踢相位 OnTimeout 必须是 no-op
        chk(15, s.state() == ProtoState::Running, "ymodem wait-second-c indefinite");
    }
    // 16 YMODEM 首 EOT 直接 ACK 的宽松收端 → 跳过双确认等 'C' 收批
    {
        auto d = MakeData(150, 16);
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "l.bin");
        VPeer v(ProtoKind::Ymodem); v.eotAckFirst = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(16, s.state() == ProtoState::Done && v.out == d, "ymodem lenient eot-ack");
    }
    // 17 YMODEM 用户取消 → CAN CAN BS BS
    {
        auto d = MakeData(4000, 17);
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "c.bin");
        VPeer v(ProtoKind::Ymodem);
        Drive dr; dr.s = &s; dr.v = &v;
        v.Kick();
        for (int it = 0; it < 9000 && s.state() == ProtoState::Running; ++it) {
            if (!s.tx().empty()) { std::vector<uint8_t> c(s.tx()); v.OnTx(c); s.Consume(c.size()); }
            if (!v.txq.empty()) { std::vector<uint8_t> r; r.swap(v.txq); s.Feed(r.data(), r.size()); }
            if (s.progress().confirmed > 1000) { s.Cancel(); break; }
        }
        chk(17, s.state() == ProtoState::Aborted && s.tx().size() == 4 &&
                s.tx()[0] == 0x18 && s.tx()[1] == 0x18 && s.tx()[2] == 0x08 && s.tx()[3] == 0x08,
            "ymodem user cancel");
    }
    // 18 ACK 丢失 → 3s 超时重发同块 → 收端按重复块补 ACK
    {
        auto d = MakeData(1200, 18);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "t.bin");
        VPeer v(ProtoKind::Xmodem); v.dropKick = true;      // 首踢手动喂，避开 kick 被丢
        Drive dr; dr.s = &s; dr.v = &v;
        uint8_t c = 0x43;
        s.Feed(&c, 1);
        bool dropped = false;
        for (long it = 0; it < 200000 && s.state() == ProtoState::Running; ++it) {
            if (!s.tx().empty()) { std::vector<uint8_t> ch(s.tx()); v.OnTx(ch); s.Consume(ch.size()); }
            if (!v.txq.empty()) {
                if (!dropped) {                    // 丢弃首个 ACK（模拟丢包）
                    dropped = true;
                    v.txq.clear();
                    s.OnTimeout();                 // 3s 到点 → 重发块 1
                } else {
                    std::vector<uint8_t> r; r.swap(v.txq); s.Feed(r.data(), r.size());
                }
            }
        }
        std::vector<uint8_t> want = d;
        while (want.size() % 1024) want.push_back(0x1A);
        chk(18, s.state() == ProtoState::Done && v.out == want && v.dupAcks >= 1,
            "ack-lost resend same-block");
    }
    // 19 XMODEM 大文件跨块号回绕（301 块 > 255，线序号 mod 256；含线序 0 的数据块）
    {
        auto d = MakeData(300 * 1024 + 5, 19);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "big.bin");
        VPeer v(ProtoKind::Xmodem);
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 1024) want.push_back(0x1A);
        chk(19, s.state() == ProtoState::Done && v.out == want, "block-number wrap >255");
    }
    // 20 YMODEM 块 0 布局黑盒：SOH 00 FF + 名\0 长度\0 + 0x00 补齐（133B 线序）
    {
        ProtoSender s; s.Start(ProtoKind::Ymodem, MakeData(1234, 20), "X y.Z");
        uint8_t c = 0x43;
        s.Feed(&c, 1);                               // 'C' → 块 0 入队
        auto& tx = s.tx();
        bool ok = tx.size() == 133 && tx[0] == 0x01 && tx[1] == 0x00 && tx[2] == 0xFF;
        std::string h((const char*)&tx[3], 128);
        size_t z1 = h.find('\0');
        ok = ok && z1 != std::string::npos && h.substr(0, z1) == "X y.Z";
        size_t z2 = z1 == std::string::npos ? std::string::npos : h.find('\0', z1 + 1);
        ok = ok && z2 != std::string::npos && h.substr(z1 + 1, z2 - z1 - 1) == "1234";
        bool tailZero = true;
        for (size_t i = z2 + 1; i < 128; ++i) if (h[i]) { tailZero = false; break; }
        ok = ok && tailZero;
        chk(20, ok, "ymodem blk0 name/size layout");
    }

    // 21 XMODEM 固定 CRC（续修3）：对端 NAK 首踢也不降级——保持 CRC16+1024B
    {
        auto d = MakeData(300, 21);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "fc.bin", XCheck::Crc16);
        VPeer v(ProtoKind::Xmodem); v.checksumKick = true;   // 对端却发 NAK 首踢
        Drive dr; dr.s = &s; dr.v = &v;
        v.Kick();
        // 收端 checksumKick 会按和校验解析 → CRC 块必被 NAK → 打转；此处仅验证**发端档位**：
        // 首踢后检查 tx 首帧 = STX(1K) + CRC 两字节尾部
        uint8_t nak = 0x15;
        s.Feed(&nak, 1);
        auto& tx = s.tx();
        bool ok = s.state() == ProtoState::Running && tx.size() == 3 + 1024 + 2 &&
                  tx[0] == 0x02;                       // STX=1K 档 + CRC16 尾
        chk(21, ok, "xmodem fixed-crc keeps 1K on nak-kick");
    }
    // 22 XMODEM 固定校验和（续修3）：对端 'C' 首踢也不升级——128B+1B 和
    {
        auto d = MakeData(300, 22);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "fs.bin", XCheck::Checksum);
        uint8_t c = 0x43;
        s.Feed(&c, 1);                                 // 'C' 首踢：固定模式仅放行
        auto& tx = s.tx();
        bool ok = s.state() == ProtoState::Running && tx.size() == 3 + 128 + 1 &&
                  tx[0] == 0x01;                       // SOH=128B 档 + 1B 校验和尾
        chk(22, ok, "xmodem fixed-checksum stays 128B on c-kick");
    }
    // 23 XMODEM 固定校验和端到端：NAK 首踢 + 和校验收端 → Done
    {
        auto d = MakeData(500, 23);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "fe.bin", XCheck::Checksum);
        VPeer v(ProtoKind::Xmodem); v.checksumKick = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 128) want.push_back(0x1A);
        chk(23, s.state() == ProtoState::Done && v.out == want,
            "xmodem fixed-checksum e2e");
    }

    // 24 XMODEM 固定块长 128（续修4）：对端 'C' 首踢也不升级——128B 档保持
    {
        auto d = MakeData(300, 24);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "b1.bin", XCheck::Auto, XBlk::Blk128);
        uint8_t c = 0x43;
        s.Feed(&c, 1);                                 // 'C' 首踢：块档保持用户选择
        auto& tx = s.tx();
        chk(24, s.state() == ProtoState::Running && tx.size() == 3 + 128 + 2 &&
                tx[0] == 0x01, "fixed blk128 stays 128 on c-kick");
    }
    // 25 XMODEM 固定块长 1024 + 校验自动（续修4）：NAK 首踢 → 校验和降级但**块长保持 1024**
    //（=SOH+1024+1B 和；想要「CRC16+1024 不降级」选 校验=CRC——案 21 覆盖）
    {
        auto d = MakeData(300, 25);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "b2.bin", XCheck::Auto, XBlk::Blk1k);
        uint8_t nak = 0x15;
        s.Feed(&nak, 1);
        auto& tx = s.tx();
        chk(25, s.state() == ProtoState::Running && tx.size() == 3 + 1024 + 1 &&
                tx[0] == 0x02, "auto-check+blk1k: sum downgraded, STX 1K frame kept");
    }
    // 26 XMODEM 128 块 + 固定校验和端到端（续修4 组合）
    {
        auto d = MakeData(260, 26);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "b3.bin", XCheck::Checksum, XBlk::Blk128);
        VPeer v(ProtoKind::Xmodem); v.checksumKick = true;
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        std::vector<uint8_t> want = d;
        while (want.size() % 128) want.push_back(0x1A);
        chk(26, s.state() == ProtoState::Done && v.out == want,
            "xmodem blk128+checksum e2e");
    }

    // 27 YMODEM 固定块长 128（续修5：包大小对 YMODEM 生效）——数据块 SOH+128，块 0 仍 SOH+128
    {
        auto d = MakeData(400, 27);
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, "y128.bin", XCheck::Auto, XBlk::Blk128);
        VPeer v(ProtoKind::Ymodem);
        Drive dr; dr.s = &s; dr.v = &v; dr.run();
        chk(27, s.state() == ProtoState::Done && v.out == d && v.name == "y128.bin" &&
                v.size == d.size(), "ymodem blk128 data blocks");
    }

    // 28 R23-P2-1 长名块 0：127B 名（原实现 1B 越界点）——名截断预留、长度域完整、无越界
    {
        auto d = MakeData(3000, 28);
        std::string nm(127, 'n');
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, nm);
        uint8_t c = 0x43;
        s.Feed(&c, 1);                               // 'C' → 块 0 入队
        auto& tx = s.tx();
        size_t nameMax = 128 - 1 - (4 + 1);          // 长度域 "3000" 4 位 → 名可用 122B
        bool ok = tx.size() == 133 && tx[0] == 0x01 && tx[1] == 0x00 && tx[2] == 0xFF;
        std::string h((const char*)&tx[3], 128);
        ok = ok && h.compare(0, nameMax, nm, 0, nameMax) == 0;   // 名前 122B 原样
        ok = ok && h[nameMax] == 0;                  // 名终止符（名占满时恰落 [122]）
        ok = ok && h.compare(nameMax + 1, 4, "3000") == 0;       // 长度域完整（原实现被挤没）
        ok = ok && h[nameMax + 5] == 0;              // 长度域终止符
        bool tailZero = true;
        for (size_t i = nameMax + 6; i < 128; ++i) if (h[i]) { tailZero = false; break; }
        ok = ok && tailZero;
        chk(28, ok, "R23 127B name: size field reserved, no OOB");
    }
    // 29 R23-P2-1 越长名 130B（>127 原越界点）——同断言
    {
        auto d = MakeData(3000, 29);
        std::string nm(130, 'm');
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, nm);
        uint8_t c = 0x43;
        s.Feed(&c, 1);
        auto& tx = s.tx();
        size_t nameMax = 128 - 1 - (4 + 1);
        bool ok = tx.size() == 133 && tx[0] == 0x01;
        std::string h((const char*)&tx[3], 128);
        ok = ok && h.compare(0, nameMax, nm, 0, nameMax) == 0;
        ok = ok && h[nameMax] == 0 && h.compare(nameMax + 1, 4, "3000") == 0 &&
                h[nameMax + 5] == 0;
        chk(29, ok, "R23 130B name: layout intact");
    }
    // 30 R23-P2-1 中文长名（42 汉字 UTF-8 = 126B，用户可达）——长度域完整
    {
        auto d = MakeData(3000, 30);
        std::string nm;                              // "测" U+6D4B → E6 B5 8B
        for (int i = 0; i < 42; ++i) { nm += (char)0xE6; nm += (char)0xB5; nm += (char)0x8B; }
        ProtoSender s; s.Start(ProtoKind::Ymodem, d, nm);
        uint8_t c = 0x43;
        s.Feed(&c, 1);
        auto& tx = s.tx();
        size_t nameMax = 128 - 1 - (4 + 1);
        bool ok = tx.size() == 133;
        std::string h((const char*)&tx[3], 128);
        ok = ok && h.compare(0, nameMax, nm, 0, nameMax) == 0;   // 126B 名截到 122
        ok = ok && h[nameMax] == 0 && h.compare(nameMax + 1, 4, "3000") == 0 &&
                h[nameMax + 5] == 0;
        chk(30, ok, "R23 CJK 126B UTF-8 name: size field intact");
    }
    // 31 R23-P3-5 CAN CAN 跨 Feed 分片：两个 CAN 劈进两次 Feed（串口分片读）也中止
    {
        auto d = MakeData(100, 31);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "cf.bin");
        uint8_t can = 0x18;
        s.Feed(&can, 1);                             // 孤立 CAN：丢弃 + 粘住
        bool ok = s.state() == ProtoState::Running;
        s.Feed(&can, 1);                             // 第二拍 CAN → 中止
        chk(31, ok && s.state() == ProtoState::Aborted && s.error() == L"对端取消",
            "CAN CAN split across two feeds aborts");
    }

    // 32 §131 续：maxRetries 参数生效（=2）——首发 + 2 次重发后第 3 次超限 Error
    {
        auto d = MakeData(128, 32);
        ProtoSender s; s.Start(ProtoKind::Xmodem, d, "r2.bin", XCheck::Auto, XBlk::Auto, 2);
        uint8_t nak = 0x15;
        s.Feed(&nak, 1);                             // NAK 首踢 → 校验和+128 首发
        bool ok = s.state() == ProtoState::Running && s.tx().size() == 3 + 128 + 1;
        s.Feed(&nak, 1);                             // retries=1 → 重发
        ok = ok && s.state() == ProtoState::Running;
        s.Feed(&nak, 1);                             // retries=2 → 重发
        ok = ok && s.state() == ProtoState::Running;
        s.Feed(&nak, 1);                             // retries=3 > 2 → Fail
        chk(32, ok && s.state() == ProtoState::Error && s.error() == L"重发超限",
            "maxRetries=2 exhausts after 2 resends");
    }

    fprintf(gFo, "proto SUMMARY total=%d pass=%d\n", gTotal, gPass);
    fclose(gFo);
    printf("protofileself: %d/%d\n", gPass, gTotal);
    return gPass == gTotal ? 0 : 1;
}
