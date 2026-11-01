## 删除同步转正喵

v0.4.1 起：**对方删掉的建筑 / 单位 / 触发，你这边也会真的消失**（此前只能增改，不能删）。

### 变更

- 删除改成「按三方合并结果删键」：只有 `本地有 → 合并结果没有` 的键才删，
  不再拿「远端整节」做镜像（旧做法会误删本地刚加的新键，破坏"双方各加新键共存"）
- 一边删、一边改同一条 → 记冲突并保留改动（不删），属预期保护
- 删除走 `GetEntities().find()` + `manual_erase(it)`，删完同一 tick 重建物件列表并重绘
- `IniDeleteSync` 默认值 **0 → 1**（配置模板同步更新）

### 验证

| 项目 | 结果 |
|---|---|
| 空白图：放建筑 → 对端出现 → 删除 → 对端消失 | 像素差 1195 / 1195，删除后与初始 **逐像素一致（diff=0）** |
| MO 真实图 `c_battlecity.map` | `buildings=[282,283,282]`，日志 `removed 1 keys (deleted by peer)` |
| 5 分钟「放/删」抖动压测 | 0 异常 / 0 崩溃快照 / 内存持平 |

### 安装

1. 备份地编目录里的 `FA2sp.dll`
2. 用包内 `FA2sp.dll` 覆盖
3. `FA2spCollab.ini` 放同目录：房主 `Host=1`，加入方 `Host=0` + `HostAddress=房主IP`
4. 两边打开**同一张图**

### 校验

- 包内 `FA2sp.dll` SHA-256：`3A88AFBEFBBF923E3B532A7FE16870E693AC244FF20BEB49A35BF46D130274C0`
- 源码即本仓库 `v0.4.1` tag；编译见 [BUILD.md](https://github.com/SwampyVoyage749/fa2collab/blob/main/BUILD.md)

### 已知限制

- 物件/删除同步约 0.5s 延迟（INI 扫描周期），地形是 200ms
- 每次物件变动整表重建，上千物件的图可能有一瞬顿挫
- 只支持局域网 / Radmin VPN 等虚拟局域网，无 NAT 穿透

许可证：AGPL-3.0（衍生自 [FA2sp](https://github.com/handama/FA2sp) / [FA2pp](https://github.com/secsome/FA2pp)）
