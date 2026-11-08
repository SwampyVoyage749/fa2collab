## [未发布] 放置提示（选中建筑时显示地基 / 地形 / 单位冲突 + 红框）

- 悬停面板新增 `Foundation: WxH, occupied: n/m, blocked: k, units: u`，有冲突时行尾标 ` (!)`
- 自定义地基按 Foundations 列表逐格统计
- 地形限制：悬崖 / Rock7 / Rock8 一律不可建；水面 / 海滩只在 art 有 `WaterBound=true` 时可建
- 单位占位：格子上有车辆或步兵时计入 `units`
- 冲突格用红色 1×1 网格框标出（DX 与 GDI 两条渲染路径都支持）
## [未发布] 地图自检（Ctrl+Shift+C，实验性）

- 新增 `CIsoViewExt::RunMapSelfCheck()`：9 类静态检查（Tag→Trigger、空 Events/Actions、type=0、TeamType→Script、Action type=7→TeamType、孤儿触发器）
- 结果同时弹窗 + 写 `<地图路径>.mapcheck.txt`（GBK）
- 每条问题带触发器名字；`ExtConfigs.MapSelfCheckOnLoad`（默认关）可开图即查
# Changelog

## v0.5.1 — 2026-11-04（预览模式）

- 新增 `CIsoViewExt::PreviewMode`：热键 **Ctrl+P** 切换，开启后整张图按 art.ini 播放建筑动画，40ms 一帧重绘。
- `ExtConfigs` 里的 `InGameDisplay.AnimPreview` 也能直接开启预览模式（Options 里可勾）。
- 修复：动画帧此前被塞进 `AlphaImagesToDraw`（光照通道）导致整块发黑；改为正常透明精灵绘制。
- 修复 ini 开关驱动时定时器被误杀、建筑动画被自身遮挡；单位/步兵也接入预览动画。
- 预览重绘间隔 40ms → 100ms，CPU 从 31.7% 降到 14.3%（基线 1.2%），内存无增长。
- 开预览时若地图有 `[Lighting]` 段，自动切到「正常」光照；关预览恢复原设置。
- 已知不足：未接 art 偏移量 ActiveAnimX/Y；未接光照/受损/朝向。


## 未发布 — 接手修复（2026-11-04）

- 合并等待租约的同块增量，不同格累积、同格保留最新值。
- 完整校验网络消息后再应用，拒绝截断 INI、异常单元格数量、超范围租约和握手前写入。
- 双向核对协作构建标识，Welcome 携带协议版本 2；拒绝旧版本混连。
- 插件端增加索引/区块/字段范围校验，累积区块恢复状态；拒绝租约后定期重试。
- 移除 INI 接收后一秒内吞掉本地真实编辑的时间窗；换图重置合并与保护状态。
- 内核构建脚本自动寻找 VS 工具链并执行测试；内核 39 项断言全部通过，部署核心 GBK 编译通过。
- 本地已补做完整验收（2026-11-04，DLL 9,587,712 字节）：verify 14/14、crashtest 5/5、
  deltest 8/8、objtest 6/6 全通过。详见 docs/接手实施记录.md。

## v0.5.0 — 2026-11-04

### 新增

- **悬停信息增强（做图向）**：鼠标停在建筑 / 车辆 / 步兵 / 飞机上时，在原有信息行末尾多一行
  Cost: 900, Sight: 8, Speed: 6（建筑只有 Cost 与 Sight），数据直接取自地图规则（rules.ini）
- 面板不再只在「对象/指针工具（ESC）」下出现：**按 Ctrl+H 可在任意工具下常显**（本机临时状态，不写 ini）
- 新配置项 [ExtConfigs] InGameDisplay.GameStats（默认 	rue）：设为 alse 可完全不显示这行

### 说明

- 改动落在上游 FA2sp 源码（CIsoView/Body.OnMouseMove.cpp 等），改法见 patches/hover-gamestats.md
- 已验证：犀牛重型坦克（HTNK）Cost: 900, Sight: 8, Speed: 6；建筑只显示 Cost/Sight

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
