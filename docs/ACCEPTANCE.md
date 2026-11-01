# 验收清单（一页版）

> 项目：FA2SP HDM Edition 1.5.3 局域网实时协同编辑
> 版本：v0.4.1（2026-11-01，物件渲染刷新 + 删除同步）
> 交付包：`FA2spCollab_v0.3.zip`

---

## 一、交付物

| 文件 | 说明 |
|---|---|
| `FA2sp.dll` | 替换用的主程序（约 9.6 MB，32 位，已含协同模块） |
| `FA2spCollab.ini` | 配置模板（默认安全配置） |
| `README.md` | 安装、配置、自查清单 |
| `HANDOFF.md` | 交接文档（环境、命令、6 个坑、下一步） |
| `EVIDENCE.md` | 证据索引（每项功能对应哪条日志/截图） |

仓库侧另有：`src/`（联机内核）、`tests/`（22 条断言）、`verify.ps1`（一键回归 11 项）、`start_two_instances.cmd`（单机双开）。

---

## 二、安装（3 步）

1. 备份地编目录下原有的 `FA2sp.dll`
2. 把包里的 `FA2sp.dll` 覆盖进去
3. 把 `FA2spCollab.ini` 放同目录，按需改：
   ```ini
   [Collab]
   Enabled=1
   Host=1              ; 房主 1，加入方 0
   HostAddress=127.0.0.1   ; 加入方填房主局域网 IP
   Port=31415
   Name=你的昵称
   ```

> 用 Syringe 启动地编，两份实例打开**同一张图**（内容一致，哈希才会相同）。

---

## 三、验收项（逐条勾）

| # | 验收内容 | 怎么验 | 期望 |
|---|---|---|---|
| 1 | 程序能加载 | 看 `FA2sp.log` | `Applying FA2sp 1.6.3, HDM Edition 1.5.3` |
| 2 | 协同模块启动 | 同上 | `[Collab] started (timerId=1)` |
| 3 | 同图会话建立 | 双方日志 | `map ready: ... hash=0x...`（两边相同） |
| 4 | 角色正确 | 日志 | 房主 `hosting on port 31415`；加入方 `joining <ip>:31415` |
| 5 | 互相可见 | 日志 + 画面 | `peer joined: id=N name=X`；左上 HUD + 玩家列表 |
| 6 | 光标同步 | 对方移动鼠标 | 自己屏幕出现对方的青色圆点 + 名字 |
| 7 | 选区同步 | 对方框选地块 | 自己屏幕出现橙色虚线框 |
| 8 | 地形同步 | 对方涂地 | 200ms 内跟着变（日志 `applied remote chunk ... cells=N`） |
| 9 | 冲突拦截 | 对方先涂一块，你立刻涂同一块 | 你这边出现红框；日志 `chunk (x,y) owned by player N` |
| 10 | 被占块还原 | 同上 | 日志 `protected chunk (x,y): restored N cells` |
| 11 | 触发/INI 同步 | 对方改触发并确定 | 日志 `applied ini section [Triggers]`，触发器列表变化 |
| 12 | 中途加入 | 对方改一堆后才进来 | 新实例日志出现一批 `applied ...`，画面与对方一致 |
| 13 | 断线重连 | 加网线/断网再恢复 | 日志 `disconnected, retrying` 后重新 `peer joined` |
| 14 | 稳定性 | 连续编辑 5 分钟 | 无崩溃；异常 0 |
| 15 | 物件同步 | 对方放一个建筑/车/兵 | 你这边画面里出现同一物件（日志 `object lists rebuilt: buildings=N`） |
| 16 | 物件不崩 | 对方反复放/换物件 5 分钟 | 无崩溃、无 `exception in Tick` |
| 17 | 删除同步 | 对方删掉一个建筑/触发 | 你这边也消失（日志 `removed N keys (deleted by peer)`） |

---

## 四、已知限制（务必知情）

| 限制 | 影响 | 规避 |
|---|---|---|
| 物件同步有 ~0.5s 延迟 | 对方放的车/兵/建筑半秒后才出现 | 正常（INI 扫描周期 500ms） |
| 每次物件改动整表重建 | 极大图（上千物件）可能有百毫秒级顿挫 | 分工减少同时放物件 |
| 触发按 ID 合并 | 两边改**同一触发同一字段**会后写覆盖（有冲突日志） | 分工避免改同一条 |
| 整节 LWW 回退 | base 未建立时退回后写覆盖 | 正常流程下 base 会建立 |
| 房间 UI 精简 | 只有 `Ctrl+Alt+C` 的设置窗（名字/IP/端口/主客） | — |

**未验证**：同键冲突的真机复现（构造该场景的自动测试会让 FA2 崩溃）。

---

## 五、复现命令

```powershell
# 一键回归（11 项断言，约 90 秒）
powershell -ExecutionPolicy Bypass -File F:\Deepseek\DeepSeekHarness\fa2collab\verify.ps1

# 单机双开（把地图拖到脚本上即可）
F:\Deepseek\DeepSeekHarness\fa2collab\start_two_instances.cmd "D:\path\to\map.map"

# 编译（只在改代码时需要）
& 'D:\VSBuildTools\MSBuild\Current\Bin\MSBuild.exe' 'C:\FA2Build\FA2sp.sln' /nologo /m:8 /v:m `
  /p:Configuration=Release /p:Platform=x86 /p:WindowsTargetPlatformVersion=10.0.26100.0
```

---

## 六、出问题的取证清单

1. 两个地编目录各自的 `FA2sp.log`
2. 若崩溃：`debug\snapshot-*\except.txt`
3. 截图（HUD / 红框 / 光标）
4. 一句话复现描述

---

## 七、验收结论（填写）

- [ ] 全部 14 项通过 → 可交付使用
- [ ] 部分失败 → 附失败项编号与日志

验收人：____________　日期：____________
