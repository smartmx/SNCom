// sncom/src/ui/layout.h —— 固定布局设计常量（96 DPI 设计像素，运行时按 DPI 缩放）
// 分区基准：PyCom V0.0.5 实测（菜单栏 → 接收显示框 → 工具行 → 底部面板 → 状态栏）；
// 控件宽度一律由 TextW()/BtnW()/ChkW() 按当前字体实测（M1 标定口径，不照抄 Qt 常量）。
#pragma once

namespace sncom {
namespace layout {

constexpr int kDefaultW   = 960;   // 与 PyCom 默认窗口一致
constexpr int kDefaultH   = 720;

constexpr int kMarginX    = 6;     // 面板/工具行左右外边距
constexpr int kGap        = 3;     // 通用间距
constexpr int kGapTight   = 4;     // 标签与控件之间

// 接收显示框（顶层，直接贴主窗视口；PyCom: x=4, w=cs-8）
constexpr int kRxTop      = 3;
constexpr int kRxMarginX  = 4;
constexpr int kRxMinH     = 90;    // 极矮窗口兜底（PyCom: max(90, ...)）

// 工具行（在接收显示框之下）
constexpr int kBtnH       = 23;    // 工具行按钮/复选框高
constexpr int kRowGap     = 3;     // 工具行与上下块的间距
constexpr int kToolGapBig = 16;    // 新窗口 → 自动滚屏（PyCom addSpacing(16)）
constexpr int kToolGapFile= 30;    // 自动滚屏 → 打开文件（PyCom addSpacing(30)；该处 x 由
                                   // 中列锚点覆盖，值仅留语义）
constexpr int kToolGapTail= 20;    // §82：清发送区 → 收起——默认窗宽 960 时让工具行
                                   // （到「扩展」）完整可见（30 时尾按钮右缘超客户区 7px）
constexpr int kEdtFileW   = 160;   // 发送文件路径编辑框（PyCom setFixedWidth(160)）

// 底部面板（定高：左列 5 行 + 大「发送」按钮；对齐 PyCom panel1 定高行为）
constexpr int kPanelH     = 176;
constexpr int kPanelPadY  = 6;
constexpr int kRowH       = 26;    // 面板内行高
constexpr int kCtlH       = 24;    // 单行控件高
constexpr int kBtnMinH    = 30;    // 「发 送」按钮高
constexpr int kColGap     = 10;    // 三列之间间距
constexpr int kComboDropH = 200;   // 下拉展开高度
constexpr int kArrowPad   = 24;    // 下拉箭头 + 内边距
constexpr int kBtnPadX    = 18;    // 按钮左右内边距
constexpr int kChkBox     = 20;    // 复选框方框 + 间距
constexpr int kEditSpinW  = 48;    // 超时 / 第 N 字节 输入框（PyCom 48）
constexpr int kEditTimedW = 62;    // 定时发送输入框（容纳 7 位）
constexpr int kEditChkW   = 96;    // 校验值只读框（PyCom 96）
constexpr int kTailCmbW   = 74;    // 帧尾档位下拉（闭合态收窄，弹出列表更宽）
constexpr int kChkCmbW    = 74;    // 加校验下拉
constexpr int kPinW       = 32;    // 线路引脚指示框（PyCom 34x18）

constexpr int kStatusH    = 22;
constexpr int kNarrowW    = 920;   // 窄窗阈值：窄窗隐藏状态栏版本号段

}} // namespace sncom::layout
