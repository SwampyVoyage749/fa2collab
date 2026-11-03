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
