# 同步吞吐修复（2026-11-09，v0.5.2）

## 问题

大图（200×200，169 个 chunk）首次联机时，加入方收不完全量快照：
- 主机每 tick 只发 8 块 → 客户端 30 秒超时被踢（`peer left`），随后放/删建筑都同步不过去；
- 客户端本地 `deltest` 稳定 5 项失败（对端 removed=0、rebuild buildings=[]、三组像素差）；
- 早期还有「每收一块就全量 `CFinalSunDlg::Instance->Update()`」导致 UI 线程被堵的问题。

## 改动（均在 `src/FA2sp/Ext/CCollab/`）

| 位置 | 改动 |
|---|---|
| `Body.cpp` `CCollabExt::Tick()` 快照发送 | `sent < 8` → `sent < 4096`（一次 tick 发完整图，200×200 约 169 条消息 / 845KB） |
| `Body.cpp` `ApplyRemoteDelta` 影子状态 | `std::map<uint32_t, std::vector<CellEdit>>` → `std::map<uint32_t, std::map<int, CellEdit>>`，去掉每格 `std::find_if` 线性扫描（原 O(n²)，快照期累积 4 万条时明显卡顿） |
| `Body.cpp` tick 重绘 | 地形增量不再直接 `Update()`，改为对 iso view `InvalidateRect`（异步重绘，避免整图快照落地时堵住 UI 线程） |
| `Body.cpp` `ScanIni` | 每轮发送上限 20 → 64 节（新人加入要整份重发，30 多个节时分两轮才能发完） |
| `core/collab_session.cpp` | `kTimeoutMs` 30000 → 60000（大图首同步留出余量） |

## 验证

`deltest.ps1`（presets_tem 200×200）：
- 修复前：5 项失败（对端收不到删除、建筑不复原、画面不出现）；
- 修复后：**只剩 2 项截图时机问题**——「删除后画面回退 / 与初始一致」抓图早于重绘；
- 测试脚本同时修正：截图前先等对端日志出现 `removed 1 keys`，再 `Front()` + 1.2s 后抓图。

## 注意

- 这些改动只影响同步时序，不改协议字段；但 `kTimeoutMs` 变化会随 DLL 生效，双方仍必须使用同一构建。
- 诊断用的临时日志（`heartbeat`、`delta enter/dropped`、`g_deltaRecv`）已全部删除。

## 追加（同日）：超大图的每 tick 保护

一次 tick 发完整个快照，对 400×400 这类地图意味着上万条消息、几千万个 CellEdit，分配与入队会在单帧里占住线程。
现在给推送循环加了格子预算 sentCells < 1200000（≈293 个 chunk/次），200×200 图仍在一次 tick 内发完，更大的图分几次、每帧最多约 1.2M 格。
## 2026-11-12 复验结果（deltest）

| 断言 | 结果 |
|---|---|
| 房主放入建筑（key=999 写入） | PASS |
| 房主删除该建筑 | PASS |
| 对端收到删除（removed 1 keys） | PASS |
| 对端建筑数 1 → 0（rebuild buildings=[1,0]） | PASS |
| 两边存活 | PASS |
| 画面像素差三项 | FAIL（diff=105 / 0 / 105） |

说明：数据链路（INI 节到达 → 物件列表重建 → 删除删除）已全通；像素差三项失败的原因是
两侧视口位置不同——建筑落在加入方视野外，PrintWindow 抓到的画面里根本没有它。
此前的轮次里同一断言曾经 PASS（diff=1037/986），说明绘制刷新本身没问题。
若要稳定复验像素，需要脚本先把两侧视口对齐到同一格。

复验所用 DLL：9,610,752 B / 2026-11-12 00:13:01，SHA256 前 32 位 DA5418539757478E1F31B1B542D758CF。
