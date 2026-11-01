#pragma once

// FA2SP 局域网协同编辑 —— 模块入口
//
// 设计原则（重要）：
//  1. 不新增任何“地址打补丁”，只读现成对象（CFinalSunDlg / CIsoView / CurrentCommand），
//     避免与上游 1.5.x 的硬编码地址冲突。
//  2. 网络全部非阻塞，心跳/轮询由 UI 线程的 WM_TIMER 驱动，绝不跨线程碰地图数据。
//  3. 任何异常都降级为“单人模式”，不许把地编带崩。
//  4. 默认关闭，必须显式在 FA2spCollab.ini 里 Enabled=1 才启动。

#include <FA2PP.h>
#include "../FA2Expand.h"

class CCollabExt
{
public:
    static void ProgramStartupInit();   // 由 FA2Expand::ExeRun() 调用
    static void Tick();                 // WM_TIMER 驱动
    static void Shutdown();

    static bool Enabled();
};
