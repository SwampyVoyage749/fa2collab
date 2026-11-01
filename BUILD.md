# 编译与测试

> 目标：把本仓库的联机模组塞进上游 FA2sp 源码树，编出 32 位 `FA2sp.dll`，再用脚本回归验证。
> 全程不需要改地图编辑器本身的其它代码。

## 0. 环境

| 依赖 | 说明 |
|---|---|
| Windows 10/11 x64 | 只在 Win10 19045 上实测过 |
| Visual Studio 2022 Build Tools | 需要 **MSVC v143 + C++20 + x86 工具链** + Windows SDK 10 |
| 上游源码 | [handama/FA2sp](https://github.com/handama/FA2sp) **RELEASE 1.6.3（HDM Edition 1.5.3）** 的源码 zip |
| 子模块 | `FA2pp`(secsome/FA2pp)、`MFC42`(secsome/MFC42)、`lexilla`(ScintillaOrg/lexilla)、`scintilla`(mirror/scintilla) —— 用与 1.6.3 对应的版本，`MFC42` 里的 `lib\NAFXCW.LIB` 等 5 个 lib 必须有 |
| 运行时 | 官方 `FA2SP_HDM_Edition_1.5.3.7z` 解压一份，作为测试用的地编目录 |

> 没有 git 也能做：上游 release 页有 source zip；子模块按 `.gitmodules` 里的地址分别下载对应快照解压到同名目录。

## 1. 把模组放进源码树

把本仓库 `src/FA2sp/Ext/CCollab/` 整个目录复制到：

```
<上游源码>\FA2sp\Ext\CCollab\
```

## 2. 注册进工程（**这一步会触发全量重编，约 40 分钟**）

### 2.1 `FA2sp.vcxproj`

在 `<ItemGroup>` 里补上（位置随意，建议跟在其它 Ext 后面）：

```xml
<ClCompile Include="FA2sp\Ext\CCollab\Body.cpp" />
<ClCompile Include="FA2sp\Ext\CCollab\core\collab_transport.cpp" />
<ClCompile Include="FA2sp\Ext\CCollab\core\collab_session.cpp" />
<ClInclude Include="FA2sp\Ext\CCollab\Body.h" />
<ClInclude Include="FA2sp\Ext\CCollab\core\collab_protocol.h" />
<ClInclude Include="FA2sp\Ext\CCollab\core\collab_transport.h" />
<ClInclude Include="FA2sp\Ext\CCollab\core\collab_session.h" />
```

### 2.2 启动挂点 `FA2sp/Ext/FA2Expand.cpp`

```cpp
#include "CCollab/Body.h"          // 文件顶部

// 在 FA2Expand::ProgramStartupInit() 末尾追加一行：
    CCollabExt::ProgramStartupInit();
```

> 之后**只改 `.cpp` 就是增量编译**（2~3 分钟）。再动 `.vcxproj` 一定全量重编。

## 3. 编译

```powershell
& '<VS 安装>\MSBuild\Current\Bin\MSBuild.exe' '<源码>\FA2sp.sln' /nologo /m:8 /v:m `
  /p:Configuration=Release /p:Platform=x86 `
  /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:DebugInformationFormat=None
```

产物：`<源码>\Release\FA2sp.dll`（32 位，PE `machine=0x014C`）。

### 源码编码（必读）

工程用 `/source-charset:gbk`：

- 新增/修改的 `.cpp/.h` **必须存成 GBK**。存成 UTF-8 时中文注释最后一行会"吃掉"下一行的 `#pragma once`，
  然后就是一堆莫名其妙的"重定义"
- 换行统一用 LF 或 CRLF 都行，但同一个文件里别混（混着也能编，只是 diff 很脏）

## 4. 部署与联机测试

准备两个地编目录（同一份官方运行时各拷一份）：

```
<工作区>\fa2sp_test_ours\   # 房主
<工作区>\fa2sp_test_beta\   # 加入方
```

把编出来的 `FA2sp.dll` 覆盖进两个目录，并各写一份 `FA2spCollab.ini`
（模板见 `config/FA2spCollab.ini`，房主 `Host=1`、加入方 `Host=0` + `HostAddress=`）。

首次运行要在 `FinalAlert.ini` 里写 `[TS] Exe=<游戏目录>`，否则会停在 Options 对话框。
同一台机器双开时第二个实例会弹 `FA2sp Init Checker`，点「是」即可。

## 5. 跑验证

```powershell
# 路径可以用环境变量覆盖：
#   FA2BUILD  = 编译目录（默认 C:\FA2Build）
#   FA2TEST   = 两个实例目录的父目录（默认 <工作区>\fa2sp_test；也会自动识别 fa2sp_test_ours/_beta）
#   MSBUILD   = MSBuild.exe 路径
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1        # 14 项一键回归
powershell -ExecutionPolicy Bypass -File scripts\objtest.ps1       # 物件同步（前后截图 + 日志）
powershell -ExecutionPolicy Bypass -File scripts\soak_objects.ps1 -Minutes 5
```

`verify.ps1` 流程：编 DLL → 编内核单测（`kernel\build_test.cmd`）→ 起两个实例 →
断言日志 14 项（握手/光标/地形增量/物件重建/租约拒绝/保护还原/INI/快照/无崩溃）。

## 6. 只编联机内核（不碰 FA2，5 秒）

`kernel/` 里的会话/传输/协议/合并算法不依赖 FA2，可以单独编译和单测：

```cmd
cd kernel
build_test.cmd
build\test_collab.exe
```

输出 `ALL PASS` 即 22 条断言全过（握手、租约、增量、断线、三方合并等）。

## 7. 常见故障

| 现象 | 原因 |
|---|---|
| `C2064` / `C2039` 一堆 | 源码存成了 UTF-8，`#pragma once` 被吃掉 |
| `static_assert` 在 `Logger::Info` | `FString::Format` 不支持 `unsigned long long`，换成 `%u`/`%d` 或先转型 |
| 第二次改 `.vcxproj` 后又是 40 分钟 | 预期行为，尽量一次改完 |
| 双实例第二台弹窗 | FA2sp 全局互斥体，`WM_COMMAND IDYES` 可自动点 |
| 对方物件不出现 | 看日志有没有 `applied ini section [Structures]` → `object lists rebuilt: buildings=N` |
| 物件重建崩 | 日志 `object list rebuild crashed`，功能会自动关闭；把日志与 `debug\snapshot-*\except.txt` 一起提 issue |
