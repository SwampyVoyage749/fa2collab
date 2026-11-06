# 悬停信息增强（Cost / Sight / Speed）

> 这是 FA2sp Collab 对**上游 FA2sp（HDM Edition 1.5.3）源码**的改动，
> 不在 `src/FA2sp/Ext/CCollab` 里，需要按下面几处手动改上游文件后再编译。

## 为什么要改上游

原版悬停面板整块挂在：

```cpp
if (CIsoView::CurrentCommand->Command == 0x1B)   // 对象/指针工具（ESC）
```

也就是说：**只有在「对象/指针」工具下，悬停才会显示坐标、造价、血量这些行**。
用左侧「无 / 建筑 / 步兵」等工具时 `Command == 0`，面板整块不执行——这是原版行为，
不是 bug，但对做图的人不友好（一直在换工具）。

## 改动清单

### 1. `FA2sp\FA2sp.h`

在 `static bool InGameDisplay_RemapableOverlay;` 后面加：

```cpp
static bool InGameDisplay_GameStats;
```

### 2. `FA2sp\FA2sp.cpp`

定义（跟其它 `ExtConfigs::` 布尔放一起）：

```cpp
bool ExtConfigs::InGameDisplay_GameStats;
```

读配置（跟其它 `ExtConfigs::X = CINI::FAData->GetBool(...)` 放一起）：

```cpp
ExtConfigs::InGameDisplay_GameStats = CINI::FAData->GetBool("ExtConfigs", "InGameDisplay.GameStats", true);
```

### 3. `FA2sp\Ext\CIsoView\Body.h`

在 `static bool DrawMoneyOnMap;` 后面加：

```cpp
static bool HoverInfoAlways;   // Ctrl+H：任何工具下都显示悬停信息
```

### 4. `FA2sp\Ext\CIsoView\Body.cpp`

```cpp
bool CIsoViewExt::HoverInfoAlways = false;
```

### 5. `FA2sp\Ext\CIsoView\Body.OnMouseMove.cpp`

**(a) 顶部**（`constexpr float cellLength` 之后）加一个算「造价 / 视野 / 速度」的小函数：

```cpp
static FString BuildGameStatsLine(MultimapHelper& mmh, const ppmfc::CString& typeID, bool isBuilding)
{
    if (!ExtConfigs::InGameDisplay_GameStats)
        return FString();

    int cost = mmh.GetInteger(typeID, "Cost");
    int sight = (int)(mmh.GetSingle(typeID, "Sight") + 0.5f);

    FString line;
    if (isBuilding)
        line.Format("Cost: %d, Sight: %d", cost, sight);
    else
        line.Format("Cost: %d, Sight: %d, Speed: %d", cost, sight,
                    (int)(mmh.GetSingle(typeID, "Speed") + 0.5f));
    return line;
}
```

（正式版用 `Translations::TranslateOrDefault("ObjectInfo.GameStats.Building"/"Unit", ...)` 包一层，方便翻译）

**(b) 面板总开关**：`if (CIsoView::CurrentCommand->Command == 0x1B)` 改成：

```cpp
if (CIsoView::CurrentCommand->Command == 0x1B || CIsoViewExt::HoverInfoAlways)
```

面板开头加 Ctrl+H 轮询（放在这个 if 之前）：

```cpp
{
    static bool s_prevDown = false;
    const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
                   && (GetAsyncKeyState('H') & 0x8000) != 0;
    if (down && !s_prevDown)
        CIsoViewExt::HoverInfoAlways = !CIsoViewExt::HoverInfoAlways;
    s_prevDown = down;
}
```

**(c) 四类物件的显示条件**（Infantry / Vehicle / Aircraft / Building 各两处：外层判断 + 内层绘制）：

```cpp
... || bDrawRange || CIsoViewExt::HoverInfoAlways)
```

**(d) 每类物件最后多画一行**（放在原版最后一行之后，DX 与 GDI 两个分支都要加）：

```cpp
FString stats = BuildGameStatsLine(mmh, object.TypeID, false);   // 建筑传 true
if (stats.GetLength() > 0)
{
    CIsoViewExt::TextOutDirectX(drawX + tab, drawY + lineHeight * i, stats, fontSize);
    i++;
}
```

## 行为

| 情况 | 结果 |
| --- | --- |
| 未改 ini（默认） | 悬停面板只在对象工具（ESC）下出现，但**多了 Cost/Sight/Speed 一行** |
| 按 Ctrl+H | 任何工具下悬停都显示面板（本机临时状态，不写 ini） |
| `InGameDisplay.GameStats=false` | 完全不显示 Cost/Sight/Speed（本机也不画） |

## 已验证

- 车辆：`Cost: 900, Sight: 8, Speed: 6`（犀牛重型坦克 HTNK）
- 建筑：`Cost: ..., Sight: ...`（无 Speed）

---

# 追加：预览模式（Ctrl+P，v0.5.1 草案）

目标：按热键让整张图的建筑按 art.ini 播放动画，替代「悬停才动」的做法。

## 改动点

1. `Ext/CIsoView/Body.h` / `Body.cpp`：新增 `static bool CIsoViewExt::PreviewMode;`（默认 false）。
2. `Ext/CIsoView/Body.DrawObjects.cpp`：在结构绘制处插入
   ```cpp
   if (CIsoViewExt::PreviewMode || ExtConfigs::InGameDisplay_AnimPreview) {
       // 首次进入时 SetTimer(view, 0xFA26, 40) 触发连续重绘
       // 每个建筑类型限流 1 秒调一次 CLoadingExt::PreloadAnimPreview(pType->ID)
       auto pAnimFrame = CLoadingExt::GetAnimPreviewFrame(pType->ID, (int)GetTickCount());
       if (ImageDataClassSafe::IsValidImage(pAnimFrame)) {
           const int ax = x1 - pAnimFrame->FullWidth / 2;
           const int ay = y1 - pAnimFrame->FullHeight / 2;
           if (ExtConfigs::DirectXRendering)
               CIsoViewExt::DirectXNormal(ax, ay, pAnimFrame, pAnimFrame->pPalette, 1.0f, objRender.HouseColor, -1, false);
           else
               CIsoViewExt::BlitSHPTransparent(pThis, ddsd.lpSurface, window, boundary,
                   ax, ay, pAnimFrame, pAnimFrame->pPalette, 255, objRender.HouseColor);
       }
   }
   ```
   **注意**：绝不要把这些帧 push 进 `AlphaImagesToDraw` —— 那条通道是把像素值当亮度系数乘到背景上（用于光照/变暗），放颜色帧会得到整块发黑的图。
3. `Ext/CFinalSunDlg/Body.cpp`：`case WM_TIMER:` 加 `0xFA26` 分支做重绘；`case WM_KEYDOWN:` 加 Ctrl+P 切换（`GetKeyState(VK_CONTROL)`）。
4. `Ext/CIsoView/Body.cpp`：`CIsoViewExt::PreTranslateMessageExt` 里也挂一份 Ctrl+P 与 `WM_TIMER 0xFA26` 处理（键盘消息实际落在 iso view 上）。

## 已知不足

- 动画帧按格子居中绘制，未接 art 的 `ActiveAnimX/Y` 偏移。
- 只处理建筑；单位/步兵/飞行器动画未做。
- 未接地图 `[Lighting]` 光照与受损/朝向状态。
- 开关只做全局，不区分单栋建筑。
## 追加修复（2026-11-04 下午）

1. **定时器被误杀**：`CFinalSunDlg`/`CIsoView` 里的 `WM_TIMER 0xFA26` 与 Ctrl+P 切换只认 `CIsoViewExt::PreviewMode`，导致只开 ini 开关 `InGameDisplay.AnimPreview` 时定时器立刻被 KillTimer，动画不动。两处判断统一改为 `CIsoViewExt::PreviewMode || ExtConfigs::InGameDisplay_AnimPreview`。
2. **建筑动画被自身遮挡**：`DrawBuilding` 末尾才调用 `DrawPreviewAnimOverlay(pType->ID, x1, y1, ...)`，之前插在建筑本体绘制之前会被覆盖。
3. **单位/步兵接入**：`DrawPreviewAnimOverlay()` 抽成公共函数后，在 `DrawUnit`（`draw()` 之后，y 用 `y + 15 - (HoveringUnit ? 10 : 0) - bridge_offset`）与 `DrawInfantry`（`draw()` 之后，用 `x1, y1`）各调用一次。
4. **增量编译陷阱**：改完源码后 msbuild 认为 up-to-date（obj 时间比源文件新）而不重编，需 `(Get-Item $f).LastWriteTime = Get-Date` 触碰后再编。
## 性能实测（2026-11-04 晚，c_battlecity.map 1920x1080 窗口）

| 场景 | CPU | 内存 |
|---|---|---|
| 预览关（基线） | 1.2% | 240.9 MB |
| 预览开，40ms 重绘 | 31.7% | 245.6 MB |
| 预览开，80ms 重绘 | 17.0% | 243.8 MB |
| 预览开，100ms 重绘 | **14.3%** | 245.4 → 245.3 MB（无泄漏） |

据此把重绘定时器从 40ms 调到 **100ms**（10 fps），CPU 降到 15% 以下，动画仍连续（连续帧像素差非空）。
## 单位/步兵动画验证（2026-11-04 晚）

在 `c_battlecity.map` 上打开预览，日志出现（节选）：

```
[PreviewOverlay] type=HTNK frames=none
[PreviewOverlay] type=TTNK frames=none
[PreviewOverlay] type=TRUCKB frames=none
[PreviewOverlay] type=NANRCT frames=yes
[PreviewOverlay] type=TESLA  frames=yes
```

结论：**单位/步兵确实走到了叠加层**（代码接入正确），但 MO 的车辆 art 没有 `Animation` 段，所以没有帧可播；能看到动画的只有 art 里定义了动画的建筑。诊断日志每种类型只记一次、上限 200 种，保留以便排查「为什么这个物件不动」。
## 预览动画偏移（2026-11-06）

`DrawPreviewAnimOverlay` 在绘制前调用 `GetPreviewAnimOffset(typeID, offX, offY)`：
读 art.ini 的 `ActiveAnimX` / `ActiveAnimY`（`CINI::Art().GetInteger`，结果按类型缓存），
把偏移加到动画帧的 x/y 上。

注意：`CINI::Art()` 返回 `CINI&`，要用 `.`（`->` 会报 C2819）；`CINI::CurrentDocument` 是
`reference<CINI,…>`，反而只能用 `->` —— 这两个在同一个函数里并存，别写混。

实测（MO，c_battlecity）：抽样绘制到的建筑/车辆类型 `ActiveAnimX/Y` 全是 0，所以画面没变化；
这项改动是给「art 里真的写了偏移」的图用的，默认情况等于不生效。
## 地图静态自检（C 项第一步，2026-11-06）

`Ctrl+Shift+C` 跑一次地图自检，结果写 `FA2sp.log` 的 `[MapCheck]` 行，并在弹窗里显示前 12 条。

检查项（第一版）：

1. `[Tags]` 里的 Tag 指向了不存在的 Trigger（第三段是 trigger id）
2. `[Triggers]` 里的 Trigger 没有对应 `[Events]`（永远不触发）
3. `[Triggers]` 里的 Trigger 没有对应 `[Actions]`（触发了也不做事）
4. `[Events]` / `[Actions]` 条目里数量字段为 0

实现位置：`Ext\CIsoView\Body.cpp` 的 `CIsoViewExt::RunMapSelfCheck(bool bShowDialog)`（声明在 `Body.h`），
热键挂在 `CIsoViewExt::PreTranslateMessageExt` 的 `WM_KEYDOWN`（Ctrl+Shift+C）。

另有 `ExtConfigs::MapSelfCheckOnLoad`（默认 0）：设 1 时每次开图自动跑一遍、只写日志不弹窗，方便批量排查。

验证（2026-11-06）：`c_battlecity.map` → `[MapCheck] done`（0 问题）；
把副本的 `[Tags]` 插入 `99999999=0,Broken tag,DEADBEEF` → `[MapCheck] Tag 99999999 -> Trigger DEADBEEF 不存在`。