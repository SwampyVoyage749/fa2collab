# 交接文档（HANDOFF）

> 写给"下一个接手的人"（可能是白天的你，也可能是接入的 agent）。
> 目标：不改一行代码也能把现状跑起来；要改也能知道从哪下手。

## 一、环境（这台机器上已经装好）

| 项 | 位置 |
|---|---|
| VS Build Tools（MSVC 14.44 + SDK 10.0.26100） | `D:\VSBuildTools` |
| 源码树（编译用副本） | `C:\FA2Build` |
| 源码树（原始，GBK 文件在这） | `F:\Deepseek\DeepSeekHarness\fa2sp_src` |
| 我们加的模块 | `fa2sp_src\FA2sp\Ext\CCollab\` |
| 联机内核 + 单测 | `F:\Deepseek\DeepSeekHarness\fa2collab\src`、`tests` |
| 官方 1.5.3 运行包（解压好） | `F:\Deepseek\DeepSeekHarness\fa2sp_runtime` |
| 测试用双实例 | `fa2sp_test_ours`（房主）/ `fa2sp_test_beta`（加入方） |
| 交付包 | `fa2collab\FA2spCollab_v0.3.zip` |

## 二、怎么编、怎么测

```powershell
# 编译（约 2~3 分钟，只改 CCollab 时是增量）
& 'D:\VSBuildTools\MSBuild\Current\Bin\MSBuild.exe' 'C:\FA2Build\FA2sp.sln' /nologo /m:8 /v:m `
    /p:Configuration=Release /p:Platform=x86 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:DebugInformationFormat=None

# 一键回归（11 项断言，约 90 秒）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\verify.ps1

# 长压测（11 分钟，看内存与稳定性）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\soak_memory.ps1

# 物件渲染刷新（前后截图 + 日志断言，约 90 秒）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\objtest.ps1
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\objtest.ps1 `
    -Map "D:\Mental Omega FS\MapsMO\Challenge\c_battlecity.map" -Injector client

# 物件重建压力（每 8 秒注入一次，观察重建次数/崩溃/内存）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\soak_objects.ps1 -Minutes 5
```

## 三、当前已验证到什么程度

| 能力 | 状态 | 证据 |
|---|---|---|
| 32 位编译 + 替换官方 DLL 启动开图 | ✅ | `FA2sp.log` 自报 1.6.3 / HDM 1.5.3 |
| 同图握手（FNV 哈希） | ✅ | 异图拒绝、同图连接 |
| 光标 / 名字 / 笔刷 / 选区 / 玩家列表 | ✅ | 截图 4 张 |
| 地形 / 覆盖图逐格增量 | ✅ | `cells=1` |
| 租约拒绝 + 冲突红框 + 被占块保护 | ✅ | 日志 + 截图 |
| 中途加入快照 | ✅ | 三实例 169 块 |
| 断线重连 | ✅ | 加载地图阻塞不再误杀 |
| INI 节增改 | ✅ | `applied ini section ... total=N` |
| INI 冲突检测 | ✅ | `CONFLICT on ini section` |
| 持续编辑稳定性 | ✅ | 11 分钟 / 88 次编辑 / 0 异常 / 内存收敛 |
| 物件同步（建筑 / 车 / 兵 / 飞机） | ✅ 走 INI 节 + 重建渲染表 + 重绘 | 日志 `object lists rebuilt: buildings=N...`；截图前后对比 |
| INI 删键 | ⚠️ 实验（默认关） | 同上 |
| 真实工程图 | ❌ 未测 | 只有 `presets_tem.map`（200×200 空白图） |

## 四、改代码前必须知道的 6 个坑

1. **新源码必须存 GBK**：工程用 `/source-charset:gbk`，UTF-8 会让中文注释吃掉 `#pragma once`
2. **改 `.vcxproj` 会全量重编**（176 个 TU，40~60 分钟）；只改 `.cpp` 才是增量（2~3 分钟）
3. **`Logger::Info` 只吃有限类型**：`unsigned long long` 会 `static_assert`
4. **`CIsoView::ViewPosition` 是屏幕级偏移**，不是格子偏移；地图↔屏幕换算漏了它光标会飞到几万像素外
5. **等距投影下对角两点屏幕 x 相同**：画矩形必须取四角包围盒，否则零宽不可见
6. **`FAMap`/`FATree` 删元素只能用 `manual_erase(iterator)`**，用 `erase` 下一 tick 就访问违例
7. **物件只能通过 INI 节同步，不能直接改 `CellDatas` 的物件槽**：等距视图用 `CellDatas` 里的索引去查
   `CMapDataExt::BuildingRenderDatasFix / UnitDatasExt / AircraftDatasExt / CMapData::InfantryDatas`，
   这几张表只有 `UpdateField*Data(false)` 才会重建；直接写索引 = 越界崩溃 / 画不出来
   （`CMapData::BuildingDatas` 是死字段，注释写着 *Never being really used*，别往那儿写）

## 五、下一步建议（按性价比排序）

1. **真实地图实测**（最重要）
   - 用你实际在做的图，两边同时开，画一片地、放几个单位、改个触发
   - 若崩：把 `debug\snapshot-*\except.txt` 和 `FA2sp.log` 留好
2. **触发按 ID 合并**
   - 现状：整节覆盖（LWW）。触发都躺在 INI 节里，键是 `0=…`、`1=…`
   - 做法：把 `ScanIni` 的"节级哈希"改成"键级哈希"，只发变化的键；应用时只写这些键
   - 好处：两个人改**不同**触发不会互相覆盖
3. **房间 UI**
   - 现状只能改 `FA2spCollab.ini`
   - 最轻做法：运行时 `CreateWindowEx` 建一个设置窗（不需要 .rc），或挂在 FA2 的 `Online` 菜单下
4. **实验特性复活**
   - INI 删键：需要先搞清 FA2 内部对象表与 INI 索引的重建时机，否则会崩
   - 物件槽直写已废弃，别再往回改

## 六、如果只想用，不想改

装 `FA2spCollab_v0.3.zip` 里的 DLL + ini，双方开同一张图，按 README 第七节对日志即可。
