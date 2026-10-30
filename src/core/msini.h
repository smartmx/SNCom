// sncom/src/core/msini.h —— 原版 ini 多条字符串解析核（G3 对拍提取：纯逻辑可单测）
// 口径 = PyCom Settings._read_ini_keys + _parse_ms_rows（docs/09 P3-3 三处对齐：
// ① body 无逗号 → text 置空（PyCom split(",",1) 无第二段）② HEX 标签整段 == "H"
// ③ 值只剥前导逗号（先 trim 空白）；另：同名键后者覆盖、; 注释行跳过、
// ini_int 整段解析失败即默认（"12x" 非法，非 stoi 部分解析）。
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace sncom {

struct MsIniRow {
    int order = 0;
    std::wstring note;
    bool hex = false;
    std::wstring text;
    int interval = 1000;
};

// ini 键值表：GBK 解码后的文本逐行切分；键 trim、值原样；同名键覆盖（PyCom dict 口径）
void ParseIniKeys(const std::wstring& text, std::vector<std::pair<std::wstring, std::wstring>>& keys);

// ← PyCom _parse_ms_rows：N(100+i)=顺序,注释,延时；Ni=H/A,数据。返回行数（≤99）。
int ParseMsIniRows(const std::vector<std::pair<std::wstring, std::wstring>>& keys,
                   std::vector<MsIniRow>& out);

} // namespace sncom
