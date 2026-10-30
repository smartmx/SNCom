// sncom/src/core/paths.h —— 路径（与 PyCom 的 app/paths.py 同义：打包/运行目录基准）
#pragma once
#include <string>
namespace sncom {
std::wstring ExeDir();                                  // exe 所在目录（无尾反斜杠）
std::wstring ConfigPath();                              // <ExeDir>\sncom.json
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
}
