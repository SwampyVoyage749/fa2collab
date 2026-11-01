## 物件同步终于画出来了喵

v0.4.0 的核心：**对方放的建筑 / 车 / 兵会真的出现在你的画面里**。

### 下载

| 文件 | 用途 |
|---|---|
| `FA2spCollab_v0.4.0.zip` | **给玩家/做图的**：编译好的 `FA2sp.dll` + 配置模板 + 说明，解压覆盖即可 |
| Source code (zip / tar.gz) | 本版本源码快照（GitHub 自动生成）＝ 本仓库 `v0.4.0` tag |

> 源码里**不含**上游 FA2sp 的完整代码。重新编译请按 [BUILD.md](https://github.com/SwampyVoyage749/fa2collab/blob/main/BUILD.md) 取上游
> FA2sp **1.6.3（HDM Edition 1.5.3）** 源码，再把本仓库 `src/FA2sp/Ext/CCollab/` 放进去。

### 修复了什么

- 旧实现直接写 `CellDatas` 的物件槽，但等距视图是用这些槽里的**索引**去查
  `BuildingRenderDatasFix / UnitDatasExt / AircraftDatasExt / InfantryDatas`——
  这几张表不重建，索引就指向不存在的表项：轻则什么都不显示，重则越界崩溃（v0.3 压测崩的就是这个）
- 现在：物件本体走 INI 节（`[Structures]/[Units]/[Infantry]/[Aircraft]`）→ 收到后调用 FA2 自己的
  `UpdateFieldStructureData/UnitData/InfantryData/AircraftData(false)` 重建渲染表 → 整屏重绘
- 每 tick 最多重建一次；重建出异常只关闭该特性（日志 `object list rebuild crashed`），**不会断开联机**
- 删掉了作废的 `SyncObjects` 开关

### 这次的实测数据

| 项目 | 结果 |
|---|---|
| `verify.ps1` 一键回归 | **14/14 PASS** |
| 空白图物件同步 | `GAPOWR` 电厂出现在对端画面（前后截图像素差） |
| MO 真实图 `c_battlecity.map` | `buildings=282→283`、`units=83`，`CAFRMA` 出现在对端画面 |
| 物件重建压测 5 分钟 | 37 次注入 / 重建 39 次 / **异常 0 / 崩溃快照 0 / 内存持平** |

### 安装

1. 备份地编目录里的 `FA2sp.dll`
2. 用包内 `FA2sp.dll` 覆盖
3. `FA2spCollab.ini` 放同目录：房主 `Host=1`，加入方 `Host=0` + `HostAddress=房主IP`
4. 两边打开**同一张图**

也可以双击包内 `setup_collab.cmd` 自动配置。

### 校验

- 包内 `FA2sp.dll` SHA-256：`EB5ED5FB142B6D403D4BC2D4F9FD1E4719C7A400425CF56B7ABCCEA39BA9F7BC`
- 源码即本仓库 `v0.4.0` tag；编译方式见 [BUILD.md](https://github.com/SwampyVoyage749/fa2collab/blob/main/BUILD.md)

### 已知限制

- 物件同步约 0.5s 延迟（INI 扫描周期），地形是 200ms
- 每次物件变动整表重建，上千物件的图可能有一瞬顿挫
- INI 删键同步仍是实验开关（默认关）
- 只支持局域网 / Radmin VPN 等虚拟局域网，无 NAT 穿透

许可证：AGPL-3.0（衍生自 [FA2sp](https://github.com/handama/FA2sp) / [FA2pp](https://github.com/secsome/FA2pp)）
