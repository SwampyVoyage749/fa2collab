# 进度（2026-11-01）

## 已完成

| 阶段 | 状态 | 证据 |
|---|---|---|
| S0 编译环境 + 原样编译 | ✅ | `C:\FA2Build\Release\FA2sp.dll`（x86, PE 0x014C）；`FA2sp.log` 自报 `Applying FA2sp 1.6.3, HDM Edition 1.5.3` |
| S0 运行验证 | ✅ | 官方 `FA2SP_HDM_Edition_1.5.3.7z` 解压到 `fa2sp_runtime`，用本喵的 DLL 替换后能开图（标题 `(presets_tem.map)`） |
| S1 握手 / 玩家列表 | ✅ | 两实例 TCP 31415：`peer joined: id=1 name=Beta` |
| S1 光标同步（传输） | ✅ | `cursor from id=1 cell=(198,195)`，鼠标移动时坐标随之变化 |
| S1 光标同步（渲染） | ✅ | 覆盖层窗口画青色圆点 + 名字，截图 `evidence/cursor_closeup.png` |

## 关键实现

- `FA2sp/Ext/CCollab/Body.cpp`：隐藏消息窗 + 100ms `WM_TIMER` 轮询；读 `CIsoView` 只读字段；分层窗口（`WS_EX_LAYERED + WS_EX_TRANSPARENT`，颜色键 `RGB(255,0,255)`）画远端光标，**不碰 FA2 的 DirectDraw**。
- 坐标换算：
  - client → map：`ScreenCoord2MapCoord(client + rect + ViewPosition)`
  - map → client：`client = MapCoord2ScreenCoord(cell) - ViewPosition`
  - 踩过的坑：`ViewPosition` 不是格子偏移，而是**屏幕级偏移**（实测 11291/5408），第一次换算漏了它，光标被画到 (11699,5818)。
- 网络层：`core/` 下 5 个文件，非阻塞 + 30s 心跳超时 + 客户端 3s 自动重连（UI 线程加载地图会阻塞 > 8s，旧超时会把对端踢掉，已修）。

## S2 进展（2026-11-01 02:1x）

- [x] 地图哈希校验：同图连、异图拒（`0x6C1B8603` vs `0x218ECD9B` 实测）
- [x] 会话懒创建 / 换图自动重建
- [x] 地形/覆盖图增量同步：**已验证**
  - Host: `selftest: modified cell idx=8020 (20,20)` → `tile delta cells=256`
  - Client: `tile delta from=0 chunk=65537 cells=256` → `applied remote chunk (1,1) cells=1`
  - 说明：远端只真正改动 1 格（其余 255 格比对后跳过），去重生效
- [x] 逐格 diff：发送量从 256 格降到 **1 格**（`local chunk changed cells=1` → `tile delta cells=1`）
- [x] 本地改动前自动 `RequestLease`，Host 授予（`lease (16,16)-(31,31) granted=1`）
- [x] 租约**真拦截**：块被别人占着时 Host 回 `LeaseDeny`，本地侧打 `chunk (16,16) owned by player 0 — local change NOT synced`（实测）
- [x] 竞态修复：客户端改图先**排队**，拿到 `LeaseGrant` 才发 `TileDelta`（被拒则丢弃）；Host 不再收到未授权的增量
- [x] **两个真 bug 已修**：①租约矩形误用格坐标（一块被记成 256 个块键，导致后续请求全被判"空闲"）；②自测写入 y=200..203 越出 200 行地图边界，扫描根本看不见
- [x] 回归验证：A 改 16 格 → `lease (12,12)-(12,12) granted` → `tile delta cells=16` → B `applied remote chunk (12,12) cells=16`
- [x] 冲突拒绝**可复现**：A 写 overlay=5 拿租约 → B 写 overlay=7 → `chunk (12,12) owned by player 0 — local change NOT synced`（连续多次跑都能复现）
- [x] 修掉第三处坐标系 bug：`SendTileDelta` 内部申请租约时仍用 `/16` 的格坐标，导致拒绝后又被"授予"了 256 个假块键
- [x] 冲突块描红**已取证**：截图 `evidence/conflict_crop.png` 里红色矩形框住被占的块 (12,12)，光标点正好在框角
  - 根因很有意思：等距投影下 `(192,192)` 与 `(199,199)` 两个对角点算出来的客户区 **x 完全相同（都是 498）**，矩形退化成一条零宽的线 → 什么都没画
  - 修法：取块的四角 `(x0,y0)(x1,y0)(x0,y1)(x1,y1)` 求包围盒：`g_deniedChunks` + 覆盖层红框（代码已就位，视觉验证待补）
- [x] 玩家列表补全：Welcome 里带上 Host（id=0），客户端不再"看不见房主"
- [x] 日志降噪：整段会话只剩 12 行 collab 日志（原来每秒刷 `draw peer` 与重连告警）
- [x] 自身改动不会回环：应用远端增量后重算该块哈希

## S3 起步（0:00 前推进）

- [x] **物件同步**：`CellEdit` 扩展 6 个物件槽（`Unit / Infantry[3] / Aircraft / Structure`），`HashCell` 一并纳入；实测
  - `applied remote chunk (12,12) cells=16 objects=16` —— 16 个物件槽真的写进了对端 `CellData`
  - 保护机制不受影响：随后 Beta 改同块依旧被拒 + 还原
- [ ] 触发/小队/特遣/脚本/AI 触发：这些是**引用关系**（ID 交叉引用），不能照抄按格 diff，要按 ID 对齐后合并
- [ ] INI 文本合并（地图内嵌 INI）
- [ ] 房间 UI

## 打包（2026-11-01 04:2x）

- 交付包：`fa2collab/FA2spCollab_v0.3.zip`（2.3 MB）
  - FA2sp.dll（9,549,824 B，PE machine=0x014C，sha256 B3AB03...D6C85）
  - FA2spCollab.ini（模板，含注释）
  - README.md（安装/验证/回滚/已知限制）
- 用交付包里的文件单独跑了一遍：进程存活、[Collab] hosting on port 31415、快照初始化正常

## INI 节同步（2026-11-01 04:5x，重启后继续）

- 新增消息 `IniSection`：按**节**同步地图 INI（触发/小队/特遣/脚本/所属方都在这里）
- 扫描：每 500ms 遍历 `CINI::CurrentDocument->Dict`，逐节算 FNV 哈希；跳过 TileSet/IsoMapPack/OverlayPack/PreviewPack 等纯地形节
- 应用：`AddOrGetSection` + 逐键覆盖（`InsertPair` 补新键），改完 `Update()` 刷新
- 实测（双向）：
  - A 写 `[CollabTest] By=Alpha` → B `applied ini section [CollabTest] entries=1`
  - B 写 `By=Beta` → A `applied ini section [CollabTest] entries=1`
- 已知限制：**只增改、不删键**；首次连接两边会各广播一遍全部节（初始对齐），日志会比较吵
- 交付包已刷新：`FA2spCollab_v0.3.zip`（含 INI 同步的新 DLL）

## 选区同步（2026-11-01 05:0x）

- 读 `MultiSelection::SelectedCoords` 求包围盒，随 CursorState 一起发（`hasSel` + `Rect`）
- 对端覆盖层画橙色虚线框（PS_DOT）；等距投影同样要四角包围盒
- 踩坑：原去重 key 只含 `cx/cy/tool/brush`，选区变了不发；已把 hasSel+矩形并入 key
- 取证：`evidence/selection_client.png` —— Beta 窗口里出现橙色虚线选区框，而 Beta 本地没有选区（无 Multi-Selection 横幅）

## INI 冲突处理（2026-11-01 05:3x）

- 每节记录 g_iniLocalTick / g_iniRemoteTick；应用远端节时若本地 1 秒内也改过 => CONFLICT 日志，HUD 增加 ini-conflict:N
- 抑制回弹：刚应用远端节后 1 秒内的本地 diff 视为回声，不再发回
- 修掉一个误报：首轮扫描把「首次见到该节」当成本地编辑，导致开局几十条假 CONFLICT；改为首见只建基线
- 实测：开局 CONFLICT 计数 A=0 / B=0；节同步仍正常（[CollabTest] 双向传递）
- 未验证：真正「同一秒内两人改同一节」的冲突日志尚未复现（时机太窄）
## 尝试「INI 删键同步」并回滚（2026-11-01 05:5x）

- 目标：整节镜像，远端没有的键本地也删掉（否则删除永远同步不过去）
- 卡点：`INISection::GetEntities().erase(key)` 走 FAMap 模板，`erase(const char*)` 与 `erase(ppmfc::CString)` 都触发 `FATree.h(573) C2064`，短时间没能绕过
- 处置：**回滚该改动**，保留原有「只增改、不删键」语义；DLL 重新编译并冒烟通过（进程存活 + `[Collab] started`）
- 结论：删键同步需要一个 FAMap 的正确用法（可能要用 `manual_erase(iterator)` 或先取迭代器），留待下轮
## INI 删键同步打通 + 首次真实冲突取证（2026-11-01 05:2x）

- 卡点绕法：FAMap 的 erase(key) 模板不可用，改用 **iterator 版**：
  auto it = section.GetEntities().find(ppmfc::CString(key)); if (it != end) erase(it);
- 语义：整节镜像 —— 远端没有的键本地删掉（否则删除永远同步不过去）
- 实测（两个实例都进行"删键"自测）：
  - A: selftest: deleted key By -> applied ini section [CollabTest] entries=0 total=0
  - B: selftest: deleted key By -> applied ini section [CollabTest] entries=0 total=0
- **首次抓到真实并发冲突日志**：A 打出
  CONFLICT on ini section [CollabTest]: local edit overwritten by remote
  （上一轮说"没复现"，这轮复现了）
## 一键验证脚本 + 抓到并修掉一个崩溃（2026-11-01 05:3x）

- 新增 `fa2collab/verify.ps1`：编译 -> 单测 -> 双实例 -> 断言日志，11 项检查全 PASS
- **脚本立刻抓到一个真 bug**：用 `GetEntities().erase(it)` 删 INI 键之后，
  B 的日志出现 `[Error] [Collab] exception in Tick, disabling collab` + `Exception handler fired!`
  —— FAMap/FATree 的 erase 迭代器版本会破坏内部结构，下一 tick 就访问违例（被我们的 SEH 兜住，降级单人）
- 改用 `manual_erase(it)` 后：11 项全部 PASS，无异常
- 经验：这类自定义容器只能用 `manual_erase`；任何"看起来编译通过"的容器操作都要跑一遍双实例验证
## 压测发现崩溃 -> 两个特性改为实验开关（2026-11-01 05:4x）

- 用 SelftTestLoopMs 做 3 分钟重复编辑压测，两个实例都崩了（debug\snapshot-*\except.txt，C0000005 @ 5DE09B39）
- 排查：关闭「物件槽同步」后仍崩 -> 再关闭「INI 删键同步」+ 去掉删键自测后：
  2.5 分钟压测 **0 次异常 / 0 个崩溃快照 / 两进程存活**
- 处置：SyncObjects 与 IniDeleteSync 都改为 ini 开关，**默认关闭**；交付包 ini 里写明"开启前备份地图"
- 教训：这类"直接写 FA2 内部结构"的特性必须做长压测，单次功能验证不够
## 真正的重复编辑压测通过（2026-11-01 05:5x）

- 修好 SelftestLoopMs（上一轮插入的循环块被误删，导致压测只跑了一次）
- 压测配置：A/B 每 8 秒重复注入一次 16 格改动，跑 2 小时 50 分钟（实测 ~2.8 分钟窗口，各 20 次）
- 结果：
  - 自测次数 **A=20 / B=20**
  - 异常 **0 / 0**
  - 崩溃快照 **0**
  - 两进程存活，内存 227 MB / 234 MB（起测时 198/199）
- 结论：默认配置（物件槽/INI 删键关闭）在反复编辑下稳定；上一轮的崩溃可归因于那两个实验特性
- 待观察：内存 3 分钟涨约 30 MB，是否泄漏需更长时间观察（可能是 FA2 自身的红移/撤销栈）
## 11 分钟内存趋势（2026-11-01 06:0x）

- 配置：默认（物件槽/INI 删键关闭），A/B 每 8 秒重复注入 16 格改动
- 内存（MB）：241 -> 242 -> 242 -> 243 -> 244 -> 244 -> 247 -> 248 -> 248 -> 249 -> 249
- 结论：**不是泄漏**，前 6 分钟缓慢增长后趋平（每分钟增量 1,0,1,1,0,3,1,0,1,0）
- 同时：异常 0 次、崩溃快照 0 个、两进程全程存活
- 数据留档：`evidence/memory_soak.csv`
## 真实 MO 地图实测：发现并修复 3 个问题（2026-11-01 06:4x）

地图：`D:\Mental Omega FS\MapsMO\Challenge\c_battlecity.map`（311 KB，88 个 INI 节，13 条触发）

- 问题 1：**INI 刷新风暴**。每应用一节就 `CFinalSunDlg::Update()`，85 节 = 85 次全量重绘，UI 线程被占满，客户端 3 秒连接超时后放弃
  - 修：改成打标记，每 tick 只刷新一次
- 问题 2：连接超时太短。大图加载时客户端阻塞，3 秒不够
  - 修：连接超时 3s -> 6s；发送限流 20 节/轮
- 问题 3（最关键）：**中途加入者收不到 INI/触发**。INI 只在"变化"时发送，后加入的人永远缺整份触发数据
  - 修：`PeerJoin` 时清空 `g_iniHash`，让 `ScanIni` 把全部节重发一遍
- 修复后实测：客户端应用 **65 个节**，包含 `Triggers / Events / Actions / Tags / TeamTypes / TaskForces / ScriptTypes / Waypoints`，**0 断线、0 异常**
## 触发「按 ID 合并」的算法层完成（2026-11-01 10:5x）

- 新增 `src/collab_inimerge.h`：INI 节**三方合并**（base/local/remote），策略：
  1. 单方改动 → 采用改动方
  2. 双方改同键成同值 → 采用
  3. 双方改同键成不同值 → 记冲突，默认本地优先
  4. 单方删除 → 删除生效
  5. 删/改冲突 → 记冲突、保留改动
  6. 双方各增新键 → 共存
- 单测新增 5 条（总 22 条断言），全部通过：
  `合并: 不同键互不影响 / 同键冲突检测 + 本地优先 / 单方删除生效 / 删/改冲突保留改动 / 双方新增键共存`
- 意义：这是"两人改不同触发不互相覆盖"的算法基础，且**不依赖 FA2**，可反复回归
- 尚未接入运行时：接入时要保存每节的 base 快照（目前没有），且要避开上次"直接在 Tick 里改 FA2 容器"导致的崩溃
## 触发按 ID 合并已接入运行时（2026-11-01 11:0x）

- 接入方式：接收端不再整节覆盖，改为 `MergeIniSection(base, local, remote)` 后只写有差异的键
- base 来源：每节首次出现时记录快照；发送成功后也更新
- 实测（MO 311KB 图，两实例各自写 [CollabTest]）：
  - B 日志 `selftest: wrote INI [CollabTest] By=Beta + Own_Beta`
  - 随后 `ini section changed: [CollabTest] entries=3`
  - **3 = By + Own_Alpha + Own_Beta** → 双方的"独有键"共存，没有互相覆盖
  - 异常 0、断线 0、两进程存活
- 冲突场景说明：本次两边新增的是"不同键"，所以没有 merge conflict；同键改不同值才会记冲突（单测已覆盖）
## 待办

- [ ] 一次性 UI：房间创建/加入 + 玩家列表（现在只能改 ini）
- [ ] 地图哈希校验（不同地图拒绝入房）
- [ ] S2：区块租约 + 地形/覆盖图增量同步 + 撤销隔离
- [ ] S3：对象/触发/INI 同步、中途加入快照、断线重连补齐
- [ ] 去掉 `draw peer=` 每秒诊断日志（或降到 debug 级）

## 复现步骤

```powershell
# 1. 编
powershell -File F:\Deepseek\DeepSeekHarness\fa2collab\build_fa2sp.cmd
# 2. 部署 + 双实例冒烟（含截图取证）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\smoke_test.ps1
```

配置：`FA2spCollab.ini`（放地编目录）
```ini
[Collab]
Enabled=1
Host=1            ; 主机 1 / 加入 0
HostAddress=127.0.0.1
Port=31415
Name=Alpha
```

## 物件渲染刷新打通（2026-11-01 13:30）

- 问题：物件（建筑/车/兵/飞机）写进了对端 `CellDatas`，但画面什么都不出现 —— 即"只差物件渲染"
- 根因（读源码确认）：
  - 等距视图每次重绘都会用 `CellDatas[..].Structure/Unit/Infantry/Aircraft` 里的**索引**
    去查 `CMapDataExt::BuildingRenderDatasFix / UnitDatasExt / AircraftDatasExt / CMapData::InfantryDatas`
  - 这些表只在「从 INI 重建字段」时生成；只改 CellDatas 索引 → 索引指向不存在的表项
  - `CMapData::BuildingDatas` 注释里写着 *Never being really used* —— 旧版 `RebuildObjectList()` 重建的正是它，所以完全没效果
  - 上一版"物件槽同步"崩溃也是同一个原因（索引越界）
- 修法：
  - 物件本身改走 **INI 节**（`[Structures]/[Units]/[Infantry]/[Aircraft]`，已在同步）
  - 收到这些节后不再手写列表，改为调用 FA2 自己的重建例程（FA2sp 已把入口 hook 成 Ext 版本）：
    `UpdateFieldStructureData(false)` / `UpdateFieldUnitData(false)` / `UpdateFieldInfantryData(false)` / `UpdateFieldAircraftData(false)`
  - 重建完 `RedrawWindow(RDW_INVALIDATE|RDW_UPDATENOW)`，把等距视图整块重画（重画时才按新索引 `Renderer::Building::Reload`）
  - 重建集中在每 tick 末尾做一次（`g_objDirty`），不会每节各刷一遍
  - 重建用独立 `__try/__except` 兜住：真崩了只关这一个特性，不断开会话
- 删掉的东西：`g_syncObjects`（物件槽直写）整套代码删除，`SyncObjects` 开关作废（留在 ini 里也无害）
- 证据（`objtest.ps1`，可复现）：
  - 空白图：A 在 B 光标旁放 `GAPOWR` → B `object lists rebuilt: buildings=1` + `LoadObjects loading: GAPOWR`
    → 前后截图对比，草地正中出现电厂（`evidence/objtest_client_before.png` / `objtest_client_after.png`）
  - 真实 MO 图 `c_battlecity.map`：由客户端注入 `CAFRMA` → 房主 `buildings=282 units=83` 重建、
    `LoadObjects loading: CAFRMA`，前后截图差异区域正好是那栋雪地砖房
    （`evidence/objtest_c_battlecity_client_before.png` / `_after.png`）
- 顺带验证：单位列表也走同一条路（`units=83` 是 MO 图里真实的车辆数）