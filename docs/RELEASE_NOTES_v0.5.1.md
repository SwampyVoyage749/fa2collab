# v0.5.1 — 预览模式：让建筑在地编里动起来

> [!IMPORTANT]
> **本插件完全由 DeepSeek-V4.1-Flash 生成**

## 这一版加了什么

按 **`Ctrl+P`** 开关「预览模式」：地图里所有在 art 里写了 `Animation` 的建筑，会按帧真正动起来（10 fps），不用开游戏就能看效果。

- 开预览时，如果地图带 `[Lighting]` 段，**自动把光照切到「正常」**；关预览时恢复你原来的光照设置
- 也可以不按热键：`Options` 里勾 `InGameDisplay.AnimPreview` 让它常开
- 预览期间 CPU 大约 +13%（实测 1.2% → 14.3%），内存无增长
- 重绘间隔 100ms（原来 40ms 时 CPU 要 31.7%）

## 修掉的坑

- 动画帧原先被塞进光照通道（`AlphaImagesToDraw`），整块发黑 —— 改为正常透明绘制
- 用 ini 开关打开时定时器被误杀、动画被建筑本体盖住 —— 已修

## 已知不足

- 只有**建筑**会动；车辆/步兵在 MO 的 art 里没有 `Animation` 段（日志 `frames=none`），所以没帧可播
- 动画按格子居中，没接 `ActiveAnimX/Y` 偏移，个别建筑可能偏一点
- 未接受损/朝向/昼夜自动变化

## 其它（0.5.0 起就有）

- 悬停显示 `Cost / Sight / Speed`（`Ctrl+H` 任意工具下常显）
- 协议 v2 握手、增量按格合并、消息校验、Bye 清理等同步内核修复

## 下载

- `FA2spCollab_v0.5.1.zip`（2.4 MB，含 `FA2sp.dll` + `FA2spCollab.ini` + 说明 + `setup_collab.cmd`）
- 安装：备份地编目录的 `FA2sp.dll` → 覆盖 → 把 `FA2spCollab.ini` 放同目录 → 两边开同一张图
- 两边版本必须一致（协议已变）