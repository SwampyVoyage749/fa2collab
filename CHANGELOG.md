# Changelog

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
