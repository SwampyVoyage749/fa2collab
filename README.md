# FA2sp Collab · 红警地编联机协作

[![Release](https://img.shields.io/github/v/release/SwampyVoyage749/fa2collab?style=flat-square&label=release)](https://github.com/SwampyVoyage749/fa2collab/releases)
[![Downloads](https://img.shields.io/github/downloads/SwampyVoyage749/fa2collab/total?style=flat-square&label=downloads)](https://github.com/SwampyVoyage749/fa2collab/releases)
[![License](https://img.shields.io/github/license/SwampyVoyage749/fa2collab?style=flat-square&label=license)](LICENSE)
[![FA2sp](https://img.shields.io/badge/base-FA2sp%20HDM%201.5.3-blue?style=flat-square)](https://github.com/handama/FA2sp)

**[⬇ 下载最新版（含编译好的 DLL，解压即用）](https://github.com/SwampyVoyage749/fa2collab/releases/latest)**

> [!IMPORTANT]
> **本插件完全由 DeepSeek-V4.1-Flash 生成**
> 从局域网同步内核、FA2 内部渲染表重建，到全部测试脚本与文档，均由它完成。

让 **FinalAlert 2 SP（HDM Edition）** 支持**两个人同时编辑同一张地图**的联机插件：
地形、矿石、单位、建筑、触发/INI 全部实时互通，谁在改哪一块看得见，抢同一块会被拦住。

做图的可以各自负责一片区域；写 INI 的可以一个人调触发、另一个人摆房屋，不用再"你改完发我我改完发你"。

![联机界面：左上角状态条 + 玩家列表 + 对方光标](evidence/hud_crop.png)
![物件同步：对方放的电厂出现在自己画面里](evidence/objtest_presets_tem_host_after.png)

## 特性

| 能力 | 状态 |
|---|---|
| 同图校验（哈希不一致拒绝连接） | ✅ |
| 光标 / 名字 / 工具 / 笔刷 / 框选 同步 | ✅ |
| 地形 + 覆盖图（矿石等）**逐格增量**同步（约 200ms） | ✅ |
| **物件同步**：建筑 / 车辆 / 步兵 / 飞机（约 0.5s，含渲染刷新） | ✅ v0.4 |
| **删除同步**：删建筑 / 单位 / 触发 → 对端同步删除 | ✅ v0.4.1（默认开） |
| 地图内嵌 INI 节同步（Triggers / Events / Actions / TeamTypes / TaskForces / ScriptTypes / Waypoints / Houses…） | ✅ |
| 触发按 ID 三方合并（不同键共存，同键冲突记日志） | ✅ |
| 16×16 区块租约：别人占着就改不动，硬改会被还原（撤销也一样） | ✅ |
| 冲突红框 / 状态条 / 玩家列表 | ✅ |
| 中途加入自动补全量快照（地形 + INI + 物件） | ✅ |
| 断线自动重连 | ✅ |
| 房间设置窗（`Ctrl+Alt+C`） | ✅ |

| 公网/NAT 穿透 | ❌ 只支持局域网或虚拟局域网（Radmin VPN / UU 等） |

## 给"只用"的人

不需要编译。直接下载 **[FA2spCollab_v0.4.1.zip](https://github.com/SwampyVoyage749/fa2collab/releases/download/v0.4.1/FA2spCollab_v0.4.1.zip)**（或看 [Releases 页](https://github.com/SwampyVoyage749/fa2collab/releases)）：

1. 备份地编目录里的 `FA2sp.dll`
2. 用包里的 `FA2sp.dll` 覆盖
3. 把 `FA2spCollab.ini` 放进同一目录，按需改 `Host / HostAddress / Port / Name`
4. 两边打开**同一张图**（文件必须完全一致）

一次配置可以双击 `setup_collab.cmd`，它会自动找目录、备份、写 ini。
详细说明见 [`docs/使用说明.md`](docs/使用说明.md)（含异地联机、防火墙、排查表）。

## 给"要改"的人

```
.
├─ src/FA2sp/Ext/CCollab/     # 联机模组（放进上游 FA2sp 源码树同路径即可编译）
│  ├─ Body.cpp                # 主逻辑：定时器、快照、增量、INI、物件重建、覆盖层绘制
│  └─ core/                   # 会话/传输/协议/三方合并（纯 C++，可脱离 FA2 单测）
├─ kernel/                    # 同一份联机内核 + 单元测试（独立编译，不依赖 FA2）
├─ scripts/                   # 一键回归、物件刷新验证、压测、双开、配置助手
├─ config/FA2spCollab.ini     # 配置模板
└─ docs/                      # 使用说明、验收清单、证据索引、踩坑记录、进度
```

编译、部署与测试见 **[BUILD.md](BUILD.md)**。三个最容易踩的坑先记住：

1. **新源码必须存 GBK**（上游工程用 `/source-charset:gbk`，UTF-8 会让中文注释吃掉 `#pragma once`）
2. **改 `.vcxproj` 会触发全量重编**（约 40 分钟），只改 `.cpp` 是增量（2~3 分钟）
3. **物件只能靠 INI 节同步 + `UpdateField*Data(false)` 重建渲染表**；直接改 `CellDatas` 的物件槽会索引越界崩溃
   （`CMapData::BuildingDatas` 是死字段，往那儿写不会有任何画面变化）

## 验证方式（不是"看起来能用"）

所有结论都有可复现脚本与日志：

```powershell
# 14 项一键回归：编译 + 内核单测 + 双实例联机 + 日志断言（约 2 分钟）
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1

# 物件同步：注入真实建筑 -> 对方重建对象列表 -> 前后截图取像素差（约 90 秒）
powershell -ExecutionPolicy Bypass -File scripts\objtest.ps1 -Map "D:\path\to\map.map" -Injector client

# 物件重建压测：每 8 秒变更一次物件，观察崩溃/异常/内存
powershell -ExecutionPolicy Bypass -File scripts\soak_objects.ps1 -Minutes 5
```

最近一次结果：14 项全 PASS；空白图与 MO 真实图均能看到对方放的建筑；
37 次物件注入压测 **0 异常 / 0 崩溃快照 / 内存持平**。详见 [`docs/EVIDENCE.md`](docs/EVIDENCE.md)。

## 遇到问题 / 想加功能

- **出了 Bug**：提 [Issue（会自动带上模板）](https://github.com/SwampyVoyage749/fa2collab/issues/new?template=bug_report.yml)。
  模板会要「两边 `FA2sp.log` + 地图尺寸来源 + 复现步骤 + 是否崩过」——这几样齐全基本当天能定位
- **想要新功能**：[功能建议模板](https://github.com/SwampyVoyage749/fa2collab/issues/new?template=feature_request.yml)，
  说清你是**做图的**还是**写 INI 的**、现在怎么绕过去，比"希望支持 XX"有用得多
- **只想知道怎么用**：先看 [`docs/使用说明.md`](docs/使用说明.md)（安装 / 异地联机 / 防火墙 / 排查表）

## 许可

本项目修改自 [FA2sp](https://github.com/handama/FA2sp)（AGPL-3.0）与 [FA2pp](https://github.com/secsome/FA2pp)（AGPL-3.0），
按 **AGPL-3.0** 发布，见 [LICENSE](LICENSE)。

- 仓库只包含我们自己写的协同模块与测试，**不含**上游完整源码；编译请按 [BUILD.md](BUILD.md) 取对应版本
- 发布包里的 `FA2sp.dll` 是上述组合的编译产物，源码即本仓库 + 上游同版本源码
- 不含 EA 的游戏资源；使用者需自备正版《命令与征服：红色警戒 2 / 尤里的复仇》
