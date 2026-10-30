// sncom/src/core/encoding.cpp —— M0.5 spike + M2 编码状态机
#include "encoding.h"
#include "enc_tables.h"
#include "gb18030_tables.h"
#include "gbk_table.h"
#include <windows.h>
#include <cwchar>
#include <cstdlib>

namespace sncom {

int CodePageOf(const std::wstring& name) {
    if (name == L"gbk" || name == L"gb2312" || name == L"cp936") return 936;
    if (name == L"gb18030") return 54936;
    if (name == L"utf-8" || name == L"utf8") return 65001;
    if (name == L"utf-16-le") return 1200;
    if (name == L"utf-16-be") return 1201;
    if (name == L"big5" || name == L"big5hkscs") return 950;
    if (name == L"shift_jis" || name == L"shift-jis" || name == L"cp932") return 932;
    if (name == L"euc_jp" || name == L"euc-jp") return 20932;
    if (name == L"euc_kr" || name == L"euc-kr") return 949;
    if (name == L"cp1252" || name == L"windows-1252") return 1252;
    if (name == L"latin-1" || name == L"latin1" || name == L"iso-8859-1") return 28591;
    // 任意手输：纯数字当代码页（对齐 PyCom「12 预置 + 任意手输」）
    if (!name.empty()) {
        bool digits = true;
        for (wchar_t c : name) if (c < L'0' || c > L'9') { digits = false; break; }
        if (digits) {
            int cp = _wtoi(name.c_str());
            if (cp > 0 && cp <= 65535) return cp;
        }
    }
    return 28591;                       // 未知 → Latin-1 字节直通（不丢字节）
}

static bool W2B(const std::wstring& s, int cp, std::vector<uint8_t>& out) {
    if (cp == 1200) {                   // utf-16-le
        out.resize(s.size() * 2);
        if (!s.empty()) memcpy(out.data(), s.data(), out.size());
        return true;
    }
    if (cp == 1201) {                   // utf-16-be
        out.resize(s.size() * 2);
        for (size_t i = 0; i < s.size(); ++i) {
            out[i * 2]     = (uint8_t)(s[i] >> 8);
            out[i * 2 + 1] = (uint8_t)(s[i] & 0xFF);
        }
        return true;
    }
    int len = WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    out.resize((size_t)len);
    WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), (char*)out.data(), len, nullptr, nullptr);
    return true;
}

bool WideToBytes(const std::wstring& s, const std::wstring& enc, std::vector<uint8_t>& out) {
    out.clear();
    return W2B(s, CodePageOf(enc), out);
}

std::wstring BytesToWide(const std::vector<uint8_t>& b, const std::wstring& enc) {
    // 一次性解码走增量解码器全表实现——不用 MultiByteToWideChar：gb18030(54936)
    // 在 Win7 的系统转换表不全（docs/00 §30 建表的动因）。
    IncrementalDecoder dec(enc);
    std::wstring out = dec.Feed(b.data(), b.size());
    out += dec.FlushFinal();
    return out;
}

// §77：文本 + 转义序列 → 字节（SSCOM V5.04/V5.11a：\x?? \\ \r \n \t \v \a \b \f \0）。
// 设计要点：转义字节不经编码转换直落（\xBD = 单字节 0xBD），文本段才走 WideToBytes(enc)；
// 未知序列原样保留（反斜杠与后续字符按普通文本编码）。
bool TextWithEscapesToBytes(const std::wstring& s, const std::wstring& enc, std::vector<uint8_t>& out) {
    auto hexVal = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    std::wstring run;
    auto flushRun = [&]() -> bool {
        if (run.empty()) return true;
        std::vector<uint8_t> b;
        if (!WideToBytes(run, enc, b)) return false;
        out.insert(out.end(), b.begin(), b.end());
        run.clear();
        return true;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c != L'\\' || i + 1 >= s.size()) { run.push_back(c); continue; }
        wchar_t n = s[i + 1];
        int byte = -1;
        size_t adv = 2;                                 // 默认消耗 \X 两字符
        switch (n) {
        case L'\\': byte = 0x5C; break;
        case L'r':  byte = 0x0D; break;
        case L'n':  byte = 0x0A; break;
        case L't':  byte = 0x09; break;
        case L'v':  byte = 0x0B; break;
        case L'a':  byte = 0x07; break;
        case L'b':  byte = 0x08; break;
        case L'f':  byte = 0x0C; break;
        case L'0':  byte = 0x00; break;
        case L'x': case L'X': {
            if (i + 3 < s.size()) {
                int hi = hexVal(s[i + 2]), lo = hexVal(s[i + 3]);
                if (hi >= 0 && lo >= 0) { byte = hi * 16 + lo; adv = 4; }
            }
            break;
        }
        default: break;
        }
        if (byte < 0) { run.push_back(c); continue; }   // 未知序列：反斜杠按普通字符
        if (!flushRun()) return false;
        out.push_back((uint8_t)byte);
        i += adv - 1;
    }
    return flushRun();
}

IncrementalDecoder::IncrementalDecoder(const std::wstring& enc) : enc_(enc) {}

void IncrementalDecoder::Reset() { tail_.clear(); }

std::wstring IncrementalDecoder::FlushFinal() {
    // Python final=True errors="replace"：无法完工的半截序列落 U+FFFD。
    // gb18030 口径（docs/00 §30 源码直译 _codecs_cn.c + multibytecodec.c）：
    // TOOFEW(final) = esize = 剩余全部 → **整体 1 个 FFFD**（其余编码逐字节 FFFD）
    if (CodePageOf(enc_) == 54936) {
        std::wstring out;
        if (!tail_.empty()) out.push_back(0xFFFD);
        tail_.clear();
        return out;
    }
    std::wstring out(tail_.size(), 0xFFFD);
    tail_.clear();
    return out;
}

std::wstring IncrementalDecoder::Feed(const uint8_t* d, size_t n) {
    int cp = CodePageOf(enc_);
    // gb18030（54936）：源码直译 CPython _codecs_cn.c gb18030_decode + multibytecodec.c
    // 框架（docs/00 §30，H-3 全面收口）。双字节 = 枚举表（含三特例/gb2312/gbkext/gb18030ext
    // 全部映射）；四字节 = 线性段表 + 代理对输出。错误语义 = 框架 `return e`：
    //   FFFD 吞 lead 一个字节，第二/三字节重新参与解析（L2 非法 b3 与 L3 非法 b4 同款——
    //   差异只在缓冲深度）；TOOFEW（序列不全）→ tail_ 留存，FlushFinal 整体 1 FFFD。
    if (cp == 54936) {
        std::vector<uint8_t> buf(tail_);
        buf.insert(buf.end(), d, d + n);
        tail_.clear();
        std::wstring out;
        size_t i = 0;
        const size_t N = buf.size();
        auto g30Lookup = [](uint16_t key) -> uint16_t {
            int lo = 0, hi = (int)(sizeof(kG30Table) / sizeof(kG30Table[0])) - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (kG30Table[mid].key == key) return kG30Table[mid].cp;
                if (kG30Table[mid].key < key) lo = mid + 1; else hi = mid - 1;
            }
            return 0;
        };
        auto g30Seg = [](uint32_t order, uint32_t& cpOut) -> bool {
            int lo = 0, hi = kG30SegCount - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                const G30Seg& s = kG30Segs[mid];
                if (order < s.startOrder) hi = mid - 1;
                else if (order >= s.startOrder + s.len) lo = mid + 1;
                else { cpOut = s.startCp + (order - s.startOrder); return true; }
            }
            return false;
        };
        while (i < N) {
            uint8_t c = buf[i];
            if (c < 0x80) { out.push_back((wchar_t)c); ++i; continue; }   // ASCII
            if (c == 0x80 || c == 0xFF) { out.push_back(0xFFFD); ++i; continue; }
            if (N - i < 2) { tail_.assign(buf.begin() + i, buf.end()); break; }   // TOOFEW
            uint8_t c2 = buf[i + 1];
            if (c2 >= 0x30 && c2 <= 0x39) {                               // 四字节路径
                if (N - i < 4) { tail_.assign(buf.begin() + i, buf.end()); break; }
                uint8_t c3 = buf[i + 2], c4 = buf[i + 3];
                if (c > 0xFE || c3 < 0x81 || c3 > 0xFE || c4 < 0x30 || c4 > 0x39) {
                    out.push_back(0xFFFD); ++i; continue;   // return 1：吞 lead，c2 重解析
                }
                uint32_t order = ((uint32_t)(c - 0x81) * 10 + (c2 - 0x30)) * 1260
                               + (uint32_t)(c3 - 0x81) * 10 + (c4 - 0x30);
                uint32_t cpv = 0;
                if (!g30Seg(order, cpv)) { out.push_back(0xFFFD); ++i; continue; }  // 洞：同款
                if (cpv <= 0xFFFF) {
                    out.push_back((wchar_t)cpv);
                } else {                                              // 增补平面 → UTF-16 代理对
                    uint32_t v = cpv - 0x10000;
                    out.push_back((wchar_t)(0xD800 + (v >> 10)));
                    out.push_back((wchar_t)(0xDC00 + (v & 0x3FF)));
                }
                i += 4;
                continue;
            }
            if (c2 >= 0x40 && c2 <= 0xFE && c2 != 0x7F) {                 // 双字节（GBK trail 区）
                uint16_t mapped = g30Lookup((uint16_t)((c << 8) | c2));
                if (mapped) { out.push_back((wchar_t)mapped); i += 2; continue; }
            }
            out.push_back(0xFFFD); ++i;                                   // return 1：吞 lead，c2 重解析
        }
        return out;
    }
    // GBK 族（936）：逐字节状态机 + CPython gbk 映射表（gbk_table.h，二分查找）——
    if (cp == 936) {
        std::vector<uint8_t> buf(tail_);
        buf.insert(buf.end(), d, d + n);
        tail_.clear();
        std::wstring out;
        size_t i = 0;
        const size_t N = buf.size();
        while (i < N) {
            uint8_t c = buf[i];
            if (c <= 0x7F) { out.push_back((wchar_t)c); ++i; continue; }
            if (c == 0x80 || c == 0xFF) { out.push_back(0xFFFD); ++i; continue; }
            if (i + 1 >= N) { tail_.push_back(c); break; }      // 悬空 lead → 下一包
            uint16_t mapped = GbkLookup(c, buf[i + 1]);
            if (mapped) { out.push_back((wchar_t)mapped); i += 2; }
            else { out.push_back(0xFFFD); ++i; }                // trail 重处理（CPython 同）
        }
        return out;
    }
    // big5 / shift_jis：逐字节状态机 + CPython 映射表（enc_tables.h，G3 ⑤ 编码对拍）——
    // Windows CP950/932 与 CPython 存在少量码点分歧（big5 A145、cp932 8160 波浪号等），
    // 查表后逐码点一致；口径同 GBK：lead 无效 → U+FFFD 前进 1；对未命中 → U+FFFD 且
    // trail 重新参与解析；悬空 lead 留尾巴。
    if (const EncTbl* t = EncTblOf(enc_)) {
        std::vector<uint8_t> buf(tail_);
        buf.insert(buf.end(), d, d + n);
        tail_.clear();
        std::wstring out;
        size_t i = 0;
        const size_t N = buf.size();
        while (i < N) {
            uint8_t c = buf[i];
            if (c <= 0x7F) { out.push_back((wchar_t)c); ++i; continue; }   // ASCII
            if (t->singleLo && c >= t->singleLo && c <= t->singleHi) {
                out.push_back((wchar_t)(c + t->singleOff)); ++i; continue;     // sjis 半角片假名 → U+FF61-FF9D
            }
            if (!(t->leads[c >> 3] & (1 << (c & 7)))) {
                out.push_back(0xFFFD); ++i; continue;          // 非法单字节（CPython 同）
            }
            // euc_jp 0x8F 三字节罕用字：不足留尾巴；查 ext3 失败 → FFFD 且第二字节重解析
            // （CPython 探测：0x8F+2 坏字节 = 3×FFFD，即 trail 逐字节重解析，docs/00 §25）
            if (t->ext3 && c == 0x8F) {
                if (i + 2 >= N) {
                    tail_.assign(buf.begin() + i, buf.end());
                    break;
                }
                uint16_t m3 = EncTblLookup(t->ext3, t->ext3Count,
                                           (uint16_t)(((buf[i + 1] - 0x80) << 8) | buf[i + 2]));
                if (m3) { out.push_back((wchar_t)m3); i += 3; }
                else { out.push_back(0xFFFD); ++i; }           // 第 2 字节重新参与解析
                continue;
            }
            if (i + 1 >= N) { tail_.push_back(c); break; }     // 悬空 lead → 下一包
            uint16_t mapped = EncTblLookup(t->data, t->count,
                                           (uint16_t)(((c - 0x80) << 8) | buf[i + 1]));
            if (mapped) { out.push_back((wchar_t)mapped); i += 2; }
            else { out.push_back(0xFFFD); ++i; }               // trail 重处理（CPython 同）
        }
        return out;
    }
    // cp1252（单字节表，H-3）：Python undefined 字节（0x81/8D/8F/90/9D）→ FFFD，其余查表
    if (cp == 1252) {
        std::vector<uint8_t> buf(tail_);
        buf.insert(buf.end(), d, d + n);
        tail_.clear();
        std::wstring out;
        for (uint8_t c : buf) {
            uint16_t m = kCp1252Map[c];
            out.push_back(m == 0xFFFF ? (wchar_t)0xFFFD : (wchar_t)m);   // 哨兵 = Python undefined
        }
        return out;
    }
    // utf-16：字节流按 2 对齐，奇数字节留尾巴
    if (cp == 1200 || cp == 1201) {
        std::wstring out;
        size_t total = tail_.size() + n;
        size_t even = total - (total % 2);
        std::vector<uint8_t> buf;
        buf.reserve(even);
        buf.insert(buf.end(), tail_.begin(), tail_.end());
        buf.insert(buf.end(), d, d + n);
        for (size_t i = 0; i < even; i += 2) {
            wchar_t ch = (cp == 1200)
                ? (wchar_t)(buf[i] | (buf[i + 1] << 8))
                : (wchar_t)((buf[i] << 8) | buf[i + 1]);
            out.push_back(ch);
        }
        tail_.assign(buf.begin() + even, buf.end());
        return out;
    }
    // 多字节：整块尝试（严格校验），失败则把末尾 ≤4 字节留给下一块
    std::vector<uint8_t> buf(tail_);
    buf.insert(buf.end(), d, d + n);
    tail_.clear();
    if (buf.empty()) return std::wstring();

    int wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS,
                                   (const char*)buf.data(), (int)buf.size(), nullptr, 0);
    if (wlen > 0) {
        std::wstring out;
        out.resize((size_t)wlen);
        MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (const char*)buf.data(), (int)buf.size(),
                            &out[0], wlen);
        return out;
    }
    // 失败：先假定末尾是多字节序列前缀——从尾部回退 1..min(4,len) 找最长可解码前缀
    size_t keepMax = buf.size() < 4 ? buf.size() : 4;
    for (size_t keep = keepMax; keep > 0; --keep) {
        size_t head = buf.size() - keep;
        if (head == 0) break;
        int hl = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS,
                                     (const char*)buf.data(), (int)head, nullptr, 0);
        if (hl > 0) {
            std::wstring out;
            out.resize((size_t)hl);
            MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (const char*)buf.data(), (int)head,
                                &out[0], hl);
            tail_.assign(buf.end() - keep, buf.end());
            return out;
        }
    }
    // 整块非法（编码切错/乱码）：Latin-1 直通本块，不吞尾巴（避免死锁）
    std::wstring out;
    out.reserve(buf.size());
    for (uint8_t b : buf) out.push_back((wchar_t)b);
    return out;
}

} // namespace sncom
