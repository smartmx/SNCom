// sncom/src/core/rxbuf.h —— 接收环形字节缓冲（← PyCom rxbuf.py：上限溢出丢最旧，绝对偏移读取）
// §117（U3 还债）：**真环形**（头指针 head_ + 有效长度 len_），append/读/丢最旧都是
// O(本次数据) 且**不做整体搬移**。旧实现是 vector 的 insert 尾加 + erase 头裁：缓冲一旦
// 到达上限，每一块新数据都要把 ~cap 字节 memmove 一次（10MB 档 × 64KB 块 ≈ 160× 写放大，
// 50Mbps 长跑即是这笔债）。对外 API 与语义（含绝对偏移、dropped/total_rx、越界截断）保持不变。
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

namespace sncom {

class RxBuffer {
public:
    explicit RxBuffer(size_t cap = 10 * 1024 * 1024) : cap_(cap < 1024 ? 1024 : cap) {}

    void set_cap(size_t cap) {
        cap_ = cap < 1024 ? 1024 : cap;
        Trim();                                     // 收小档位 → 丢最旧（与旧实现同语义）
    }
    size_t cap() const { return cap_; }
    size_t size() const { return len_; }
    uint64_t start_abs() const { return base_; }
    uint64_t end_abs() const { return base_ + len_; }
    uint64_t total_rx = 0;            // 累计接收字节数（不清屏）
    uint64_t dropped = 0;             // 溢出丢弃的字节数

    void append(const uint8_t* d, size_t n) {
        if (!n) return;
        total_rx += n;
        if (n >= cap_) {                            // 单块即超整档：只留最后 cap_ 字节
            const size_t skip = n - cap_;
            base_ += len_ + skip;
            dropped += len_ + skip;
            len_ = 0;
            EnsureMem(cap_);
            std::memcpy(mem_.data(), d + skip, cap_);
            head_ = 0;
            len_ = cap_;
            return;
        }
        if (len_ + n > cap_) DropOldest(len_ + n - cap_);
        EnsureMem(len_ + n);
        const size_t w = head_ + len_ < mem_.size() ? head_ + len_ : head_ + len_ - mem_.size();
        size_t first = mem_.size() - w;
        if (first > n) first = n;
        std::memcpy(mem_.data() + w, d, first);
        if (n > first) std::memcpy(mem_.data(), d + first, n - first);   // 环绕段
        len_ += n;
    }
    void clear() { head_ = 0; len_ = 0; base_ = 0; }   // 显示层随之清空，偏移重置（保留已分配内存）

    // 按绝对偏移读（越界自动截断）
    std::vector<uint8_t> read(uint64_t start, size_t n) const {
        std::vector<uint8_t> out;
        uint64_t lo = start > base_ ? start : base_;
        uint64_t hi = start + n < end_abs() ? start + n : end_abs();
        if (hi > lo) {
            const size_t k = (size_t)(hi - lo);
            out.resize(k);
            CopyFrom((size_t)(lo - base_), out.data(), k);
        }
        return out;
    }
    std::vector<uint8_t> tail(size_t n) const {
        size_t k = n < len_ ? n : len_;
        std::vector<uint8_t> out(k);
        if (k) CopyFrom(len_ - k, out.data(), k);
        return out;
    }

private:
    // 丢最旧 k 字节（k ≤ len_）
    void DropOldest(size_t k) {
        if (!k) return;
        head_ = head_ + k < mem_.size() ? head_ + k : head_ + k - mem_.size();
        len_ -= k;
        base_ += k;
        dropped += k;
    }
    void Trim() {
        if (len_ > cap_) DropOldest(len_ - cap_);
    }
    // 保证可放 need 字节（need ≤ cap_）；扩容时把环内数据线性化到新内存
    void EnsureMem(size_t need) {
        if (need <= mem_.size()) return;
        size_t m = mem_.empty() ? 1024 : mem_.size();
        while (m < need) m = m < cap_ / 2 ? m * 2 : cap_;
        std::vector<uint8_t> nm(m);
        if (len_) {
            size_t first = mem_.size() - head_;
            if (first > len_) first = len_;
            std::memcpy(nm.data(), mem_.data() + head_, first);
            if (len_ > first) std::memcpy(nm.data() + first, mem_.data(), len_ - first);
        }
        mem_.swap(nm);
        head_ = 0;
    }
    // 从环内偏移 off 起拷 k 字节（off/k 已按 len_ 夹取，两段拼）
    void CopyFrom(size_t off, uint8_t* dst, size_t k) const {
        size_t p = head_ + off;
        if (p >= mem_.size()) p -= mem_.size();
        size_t first = mem_.size() - p;
        if (first > k) first = k;
        std::memcpy(dst, mem_.data() + p, first);
        if (k > first) std::memcpy(dst + first, mem_.data(), k - first);
    }

    std::vector<uint8_t> mem_;        // 已分配内存（≥len_，≤cap_；首次使用才分配）
    size_t head_ = 0;                 // 最旧字节在 mem_ 中的下标
    size_t len_ = 0;                  // 有效字节数
    uint64_t base_ = 0;               // 绝对偏移（buf_[0] 对应）
    size_t cap_;
};

} // namespace sncom
