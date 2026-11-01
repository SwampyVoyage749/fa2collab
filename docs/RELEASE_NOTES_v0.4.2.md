> [!IMPORTANT]
> **本插件完全由 DeepSeek-V4.1-Flash 生成**
> 从局域网同步内核、FA2 内部渲染表重建，到全部测试脚本与文档，均由它完成。

## 修掉联机时的崩溃（非法光标坐标）

这个版本修的是**真实联机时 FA2 闪退**：对方鼠标移出地图/视野外时，会把无效格子坐标（例如 `(-1,-1)`）
通过网络发过来；渲染对方光标时我们直接把它交给 FA2 的 `CIsoView::MapCoord2ScreenCoord`，
而它内部 `GetCellAt(X,Y)` **没有任何越界检查** → 访问违例，崩溃地址固定为 `0045E8F0`
（事件日志表现为 `FinalAlert2YR.dat` 的 `0xc0000005`，随后显卡驱动 `nvoglv32.dll` `0xc0000409`）。

### 修复

- 所有把坐标喂给 FA2 的地方（对方光标 / 对方选区 / 冲突红框 / 本机广播）先做内存安全校验
  `0 <= X,Y < stride 且 index < CellDataCount`，不合法就**忽略这一帧**，不再换算
- 本机鼠标不在有效格子上时不再广播 `(-1,-1)`，改发"无效标记"让对方忽略
- 日志：`[Collab] ignore invalid cursor from id=1 cell=(-1,-1)`（5 秒最多一条）
- 新增回归测试 `scripts/crashtest.ps1`：故意让一端广播 `(-1,-1)` 与 `(30000,30000)`，
  另一端必须忽略且不崩（进程存活 / 无崩溃快照 / 无 `exception in Tick`）

### 验证

| 项目 | 结果 |
|---|---|
| `crashtest.ps1` 非法坐标回归 | 5/5 PASS（对端存活、无快照、无异常） |
| `verify.ps1` 一键回归 | 14/14 PASS |
| `deltest.ps1` 删除同步 | 空白图 8/8 PASS |

### 安装

1. 备份地编目录里的 `FA2sp.dll`
2. 用包内 `FA2sp.dll` 覆盖（**两边都要换，版本必须一致**）
3. `FA2spCollab.ini` 放同目录：房主 `Host=1`，加入方 `Host=0` + `HostAddress=房主IP`
4. 两边打开**同一张图**

### 校验

- 包内 `FA2sp.dll` SHA-256：`364E7011BA756737D390C59BDB54BB2153968C369026896D33E8EB89957C9D2E`
- 源码即本仓库 `v0.4.2` tag；编译见 [BUILD.md](https://github.com/SwampyVoyage749/fa2collab/blob/main/BUILD.md)

### 已知限制

- 物件/删除同步约 0.5s 延迟（INI 扫描周期），地形是 200ms
- 只支持局域网 / Radmin VPN 等虚拟局域网，无 NAT 穿透

许可证：AGPL-3.0（衍生自 [FA2sp](https://github.com/handama/FA2sp) / [FA2pp](https://github.com/secsome/FA2pp)）
