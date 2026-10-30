// sncom/src/core/encoding.h —— M0.5 spike：编码名 → Windows 代码页 + 增量解码（跨包半截序列）
// 对应 PyCom 的 Python codecs 增量解码器（final=False）；完整 12 编码状态机在 M2 补齐。
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace sncom {

// 编码名（与 PyCom ENCODINGS 取值一致）→ 代码页；未知名字回退 28591（Latin-1，字节直通）
int CodePageOf(const std::wstring& name);

// 宽字符 → 字节（utf-16-le/be 特判直拷；其余走代码页）。返回 false = 转换失败。
bool WideToBytes(const std::wstring& s, const std::wstring& enc, std::vector<uint8_t>& out);

// 字节 → 宽字符一次性解码（走增量解码器全表实现；§54 发送框右键「hex → 编码字符」用。
// 不直接用 MultiByteToWideChar：gb18030(54936) 在 Win7 的系统表不全）。
std::wstring BytesToWide(const std::vector<uint8_t>& b, const std::wstring& enc);

// §77（SSCOM V5.04/V5.11a 语义）：文本 + 转义序列 → 字节。
// 支持 \x??（两位十六进制→单字节）、\\、\r \n \t \v \a \b \f \0；
// 未知序列原样保留（反斜杠按普通字符编码）。转义产生的字节**不经编码转换**直接落字节
// （\xBD 就是 0xBD 单字节，而非 GBK 双字节字符），文本段才走 WideToBytes(enc)。
// 返回 false = 文本段编码转换失败。
bool TextWithEscapesToBytes(const std::wstring& s, const std::wstring& enc, std::vector<uint8_t>& out);

// 增量解码器：喂任意长度字节块，返回可解码文本；不可解码的尾部字节（半截多字节序列）
// 留在内部等下一块——对应 PyCom 的 final=False。切编码 = 换新实例（已显示内容不重算）。
class IncrementalDecoder {
public:
    explicit IncrementalDecoder(const std::wstring& enc);
    std::wstring Feed(const uint8_t* d, size_t n);
    std::wstring FlushFinal();          // 尾巴落成 U+FFFD（≈ Python final=True errors=replace），并清空
    void Reset();                       // 丢弃半截尾巴（如清除窗口）
private:
    std::wstring enc_;
    std::vector<uint8_t> tail_;
};

} // namespace sncom
