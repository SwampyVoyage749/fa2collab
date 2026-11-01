# 证据索引（按功能对照）

> 全部证据都在 `F:\Deepseek\DeepSeekHarness\fa2collab\evidence\` 与两份实例日志里。
> 复现方式：`powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\verify.ps1`

## 一键验证结果（2026-11-01 05:35）

```
编译 FA2sp.vcxproj        PASS
联机内核单测              PASS        （17 条断言，见 tests/test_collab.cpp）
A 建图 + 开房             PASS
B 加入并看到房主          PASS
A 看到 B                 PASS
地形+物件增量到达         PASS
租约拒绝生效              PASS
被占块被还原              PASS
INI 节同步               PASS
快照推送完成              PASS
无崩溃                   PASS
== 全部通过 ==
```

## 逐项证据

| 功能 | 证据文件 / 日志关键字 |
|---|---|
| 32 位编译 | `C:\FA2Build\Release\FA2sp.dll`，PE `machine=0x014C` |
| 替换官方 DLL 可运行 | `FA2sp.log`: `Applying FA2sp 1.6.3, HDM Edition 1.5.3` |
| 同图才连 | `map ready: ... hash=0x6C1B8603`；异图时无 `peer joined` |
| 光标 / 名字 | `evidence/cursor_closeup.png` |
| 玩家列表 | `evidence/player_list.png` |
| 远端选区 | `evidence/selection_client.png` |
| 地形逐格增量 | `tile delta ... cells=1` / `applied remote chunk ... cells=1` |
| 物件（车/兵/建筑） | `applied ini section [Structures]` → `object lists rebuilt: buildings=N units=M` → `LoadObjects loading: <TypeID>`；截图 `objtest_client_before/after.png`（空白图）、`objtest_c_battlecity_client_before/after.png`（MO 真实图） |
| 租约拒绝 | `chunk (12,12) owned by player 0 — local change NOT synced` |
| 冲突描红 | `evidence/conflict_crop.png` |
| 被占块保护 | `protected chunk (12,12): restored 16 cells` |
| 中途加入快照 | `snapshot push finished: 169 chunks` + 晚加入实例 169 次 `applied` |
| INI 节同步 | `applied ini section [CollabTest] entries=1 total=1` |
| INI 删键 | `ini section [CollabTest]: removed 1 stale keys` |
| INI 冲突检测 | `CONFLICT on ini section [CollabTest]: local edit overwritten by remote` |
| 断线重连 | 加载地图阻塞 10s 不再误杀；`disconnected, retrying` |

## 踩坑记录（都写进了 projects.md 长期记忆）

1. 新源码必须存 **GBK**（工程用 `/source-charset:gbk`）
2. `Logger::Info` 只支持有限类型，`unsigned long long` 会 static_assert
3. 改 `.vcxproj` 触发全量重编（176 个 TU，约 40-60 分钟）
4. `CIsoView::ViewPosition` 是**屏幕级偏移**不是格子坐标
5. 等距投影下对角两点屏幕 x 相同，画矩形必须取**四角包围盒**
6. FAMap/FATree 删元素只能用 **`manual_erase`**，用 `erase` 会在下一 tick 访问违例

## 物件渲染刷新（2026-11-01 13:30）— 已打通

复现：`powershell -ExecutionPolicy Bypass -File objtest.ps1`（空白图，房主注入）；  
`objtest.ps1 -Map "D:\Mental Omega FS\MapsMO\Challenge\c_battlecity.map" -Injector client`（真实图，客户端注入）

| 断言 | 结果 |
|---|---|
| 注入端写入真实 [Structures] 记录 | PASS（`selftest: placed structure key=999 type=GAPOWR/CAFRMA`） |
| 观察端收到 [Structures] 节 | PASS |
| 观察端重建对象列表 | PASS（空白图 buildings=1；MO 图 buildings=282→283、units=83） |
| 观察端加载该建筑素材 | PASS（`CLoadingExt::LoadObjects loading: GAPOWR / CAFRMA`） |
| 画面上真的出现该建筑 | PASS（前后截图做像素差，差异区正好是那栋建筑） |

## 仍未完成

- 触发"按 ID 合并"（目前整节覆盖，LWW）
- 房间 UI（仍要手改 `FA2spCollab.ini`）
- 真实工程图未测（仅 `presets_tem.map`）

## 稳定性

| 项 | 数据 |
|---|---|
| 11 分钟持续压测（每 8 秒一次 16 格改动） | 88 次编辑、异常 0、崩溃快照 0、两进程全程存活 |
| 内存趋势 | 241 → 249 MB，前 6 分钟缓涨后趋平（非泄漏），原始数据 `evidence/memory_soak.csv` |
| 一键回归 | `verify.ps1` 11 项全 PASS |
## 真实 MO 地图（带触发）实测 — 2026-11-01 06:45

- 地图：`c_battlecity.map`（311 KB / 88 个 INI 节 / 13 条触发）
- 修复三个问题后：客户端应用 **65 个节**，含
  `Triggers / Events / Actions / Tags / TeamTypes / TaskForces / ScriptTypes / Waypoints`
- 结果：**0 断线、0 异常、两进程存活**
- 修复内容：
  1. INI 应用后不再逐节 `Update()`，改为每 tick 一次（原来 85 次全量重绘把 UI 线程堵死）
  2. 连接超时 3s → 6s；INI 发送限流 20 节/轮
  3. 有人加入时清空 INI 哈希，强制把全部节重发给新人（原来中途加入者拿不到触发）
## 出货版本最终复测（2026-11-01 11:2x）

对**打进交付包的那份 DLL** 在 MO 真实图上复测：

- 客户端应用 **40 个节**，含 `Triggers / Events / Actions / TeamTypes`
- 主机异常 0、客户端异常 0
- 断线 0、两进程存活
- 主机的 iniApplied=147（收 + 发累计）
## 真实 MO 地图 5 分钟稳定压测（2026-11-01 11:3x）

条件：`c_battlecity.map`（311 KB），房主每 8 秒注入一次 16 格改动，客户机只收。

- 房主执行 **36 次**编辑
- **异常 0 / 断线 0 / 崩溃快照 0**，两进程全程存活
- 内存：房主 228 → 227 MB（持平）；客户机 188 → 236 MB（接收大量增量后趋于上限）

结论：出货版本在真实工程图 + 持续编辑下稳定，不是只在空白图上成立。

## 删除同步（v0.4.1，2026-11-01 18:0x）

复现：`powershell -ExecutionPolicy Bypass -File deltest.ps1`
（真实图：`-Map "D:\Mental Omega FS\MapsMO\Challenge\c_battlecity.map"`）

| 断言 | 结果 |
|---|---|
| 注入端放入建筑（key=999） | PASS |
| 注入端删除该键 | PASS |
| 观察端日志 `removed 1 keys (deleted by peer)` | PASS |
| 观察端建筑数 +1 后回到基线（空白图 [1,0]；MO [282,283,282]） | PASS |
| 空白图：画面出现建筑 / 删除后消失（像素差 1195） | PASS |
| 空白图：删除后与初始画面**逐像素一致** | PASS（diff=0） |
| 截图 | `evidence/deltest_presets_tem_1_before.png` / `_2_placed.png` / `_3_deleted.png` |
| 压测 | `soak_objects.ps1`（SelfTestDeleteDelayMs=4000，放/删抖动）5 分钟：37 放 / 37 删 / 对端删 37 / 重建 78+75 / 0 异常 / 0 崩溃 / 内存 240→229 MB |

> 真实图（MO）上像素差只作参考：已有 282 个建筑可能遮挡注入点，且水面/灯光有动画；
> 判定以建筑计数与日志为准。

## 非法坐标崩溃回归（v0.4.2，2026-11-01 19:0x）

复现：`powershell -ExecutionPolicy Bypass -File crashtest.ps1`（房主广播 (-1,-1) 与 (30000,30000)）

| 断言 | 结果 |
|---|---|
| 对端日志 `ignore invalid cursor` | PASS |
| 对端进程存活 | PASS |
| 两边无 `snapshot-*` 崩溃快照 | PASS |
| 无 `exception in Tick` / `Exception handler fired` | PASS |
