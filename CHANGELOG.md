# Changelog

## v0.4.2 — 2026-11-01

### 修复

- **联机崩溃（真实图复现）**：对端鼠标移出地图/视野时广播 `(-1,-1)`；渲染对方光标时直接调用
  `CIsoView::MapCoord2ScreenCoord`，其内部 `GetCellAt(X,Y)` 无越界检查 → `C0000005 @ 0045E8F0`
  （事件日志另有显卡驱动 `nvoglv32.dll 0xc0000409`）
- 新增 `CellInRange()` 内存安全校验，覆盖对方光标 / 选区 / 冲突红框 / 本机广播四个调用点；
  本机鼠标在无效格子时不再广播；被忽略的坐标打 `ignore invalid cursor`（5 秒限流）
- 新增回归脚本 `scripts/crashtest.ps1`（一端广播 `(-1,-1)` 与 `(30000,30000)`，另一端必须忽略且不崩）

### 验证

- `crashtest.ps1` 5/5、`verify.ps1` 14/14、`deltest.ps1` 8/8

## v0.4.1 — 2026-11-01

### 变更

- **删除同步转正**：删掉建筑 / 车辆 / 步兵 / 触发等 INI 键，对端会真的删掉（默认 `IniDeleteSync=1`）。
  - 实现改成「按三方合并结果删键」：只有 `本地有 → 合并结果没有` 的键才删，
    不再拿「远端整节」做镜像（旧做法会误删本地新增的键，破坏"双方各加新键共存"）
  - 删除走 `GetEntities().find()` + `manual_erase(it)`，并在同一 tick 重建物件列表 + 重绘
  - 一边删、一边改同一条 → 记冲突并保留改动（不删），属预期保护
- `IniDeleteSync` 默认值 0 → 1；配置模板注释更新

### 验证

- `deltest.ps1`：放建筑 → 对端出现（像素差）→ 删键 → 对端消失（像素差），
  空白图与 MO 真实图均 8/8 PASS；删除后画面与初始**逐像素一致**（diff=0）
- 5 分钟「放/删」抖动压测：0 异常 / 0 崩溃快照 / 内存持平（见 `docs/EVIDENCE.md`）

## v0.4.0 — 2026-11-01

### 新增

- **物件渲染刷新**：建筑 / 车辆 / 步兵 / 飞机 现在会真的出现在对方画面里。
  - 物件本体走 INI 节（`[Structures]/[Units]/[Infantry]/[Aircraft]`）同步
  - 收到后调用 FA2 自己的 `UpdateFieldStructureData(false)` / `UpdateFieldUnitData(false)` /
    `UpdateFieldInfantryData(false)` / `UpdateFieldAircraftData(false)` 重建
    `BuildingRenderDatasFix / UnitDatasExt / AircraftDatasExt / InfantryDatas`
  - 重建后整屏重绘（`RDW_INVALIDATE | RDW_UPDATENOW`），渲染器按新索引 `Reload`
  - 每 tick 最多重建一次；重建崩溃只关闭该特性，不断开联机
- 房间设置窗（`Ctrl+Alt+C`）：名字 / IP / 端口 / 主客，改完「应用并重连」
- 三方合并（base/local/remote）接管 INI 节应用，不同键不再互相覆盖
- 测试脚本：`objtest.ps1`（物件同步前后截图验证）、`soak_objects.ps1`（物件重建压测）

### 修复 / 变更

- **删除**旧的「物件槽直写」实现（`SyncObjects` 开关作废）。它直接写 `CellDatas` 的物件索引，
  而渲染表没有对应表项，导致索引越界崩溃 —— 这也是 v0.3 压测崩溃的根因
- 不再往 `CMapData::BuildingDatas` 写数据（上游注释明确写着该字段实际未被使用）
- `verify.ps1` 断言从 11 项扩到 14 项（加物件列表重建 / 物件节到达）
- 配置模板去掉 `SyncObjects`，补注释；`setup_collab.cmd` 同步更新

### 验证

- `verify.ps1` 14/14 PASS
- 空白图 + MO 真实图（`c_battlecity.map`）物件同步均有前后截图之像素差证据
- 5 分钟物件重建压测：37 次注入 / 对方重建 39 次 / 异常 0 / 崩溃快照 0 / 内存持平

## v0.3.5 — 2026-11-01

- 真实 MO 地图（88 节 / 13 触发）实测：修复 INI 刷新风暴、连接超时、中途加入者缺 INI
- 快照推送分批（每 tick 8 块），大图不再卡 UI
- 玩家列表补上房主

## v0.3 — 2026-11-01

- 首个可用版本：握手 / 同图校验 / 光标 / 选区 / 地形逐格增量 / 区块租约 / 冲突红框 /
  被占块还原 / 断线重连 / INI 节同步 / 中途加入快照
- 联机内核独立成 `core/`，带 17 条单测（后扩到 22 条）
