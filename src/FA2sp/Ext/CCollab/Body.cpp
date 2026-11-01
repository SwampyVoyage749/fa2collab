#include "Body.h"

#include <CIsoView.h>
#include <CFinalSunDlg.h>
#include <CFinalSunApp.h>
#include <CMapData.h>
#include <CINI.h>
#include "../../Miscs/MultiSelection.h"
#include <CObjectDatas.h>
#include <windows.h>
#include <map>
#include <set>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>

#include "../../Logger.h"
#include "core/collab_session.h"
#include "core/collab_inimerge.h"
#include "../CMapData/Body.h"

namespace {

HWND        g_hWnd = nullptr;      // 隐藏消息窗口（定时器）
HWND        g_overlay = nullptr;   // 远端光标覆盖层
HWND        g_cfg = nullptr;       // 房间设置窗
HWND        g_edName = nullptr, g_edHost = nullptr, g_edPort = nullptr;
bool        g_cfgIsHost = true;
bool        g_enabled = false;
bool        g_started = false;
UINT_PTR    g_timer = 0;
std::string g_name = "player";
std::string g_host = "127.0.0.1";
uint16_t    g_port = 31415;
bool        g_isHost = false;
std::string g_lastCursorKey;

struct PeerCursor {
    std::string name;
    int cx = 0, cy = 0;
    bool has = false;
    bool hasSel = false;
    collab::Rect sel;
};
std::map<uint32_t, PeerCursor> g_peers;
std::set<uint32_t> g_deniedChunks;   // 被别人占住、本地改不动的块
std::map<uint32_t, std::vector<collab::CellEdit>> g_remoteShadow;  // 块 -> 收到的远端状态
std::string g_mapPath;
int          g_mapW = 0, g_mapH = 0, g_stride = 0;
int          g_chunkCols = 0, g_chunkRows = 0;
const int    kChunkSize = 16;
std::vector<uint64_t> g_chunkHash;
std::vector<uint64_t> g_cellHash;   // 每格上一帧的状态哈希
uint64_t     g_lastScan = 0;
bool         g_selfTest = false;
bool         g_selfTestDone = false;
uint64_t     g_selfTestP2 = 0;
bool         g_selfTestP2Done = false;
uint64_t     g_selfTestPhase2 = 0;
uint64_t     g_sessionStart = 0;
uint64_t     g_firstPeerTime = 0;
uint32_t     g_selfTestDelayMs = 0;
uint32_t     g_selfTestLoopMs = 0;
bool         g_selfTestObject = false; // test hook: push one [Structures] key
bool         g_selfTestObjectWalk = false; // test hook: move it every time (soak)
uint32_t     g_selfTestDeleteDelayMs = 0;  // test hook: delete the injected key after N ms
bool         g_selfTestBadCursor = false;  // test hook: broadcast out-of-range cursor cells
bool         g_selfTestDeletePending = false;
uint64_t     g_selfTestDeleteAt = 0;
int          g_selfTestObjSeq = 0;     // 每次注入挪一格，压测时能持续触发重建
bool         g_objDirty = false;       // an object section changed this tick
bool         g_objRefreshFailed = false; // object rebuild crashed once -> stay off
bool         g_iniDeleteSync = false; // INI 删键同步：实验性，默认关
bool         g_iniDirty = false;      // 本轮有 INI 变化，需要统一刷新一次
bool         g_showHud = true;        // 左上角状态条
bool         g_showPlayerList = true; // 玩家列表
std::map<std::string, std::map<std::string, std::string>> g_iniBase;   // 每节的共同起点
   // >0 表示周期性重复自测（压测用）
uint64_t     g_lastSelfTest = 0;
uint64_t     g_conflictTestAt = 0;
bool         g_conflictTestDone = false;
int          g_selfTestValue = 5;
std::map<std::string, uint64_t> g_iniHash;   // 节名 -> 内容哈希
std::map<std::string, uint64_t> g_iniLocalTick;   // 节名 -> 本地最近改动时间
std::map<std::string, uint64_t> g_iniRemoteTick;  // 节名 -> 应用远端改动的时间
uint32_t g_iniConflicts = 0;
uint64_t     g_lastIniScan = 0;
bool         g_snapshotPending = false;   // 有新人进来，需要推整图
int          g_snapChunkCursor = 0;
uint32_t    g_mapHash = 0;

std::unique_ptr<collab::Session> g_session;
const COLORREF kKeyColor = RGB(255, 0, 255);

std::string ExeDir()
{
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    auto pos = s.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : s.substr(0, pos);
}
std::string IniPath() { return ExeDir() + "\\FA2spCollab.ini"; }
std::string ReadIni(const char* key, const char* def)
{
    char buf[256] = {0};
    GetPrivateProfileStringA("Collab", key, def, buf, sizeof(buf), IniPath().c_str());
    return std::string(buf);
}

// client -> 屏幕 -> 地图，与 Ext/CIsoView 的 GetCurrentMapCoord 等价
inline void ClientToMap(CIsoView* view, POINT pt, int& mx, int& my)
{
    ClientToScreen(view->GetSafeHwnd(), &pt);
    mx = pt.x + view->ViewPosition.x;
    my = pt.y + view->ViewPosition.y;
    CIsoView::ScreenCoord2MapCoord(mx, my);
}

// 地图 -> 视图客户区
// 这个格子能不能安全地交给 FA2 换算：MapCoord2ScreenCoord 内部会 GetCellAt(X,Y)，
// 完全没有越界检查，坐标一旦非法就是 C0000005（实测崩在 0045E8F0）。
inline bool CellInRange(int mx, int my)
{
    if (CMapData::MapNotLoaded) return false;
    CMapData* map = &CMapData::Instance;
    if (!map || !map->CellDatas) return false;
    int stride = map->MapWidthPlusHeight;
    if (stride <= 0) return false;
    if (mx < 0 || my < 0 || mx >= stride || my >= stride) return false;
    long long idx = (long long)mx + (long long)my * stride;
    if (idx < 0 || idx >= map->CellDataCount) return false;
    return map->IsCoordInMap(mx, my);
}

inline void MapToClient(CIsoView* view, int mx, int my, int& cx, int& cy)
{
    cx = cy = -99999;                 // 调用方看到这个值就跳过绘制
    if (!CellInRange(mx, my)) return;
    int x = mx, y = my;
    CIsoView::MapCoord2ScreenCoord(x, y);
    POINT pt{x, y};
    ScreenToClient(view->GetSafeHwnd(), &pt);
    // 逆向：MapCoord2ScreenCoord 的结果 = client + ViewPosition
    cx = pt.x - view->ViewPosition.x;
    cy = pt.y - view->ViewPosition.y;
}

// ---------------- 地形/覆盖图同步（S2） ----------------
inline uint64_t HashCell(const CellData& c)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(c.TileIndex); mix(c.TileSubIndex); mix(c.Height);
    mix(c.Overlay); mix(c.OverlayData);
    return h;
}

inline uint32_t ChunkIndexOf(int cx, int cy) { return (uint32_t)cy * (uint32_t)g_chunkCols + (uint32_t)cx; }

bool EnsureSnapshot()
{
    if (CMapData::MapNotLoaded) return false;
    int w = CMapData::Instance->Size.Width;
    int h = CMapData::Instance->Size.Height;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return false;
    if (w == g_mapW && h == g_mapH && !g_chunkHash.empty()) return true;

    g_mapW = w; g_mapH = h;
    g_stride = CMapData::Instance->MapWidthPlusHeight;
    g_chunkCols = (w + kChunkSize - 1) / kChunkSize;
    g_chunkRows = (h + kChunkSize - 1) / kChunkSize;
    g_chunkHash.assign((size_t)g_chunkCols * g_chunkRows, 0);
    g_cellHash.assign((size_t)g_stride * (size_t)(h + 1), 0);
    Logger::Info("[Collab] snapshot init: %dx%d stride=%d chunks=%dx%d", w, h, g_stride, g_chunkCols, g_chunkRows);
    return true;
}

// 逐格比对：只把真正变化的格发出去
void ScanChunks(collab::Session* s, bool warmup)
{
    if (!EnsureSnapshot()) return;
    CellData* cells = CMapData::Instance->CellDatas;
    if (!cells) return;


    for (int cy = 0; cy < g_chunkRows; ++cy) {
        for (int cx = 0; cx < g_chunkCols; ++cx) {
            // 别人占着的块：本地改动（包括撤销）一律还原成远端状态，保护别人的工作
            uint32_t ckNow = collab::ChunkKey(cx, cy);
            auto shIt = g_remoteShadow.find(ckNow);
            if (g_deniedChunks.count(ckNow) && shIt != g_remoteShadow.end()) {
                int restored = 0;
                for (auto& e : shIt->second) {
                    if (e.index < 0) continue;
                    CellData& c = cells[e.index];
                    if (c.TileIndex != (unsigned short)e.tile || c.Height != (unsigned char)e.height ||
                        c.Overlay != (unsigned char)e.overlay || c.OverlayData != (unsigned char)e.ovdata ||
                        c.TileSubIndex != (unsigned char)e.sub) {
                        c.TileIndex = (unsigned short)e.tile;
                        c.TileSubIndex = (unsigned char)e.sub;
                        c.Height = (unsigned char)e.height;
                        c.Overlay = (unsigned char)e.overlay;
                        c.OverlayData = (unsigned char)e.ovdata;
                        if (e.index < (int)g_cellHash.size()) g_cellHash[e.index] = HashCell(c);
                        ++restored;
                    }
                }
                if (restored > 0) {
                    Logger::Info("[Collab] protected chunk (%d,%d): restored %d cells (owned by another player)",
                                 cx, cy, restored);
                    if (CFinalSunDlg::Instance) CFinalSunDlg::Instance->Update();
                }
                continue;   // 这块不参与本地 diff / 发送
            }

            std::vector<collab::CellEdit> edits;
            int x0 = cx * kChunkSize, y0 = cy * kChunkSize;
            for (int y = y0; y < y0 + kChunkSize && y < g_mapH; ++y) {
                for (int x = x0; x < x0 + kChunkSize && x < g_mapW; ++x) {
                    int idx = x + y * g_stride;
                    const CellData& c = cells[idx];
                    uint64_t h = HashCell(c);
                    if (idx >= (int)g_cellHash.size()) continue;
                    if (g_cellHash[idx] == h) continue;

                    g_cellHash[idx] = h;
                    if (!warmup) {
                        collab::CellEdit e;
                        e.index = idx;
                        e.height = (int16_t)c.Height;
                        e.tile = (int32_t)c.TileIndex;
                        e.sub = (int16_t)c.TileSubIndex;
                        e.overlay = (int32_t)c.Overlay;
                        e.ovdata = (int32_t)c.OverlayData;
                        edits.push_back(e);
                    }
                }
            }
            uint32_t ci = ChunkIndexOf(cx, cy);
            if (!warmup && !edits.empty()) {
                // 先要租约（v1 Host 会自动授予）
                if (s) {
                    collab::Rect r{(int16_t)cx, (int16_t)cy, (int16_t)cx, (int16_t)cy};  // 块坐标
                    s->RequestLease(r);
                    s->SendTileDelta(collab::ChunkKey(cx, cy), edits);
                }
                Logger::Info("[Collab] local chunk (%d,%d) changed cells=%u", cx, cy, (unsigned)edits.size());
            }
            if (warmup && ci < g_chunkHash.size()) g_chunkHash[ci] = 1;  // 占位，下面统一重算
        }
    }
}

// ---------------- 地图 INI 单节同步（触发/小队/脚本都在这） ----------------
bool IsTerrainSection(const char* name)
{
    return _strnicmp(name, "TileSet", 7) == 0 || _strnicmp(name, "IsoMapPack", 10) == 0 ||
           _strnicmp(name, "OverlayPack", 11) == 0 || _strnicmp(name, "OverlayDataPack", 15) == 0 ||
           _strnicmp(name, "PreviewPack", 11) == 0;
}

void ScanIni(collab::Session* s)
{
    CINI* doc = &CINI::CurrentDocument;
    if (!doc) return;

    int sentThisRound = 0;
    for (auto& kv : doc->Dict) {
        if (sentThisRound >= 20) break;   // 限流：大图节多，避免一次刷爆
        const char* name = kv.first;
        if (!name || !*name || IsTerrainSection(name)) continue;

        uint64_t h = 1469598103934665603ull;
        std::vector<std::pair<std::string, std::string>> pairs;
        for (auto& e : kv.second.GetEntities()) {
            const char* k = e.first;
            const char* v = e.second;
            if (!k) continue;
            for (const char* p = k; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
            if (v) for (const char* p = v; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
            pairs.emplace_back(k, v ? v : "");
        }
        auto it = g_iniHash.find(name);
        if (it != g_iniHash.end() && it->second == h) continue;

        if (it == g_iniHash.end()) {
            // 首次见到：属于"初始同步"，同时建立 base 快照（合并算法需要）
            g_iniHash[name] = h;
            std::map<std::string, std::string> b;
            for (auto& p : pairs) b[p.first] = p.second;
            g_iniBase[name] = b;
        } else {
            g_iniHash[name] = h;
            uint64_t nowTick = GetTickCount64();
            auto rt = g_iniRemoteTick.find(name);
            if (rt != g_iniRemoteTick.end() && nowTick - rt->second < 1000) {
                continue;   // 刚被远端覆盖，这一跳是回弹，不再发回去
            }
            g_iniLocalTick[name] = nowTick;
        }
        if (s) {
            s->SendIniSection(name, pairs);
            { std::map<std::string, std::string> b; for (auto& p : pairs) b[p.first] = p.second; g_iniBase[name] = b; }
            ++sentThisRound;
            Logger::Info("[Collab] ini section changed: [%s] entries=%u", name, (unsigned)pairs.size());
        }
    }
}

// ---- 物件同步：物件本体走 INI 节（[Structures]/[Units]/[Infantry]/[Aircraft]）----
// 收到远端物件后，只改 CellDatas 是不够的：等距视图每次重绘都会用 CellDatas 里的
// 索引去查 CMapDataExt::BuildingRenderDatasFix / UnitDatasExt / AircraftDatasExt /
// CMapData::InfantryDatas，这些表不重建就什么都画不出来（旧版直接写物件槽会崩，
// 正是因为索引指向了不存在的表项）。这里改调 FA2 自己的“从 INI 重建字段”例程。
bool IsObjectSection(const char* name)
{
    return _stricmp(name, "Structures") == 0 || _stricmp(name, "Units") == 0 ||
           _stricmp(name, "Infantry") == 0 || _stricmp(name, "Aircraft") == 0;
}

void RefreshObjectLists()
{
    if (CMapData::MapNotLoaded) return;
    CMapData* map = &CMapData::Instance;
    if (!map || !map->CellDatas) return;

    bool ok = true;
    __try {
        map->UpdateFieldStructureData(false);
        map->UpdateFieldUnitData(false);
        map->UpdateFieldInfantryData(false);
        map->UpdateFieldAircraftData(false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        g_objRefreshFailed = true;
        Logger::Error("[Collab] object list rebuild crashed, feature disabled (save your map, then restart the editor)");
        return;
    }

    Logger::Info("[Collab] object lists rebuilt: buildings=%d units=%d infantry=%d aircraft=%d",
                 (int)CMapDataExt::BuildingRenderDatasFix.size(),
                 (int)CMapDataExt::UnitDatasExt.size(),
                 (int)map->InfantryDatas.size(),
                 (int)CMapDataExt::AircraftDatasExt.size());

    // 表换了内容，必须让等距视图整块重画；重画时才会按新索引 Reload 渲染器
    CIsoView* view = CIsoView::GetInstance();
    if (view && view->GetSafeHwnd())
        ::RedrawWindow(view->GetSafeHwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

std::map<std::string, std::string> CollectSection(INISection* sec)
{
    std::map<std::string, std::string> m;
    if (!sec) return m;
    for (auto& e : sec->GetEntities()) {
        const char* k = e.first;
        const char* v = e.second;
        if (k) m[k] = v ? v : "";
    }
    return m;
}
void ApplyIniSection(const std::string& name,
                     const std::vector<std::pair<std::string, std::string>>& kv)
{
    CINI* doc = &CINI::CurrentDocument;
    if (!doc) return;

    // 冲突检测：本地 1 秒内也改过同一节 → 记为冲突（策略：远端覆盖本地）
    {
        uint64_t nowTick = GetTickCount64();
        auto lt = g_iniLocalTick.find(name);
        if (lt != g_iniLocalTick.end() && nowTick - lt->second < 1000) {
            ++g_iniConflicts;
            Logger::Warn("[Collab] CONFLICT on ini section [%s]: local edit overwritten by remote", name.c_str());
        }
        g_iniRemoteTick[name] = nowTick;
    }

    INISection* sec = doc->AddOrGetSection(name.c_str());
    if (!sec) return;

    std::map<std::string, std::string> remoteMap;
    for (auto& p : kv) remoteMap[p.first] = p.second;
    auto localMap = CollectSection(sec);
    auto baseIt = g_iniBase.find(name);
    const auto& baseMap = (baseIt != g_iniBase.end()) ? baseIt->second : localMap;

    auto merged = collab::MergeIniSection(baseMap, localMap, remoteMap);
    if (!merged.conflicts.empty()) {
        g_iniConflicts += (uint32_t)merged.conflicts.size();
        Logger::Warn("[Collab] merge conflict on [%s]: %u keys (kept local)",
                     name.c_str(), (unsigned)merged.conflicts.size());
    }

    int applied = 0, removed = 0;
    for (auto& p : merged.merged) {
        auto it = localMap.find(p.first);
        if (it != localMap.end() && it->second == p.second) continue;   // 没变化
        if (auto* existing = sec->TryGetString(p.first.c_str())) *existing = p.second.c_str();
        else doc->InsertPair(sec->GetEntities(), p.first.c_str(), p.second.c_str());
        ++applied;
    }

    // 删除同步：本地有、三方合并结果里没有的键 —— 说明「对端删了、本地没动」，真删掉。
    // 注意不能拿「远端整节」做镜像：那样会把本地刚加的新键一起误删（"不同键共存"就没了）。
    if (g_iniDeleteSync) {
        std::vector<std::string> toDelete;
        for (auto& kv2 : localMap) {
            if (merged.merged.find(kv2.first) == merged.merged.end()) toDelete.push_back(kv2.first);
        }
        for (auto& k : toDelete) {
            auto it = sec->GetEntities().find(ppmfc::CString(k.c_str()));
            if (it != sec->GetEntities().end()) { sec->GetEntities().manual_erase(it); ++removed; }
        }
        if (removed > 0) {
            Logger::Info("[Collab] ini section [%s]: removed %d keys (deleted by peer)",
                         name.c_str(), removed);
        }
    }
    applied += removed;

    g_iniBase[name] = CollectSection(sec);
    if (IsObjectSection(name.c_str()) || removed > 0) {
        g_objDirty = true;   // rebuilt once at the end of this tick
    }
    // 保持基线一致，避免把收到的改动再发回去
    uint64_t h = 1469598103934665603ull;
    for (auto& e : sec->GetEntities()) {
        const char* k = e.first; const char* v = e.second;
        if (k) for (const char* p = k; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
        if (v) for (const char* p = v; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
    }
    g_iniHash[name] = h;

    Logger::Info("[Collab] applied ini section [%s] entries=%d total=%u",
                 name.c_str(), applied, (unsigned)sec->GetEntities().size());
    if (applied > 0) g_iniDirty = true;   // 放到 tick 末尾统一刷新
}
// 应用远端增量：写入并同步本格快照，避免回环
void ApplyRemoteDelta(uint32_t chunk, const std::vector<collab::CellEdit>& edits)
{
    if (!EnsureSnapshot()) return;
    CellData* cells = CMapData::Instance->CellDatas;
    if (!cells) return;

    int applied = 0;
    for (auto& e : edits) {
        if (e.index < 0) continue;
        CellData& c = cells[e.index];
        if (c.TileIndex == (unsigned short)e.tile && c.Height == (unsigned char)e.height &&
            c.Overlay == (unsigned char)e.overlay && c.OverlayData == (unsigned char)e.ovdata &&
            c.TileSubIndex == (unsigned char)e.sub) {
            continue;
        }
        c.TileIndex = (unsigned short)e.tile;
        c.TileSubIndex = (unsigned char)e.sub;
        c.Height = (unsigned char)e.height;
        c.Overlay = (unsigned char)e.overlay;
        c.OverlayData = (unsigned char)e.ovdata;
        if (e.index < (int)g_cellHash.size()) g_cellHash[e.index] = HashCell(c);
        ++applied;
    }

    {
        // 记录该块的远端基准，后续用来挡住本地撤销/误改
        auto& shadow = g_remoteShadow[chunk];
        shadow = edits;
    }

    if (applied > 0) {
        Logger::Info("[Collab] applied remote chunk (%u,%u) cells=%d", collab::ChunkX(chunk), collab::ChunkY(chunk), applied);
    }
    if (applied > 0 && CFinalSunDlg::Instance) {
        CFinalSunDlg::Instance->Update();
    }
}
class LogSink : public collab::Sink
{
public:
    void OnPeerJoin(const collab::Peer& p) override
    {
        g_peers[p.id].name = p.name;
        if (g_firstPeerTime == 0) g_firstPeerTime = GetTickCount64();
        if (g_isHost) {              // 房主负责给新人补一份完整快照
            g_snapshotPending = true;
            g_snapChunkCursor = 0;
            // 新人没有 INI 状态：清空哈希，让 ScanIni 把全部节重发一遍
            g_iniHash.clear();
            g_iniRemoteTick.clear();
        }
        Logger::Info("[Collab] peer joined: id=%u name=%s", p.id, p.name.c_str());
    }
    void OnPeerLeave(uint32_t id) override
    {
        g_peers.erase(id);
        Logger::Info("[Collab] peer left: id=%u", id);
    }
    void OnCursor(const collab::CursorState& c) override
    {
        auto& pc = g_peers[c.player];
        if (!CellInRange(c.cx, c.cy)) {
            // 对端鼠标在视野外时会把 (-1,-1) 发过来；这种坐标绝不能喂给 FA2
            static uint64_t lastBadWarn = 0;
            uint64_t nowBad = GetTickCount64();
            if (nowBad - lastBadWarn > 5000) {
                lastBadWarn = nowBad;
                Logger::Info("[Collab] ignore invalid cursor from id=%u cell=(%d,%d)",
                             c.player, (int)c.cx, (int)c.cy);
            }
            pc.has = false;
            pc.hasSel = false;
            return;
        }
        pc.cx = c.cx; pc.cy = c.cy; pc.has = true;
        pc.hasSel = c.hasSel;
        pc.sel = c.sel;
        static int lastCx = -32768, lastCy = -32768;
        if (c.cx != lastCx || c.cy != lastCy) {
            lastCx = c.cx; lastCy = c.cy;
            Logger::Info("[Collab] cursor from id=%u cell=(%d,%d) tool=%u brush=%u",
                         c.player, (int)c.cx, (int)c.cy, (unsigned)c.tool, (unsigned)c.brush);
        }
    }
    void OnLeaseResult(const collab::Rect& r, bool ok, uint32_t chunks, uint32_t owner) override
    {
        if (!ok) {
            g_deniedChunks.insert(collab::ChunkKey(r.x0, r.y0));
            // 被别人占着：本地改动不会同步过去
            static uint64_t lastWarn = 0;
            uint64_t now = GetTickCount64();
            if (now - lastWarn > 3000) {
                lastWarn = now;
                Logger::Warn("[Collab] chunk (%d,%d) owned by player %u — local change NOT synced",
                             (int)r.x0, (int)r.y0, owner);
            }
            return;
        }
        g_deniedChunks.erase(collab::ChunkKey(r.x0, r.y0));
        Logger::Info("[Collab] lease (%d,%d)-(%d,%d) granted chunks=%u",
                     (int)r.x0, (int)r.y0, (int)r.x1, (int)r.y1, chunks);
    }
    void OnTileDelta(uint32_t from, uint32_t chunk, const std::vector<collab::CellEdit>& e) override
    {
        Logger::Info("[Collab] tile delta from=%u chunk=%u cells=%u", from, chunk, (unsigned)e.size());
        ApplyRemoteDelta(chunk, e);
    }
    void OnChat(uint32_t from, const std::string& t) override
    {
        Logger::Info("[Collab] chat from=%u: %s", from, t.c_str());
    }
    void OnIniSection(const std::string& name,
                      const std::vector<std::pair<std::string, std::string>>& kv) override
    {
        ApplyIniSection(name, kv);
    }
    void OnDisconnected() override
    {
        static uint64_t lastWarn = 0;
        uint64_t now = GetTickCount64();
        if (now - lastWarn > 15000) {
            lastWarn = now;
            Logger::Warn("[Collab] disconnected, retrying (single-player meanwhile)");
        }
        g_peers.clear();
        if (g_overlay) InvalidateRect(g_overlay, nullptr, TRUE);
    }
};
LogSink g_sink;

void PaintOverlay(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    if (!hdc) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    HBRUSH bg = CreateSolidBrush(kKeyColor);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // 左上角状态 HUD：本机身份 / 在线玩家 / 冲突块数
    if (g_showHud) {
        char hud[256];
        _snprintf_s(hud, sizeof(hud), _TRUNCATE,
                    "Collab %s (%s)  online:%u  conflict:%u  ini-conflict:%u",
                    g_name.c_str(), g_isHost ? "host" : "join",
                    (unsigned)g_peers.size(), (unsigned)g_deniedChunks.size(), g_iniConflicts);
        RECT box{ 8, 8, 8 + 8 * (int)strlen(hud), 30 };
        HBRUSH hb = CreateSolidBrush(RGB(20, 40, 70));
        FillRect(hdc, &box, hb);
        DeleteObject(hb);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(210, 240, 255));
        HFONT hf = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH, "Consolas");
        HGDIOBJ of = SelectObject(hdc, hf);
        TextOutA(hdc, 12, 10, hud, (int)strlen(hud));
        SelectObject(hdc, of);
        DeleteObject(hf);
    }

    // 玩家列表：名字 + 对方光标所在格
    if (g_showPlayerList) {
        int y = 36;
        HFONT hf2 = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, "Consolas");
        HGDIOBJ of2 = SelectObject(hdc, hf2);
        SetBkMode(hdc, TRANSPARENT);
        for (auto& kv : g_peers) {
            char line[160];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "  %s  (%d,%d)",
                        kv.second.name.empty() ? "?" : kv.second.name.c_str(),
                        kv.second.cx, kv.second.cy);
            int w = 8 * (int)strlen(line) + 8;
            RECT b{ 8, y, 8 + w, y + 20 };
            HBRUSH hb2 = CreateSolidBrush(RGB(20, 40, 70));
            FillRect(hdc, &b, hb2);
            DeleteObject(hb2);
            SetTextColor(hdc, RGB(180, 220, 255));
            TextOutA(hdc, 12, y + 2, line, (int)strlen(line));
            y += 20;
        }
        SelectObject(hdc, of2);
        DeleteObject(hf2);
    }

    CIsoView* view = CIsoView::GetInstance();
    if (view) {
        SetBkMode(hdc, TRANSPARENT);
        for (auto& kv : g_peers) {
            if (!kv.second.has) continue;
            int x = 0, y = 0;
            MapToClient(view, kv.second.cx, kv.second.cy, x, y);
            if (x < -100 || y < -100 || x > rc.right + 100 || y > rc.bottom + 100) continue;

            HBRUSH dot = CreateSolidBrush(RGB(0, 200, 255));
            HGDIOBJ oldBrush = SelectObject(hdc, dot);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 128));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            Ellipse(hdc, x - 6, y - 6, x + 6, y + 6);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(dot);
            DeleteObject(pen);

            // 被占住的块：淡红描边
            SetTextColor(hdc, RGB(0, 90, 160));
            HFONT font = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     DEFAULT_PITCH, "Microsoft YaHei");
            HGDIOBJ oldFont = SelectObject(hdc, font);
            TextOutA(hdc, x + 9, y - 18, kv.second.name.c_str(), (int)kv.second.name.size());
            SelectObject(hdc, oldFont);
            DeleteObject(font);
        }
    }
    // 远端选区（橙色细框）
    if (view) {
        for (auto& kv : g_peers) {
            if (!kv.second.hasSel) continue;
            if (!CellInRange(kv.second.sel.x0, kv.second.sel.y0) ||
                !CellInRange(kv.second.sel.x1, kv.second.sel.y1)) continue;
            int px[4], py[4];
            MapToClient(view, kv.second.sel.x0, kv.second.sel.y0, px[0], py[0]);
            MapToClient(view, kv.second.sel.x1, kv.second.sel.y0, px[1], py[1]);
            MapToClient(view, kv.second.sel.x0, kv.second.sel.y1, px[2], py[2]);
            MapToClient(view, kv.second.sel.x1, kv.second.sel.y1, px[3], py[3]);
            int l = px[0], t2 = py[0], r = px[0], b2 = py[0];
            for (int i = 1; i < 4; ++i) {
                if (px[i] < l) l = px[i];
                if (px[i] > r) r = px[i];
                if (py[i] < t2) t2 = py[i];
                if (py[i] > b2) b2 = py[i];
            }
            HPEN selPen = CreatePen(PS_DOT, 1, RGB(255, 160, 0));
            HGDIOBJ op = SelectObject(hdc, selPen);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, l, t2, r, b2);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(selPen);
        }
    }

    // 被拒块描红（只在有冲突时出现）

    if (view && !g_deniedChunks.empty()) {
        HPEN redPen = CreatePen(PS_SOLID, 2, RGB(220, 40, 40));
        HGDIOBJ oldPen2 = SelectObject(hdc, redPen);
        HGDIOBJ oldBrush2 = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        for (uint32_t ck : g_deniedChunks) {
            int cx0 = collab::ChunkX(ck) * 16, cy0 = collab::ChunkY(ck) * 16;
            int cx1 = cx0 + 15, cy1 = cy0 + 15;
            if (cx1 >= g_mapW) cx1 = g_mapW - 1;   // 贴边块要裁到地图内，否则坐标换算会跑飞
            if (cy1 >= g_mapH) cy1 = g_mapH - 1;
            if (!CellInRange(cx0, cy0) || !CellInRange(cx1, cy1)) continue;
            // 等距投影下对角两点会退化成同一个 x（同一条斜线），必须取四角包围盒
            int px[4], py[4];
            MapToClient(view, cx0, cy0, px[0], py[0]);
            MapToClient(view, cx1, cy0, px[1], py[1]);
            MapToClient(view, cx0, cy1, px[2], py[2]);
            MapToClient(view, cx1, cy1, px[3], py[3]);
            int x0 = px[0], x1 = px[0], y0 = py[0], y1 = py[0];
            for (int i = 1; i < 4; ++i) {
                if (px[i] < x0) x0 = px[i];
                if (px[i] > x1) x1 = px[i];
                if (py[i] < y0) y0 = py[i];
                if (py[i] > y1) y1 = py[i];
            }

            if (x0 > rc.right || y0 > rc.bottom || x1 < 0 || y1 < 0) continue;
            // 等距投影下 cx1,cy1 可能落在左上方，必须归一化，否则 Rectangle 画不出来
            RECT r;
            r.left = x0 < x1 ? x0 : x1;
            r.top = y0 < y1 ? y0 : y1;
            r.right = x0 < x1 ? x1 : x0;
            r.bottom = y0 < y1 ? y1 : y0;
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        }
        SelectObject(hdc, oldBrush2);
        SelectObject(hdc, oldPen2);
        DeleteObject(redPen);
    }

    EndPaint(hWnd, &ps);
}

void RestartSession()
{
    if (g_session) { g_session->Shutdown(); g_session.reset(); }
    g_peers.clear();
    g_deniedChunks.clear();
    g_remoteShadow.clear();
    g_iniHash.clear();
    g_iniLocalTick.clear();
    g_iniRemoteTick.clear();
    g_mapPath.clear();      // 触发 Tick 里重新建立会话
    Logger::Info("[Collab] session restart requested (host=%d name=%s addr=%s port=%u)",
                 (int)g_isHost, g_name.c_str(), g_host.c_str(), (unsigned)g_port);
}

void ApplyCfgFromWindow()
{
    char buf[128];
    if (g_edName) { GetWindowTextA(g_edName, buf, sizeof(buf)); g_name = buf; }
    if (g_edHost) { GetWindowTextA(g_edHost, buf, sizeof(buf)); g_host = buf; }
    if (g_edPort) { GetWindowTextA(g_edPort, buf, sizeof(buf)); int p = atoi(buf); if (p > 0 && p < 65536) g_port = (uint16_t)p; }
    g_isHost = g_cfgIsHost;
    RestartSession();
}

LRESULT CALLBACK CfgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleA(nullptr);
        CreateWindowA("STATIC", "Name:", WS_CHILD | WS_VISIBLE, 12, 12, 60, 20, hWnd, nullptr, hi, nullptr);
        g_edName = CreateWindowA("EDIT", g_name.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 80, 10, 180, 22, hWnd, nullptr, hi, nullptr);
        CreateWindowA("STATIC", "Host IP:", WS_CHILD | WS_VISIBLE, 12, 44, 60, 20, hWnd, nullptr, hi, nullptr);
        g_edHost = CreateWindowA("EDIT", g_host.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 80, 42, 180, 22, hWnd, nullptr, hi, nullptr);
        CreateWindowA("STATIC", "Port:", WS_CHILD | WS_VISIBLE, 12, 76, 60, 20, hWnd, nullptr, hi, nullptr);
        char pbuf[16]; _snprintf_s(pbuf, sizeof(pbuf), _TRUNCATE, "%u", (unsigned)g_port);
        g_edPort = CreateWindowA("EDIT", pbuf, WS_CHILD | WS_VISIBLE | WS_BORDER, 80, 74, 80, 22, hWnd, nullptr, hi, nullptr);
        CreateWindowA("BUTTON", "我是房主(监听)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (g_cfgIsHost ? WS_TABSTOP : 0),
                      12, 104, 130, 22, hWnd, (HMENU)1001, hi, nullptr);
        CreateWindowA("BUTTON", "加入别人", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (g_cfgIsHost ? 0 : WS_TABSTOP),
                      150, 104, 110, 22, hWnd, (HMENU)1002, hi, nullptr);
        CreateWindowA("BUTTON", "应用并重连", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 12, 136, 120, 26, hWnd, (HMENU)1003, hi, nullptr);
        CreateWindowA("BUTTON", "关闭", WS_CHILD | WS_VISIBLE, 150, 136, 110, 26, hWnd, (HMENU)1004, hi, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 1001) g_cfgIsHost = true;
        else if (id == 1002) g_cfgIsHost = false;
        else if (id == 1003) { ApplyCfgFromWindow(); ShowWindow(hWnd, SW_HIDE); }
        else if (id == 1004) ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void ShowCfgWindow()
{
    if (!g_cfg) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = CfgProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "FA2CollabCfg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        g_cfg = CreateWindowExA(0, "FA2CollabCfg", "FA2sp Collab 设置 (Ctrl+Alt+C)",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 300, 210,
                                nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (g_cfg) { ShowWindow(g_cfg, SW_SHOW); SetForegroundWindow(g_cfg); }
}
LRESULT CALLBACK OverlayProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:      PaintOverlay(hWnd); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void SyncOverlay()
{
    if (!g_overlay) return;
    HWND main = CFinalSunDlg::Instance ? CFinalSunDlg::Instance->GetSafeHwnd() : nullptr;
    if (!main || !IsWindowVisible(main)) { ShowWindow(g_overlay, SW_HIDE); return; }
    RECT cr;
    GetClientRect(main, &cr);
    POINT tl{cr.left, cr.top};
    ClientToScreen(main, &tl);
    int w = cr.right - cr.left, h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return;
    SetWindowPos(g_overlay, HWND_TOP, tl.x, tl.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_overlay, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == 1) { CCollabExt::Tick(); return 0; }
    if (msg == WM_HOTKEY && wParam == 0xA1) { ShowCfgWindow(); return 0; }
    if (msg == WM_DESTROY) { if (g_timer) { KillTimer(hWnd, 1); g_timer = 0; } return 0; }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

} // namespace

bool CCollabExt::Enabled() { return g_enabled; }

// 文件级哈希：够用即可（同图同哈希，不同图绝不相等）
static uint32_t HashFile(const char* path)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") != 0 || !fp) return 0;
    uint32_t h = 2166136261u;
    unsigned char buf[8192];
    size_t n = 0, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0 && total < (8u << 20)) {
        for (size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 16777619u; }
        total += n;
    }
    fclose(fp);
    h ^= (uint32_t)total;
    return h;
}

// 地图就绪后（或换图后）再建会话，保证哈希与地图一致
static void EnsureSession()
{
    const char* mp = CFinalSunApp::MapPath();
    if (!mp || !*mp) return;
    if (g_session && g_mapPath == mp) return;

    uint32_t h = HashFile(mp);
    if (g_session) {
        g_session->Shutdown();
        g_session.reset();
        g_peers.clear();
        g_lastCursorKey.clear();
        Logger::Info("[Collab] map changed, session restarted");
    }
    g_mapPath = mp;
    g_mapHash = h;
    g_sessionStart = GetTickCount64();
    g_chunkHash.clear();
    Logger::Info("[Collab] map ready: %s hash=0x%08X", mp, h);

    if (g_isHost) {
        uint16_t realPort = g_port;
        g_session = collab::CreateHost(g_port, g_name, h, 30000, &g_sink, &realPort);
        if (g_session) Logger::Info("[Collab] hosting on port %u as %s", (unsigned)realPort, g_name.c_str());
    } else {
        g_session = collab::CreateClient(g_host, g_port, g_name, h, &g_sink);
        if (g_session) Logger::Info("[Collab] joining %s:%u as %s", g_host.c_str(), (unsigned)g_port, g_name.c_str());
    }
    if (!g_session) Logger::Warn("[Collab] session not started (will retry)");
}
void CCollabExt::ProgramStartupInit()
{
    g_enabled = ReadIni("Enabled", "0") == "1";
    if (!g_enabled) {
        Logger::Info("[Collab] disabled (FA2spCollab.ini: [Collab] Enabled=0)");
        return;
    }

    g_name   = ReadIni("Name", "player");
    g_isHost = ReadIni("Host", "0") == "1";
    g_host   = ReadIni("HostAddress", "127.0.0.1");
    g_port   = (uint16_t)atoi(ReadIni("Port", "31415").c_str());
    g_selfTest = ReadIni("SelfTest", "0") == "1";
    g_selfTestDelayMs = (uint32_t)atoi(ReadIni("SelfTestDelayMs", "0").c_str());
    g_selfTestValue = atoi(ReadIni("SelfTestValue", "5").c_str());
    g_selfTestLoopMs = (uint32_t)atoi(ReadIni("SelfTestLoopMs", "0").c_str());
    g_selfTestObject = ReadIni("SelfTestObject", "0") == "1";
    g_selfTestObjectWalk = ReadIni("SelfTestObjectWalk", "0") == "1";
    g_selfTestDeleteDelayMs = (uint32_t)atoi(ReadIni("SelfTestDeleteDelayMs", "0").c_str());
    g_selfTestBadCursor = ReadIni("SelfTestBadCursor", "0") == "1";
    g_iniDeleteSync = ReadIni("IniDeleteSync", "1") == "1";
    g_showHud = ReadIni("Hud", "1") == "1";
    g_showPlayerList = ReadIni("PlayerList", "1") == "1";


    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "FA2CollabWnd";
    RegisterClassA(&wc);

    g_hWnd = CreateWindowExA(0, "FA2CollabWnd", "FA2Collab", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_hWnd) {
        Logger::Error("[Collab] failed to create hidden window, disabled");
        g_enabled = false;
        return;
    }


    WNDCLASSA wco = {};
    wco.lpfnWndProc = OverlayProc;
    wco.hInstance = wc.hInstance;
    wco.lpszClassName = "FA2CollabOverlay";
    wco.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wco);
    g_overlay = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        "FA2CollabOverlay", "", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    if (g_overlay) SetLayeredWindowAttributes(g_overlay, kKeyColor, 0, LWA_COLORKEY);
    else Logger::Warn("[Collab] overlay window failed, presence without drawing");

    if (RegisterHotKey(g_hWnd, 0xA1, MOD_CONTROL | MOD_ALT, 'C'))
        Logger::Info("[Collab] hotkey Ctrl+Alt+C registered");
    else
        Logger::Warn("[Collab] hotkey registration failed (err=%u)", (unsigned)GetLastError());
    if (ReadIni("ShowCfgOnStart", "0") == "1") ShowCfgWindow();
    g_timer = SetTimer(g_hWnd, 1, 100, nullptr);
    g_started = true;
    Logger::Info("[Collab] started (timerId=%u)", (unsigned)g_timer);
}

void CCollabExt::Tick()
{
    if (!g_started) return;
    EnsureSession();
    if (!g_session) return;

    {
        uint64_t nowMs = GetTickCount64();
        if (g_chunkHash.empty()) {
            g_chunkHash.assign(1, 0);   // 占位，EnsureSnapshot 会重建
            g_chunkHash.clear();
            if (EnsureSnapshot()) { ScanChunks(g_session.get(), true); }
        } else if (nowMs - g_lastScan > 200) {
            g_lastScan = nowMs;
            ScanChunks(g_session.get(), false);
        }

        if (nowMs - g_lastIniScan > 500) {
            g_lastIniScan = nowMs;
            ScanIni(g_session.get());
        }

        // 中途加入快照：按块分批推，每 tick 8 块，避免一次性堵住
        if (g_isHost && g_snapshotPending && EnsureSnapshot()) {
            CellData* cells = CMapData::Instance->CellDatas;
            int sent = 0;
            while (g_snapChunkCursor < g_chunkRows * g_chunkCols && sent < 8) {
                int cx = g_snapChunkCursor % g_chunkCols;
                int cy = g_snapChunkCursor / g_chunkCols;
                ++g_snapChunkCursor;
                std::vector<collab::CellEdit> edits;
                int x0 = cx * kChunkSize, y0 = cy * kChunkSize;
                for (int y = y0; y < y0 + kChunkSize && y < g_mapH; ++y)
                    for (int x = x0; x < x0 + kChunkSize && x < g_mapW; ++x) {
                        int idx = x + y * g_stride;
                        const CellData& c = cells[idx];
                        collab::CellEdit e;
                        e.index = idx;
                        e.height = (int16_t)c.Height;
                        e.tile = (int32_t)c.TileIndex;
                        e.sub = (int16_t)c.TileSubIndex;
                        e.overlay = (int32_t)c.Overlay;
                        e.ovdata = (int32_t)c.OverlayData;
                        edits.push_back(e);
                    }
                if (!edits.empty() && g_session) {
                    g_session->SendTileDelta(collab::ChunkKey(cx, cy), edits);
                    ++sent;
                }
            }
            if (g_snapChunkCursor >= g_chunkRows * g_chunkCols) {
                g_snapshotPending = false;
                Logger::Info("[Collab] snapshot push finished: %d chunks", g_snapChunkCursor);
            }
        }


        // 删除自测：把刚放进去的 key=999 从 INI 删掉，验证对端也会删
        if (g_selfTestDeletePending && nowMs >= g_selfTestDeleteAt) {
            g_selfTestDeletePending = false;
            CINI* d2 = &CINI::CurrentDocument;
            INISection* st2 = d2 ? d2->GetSection("Structures") : nullptr;
            if (st2) {
                auto it2 = st2->GetEntities().find(ppmfc::CString("999"));
                if (it2 != st2->GetEntities().end()) {
                    st2->GetEntities().manual_erase(it2);
                    Logger::Info("[Collab] selftest: deleted structure key=999");
                }
            }
        }

        // 压测模式：周期性重置，让自测反复注入改动
        if (g_selfTestLoopMs > 0 && g_selfTestDone && nowMs - g_lastSelfTest > g_selfTestLoopMs) {
            g_selfTestDone = false;
            g_lastSelfTest = nowMs;
        }

        // 自测：注入一次本地改动，验证增量能否到达对端
        if (g_selfTest && !g_selfTestDone && g_firstPeerTime != 0 && (nowMs - g_firstPeerTime) > g_selfTestDelayMs && EnsureSnapshot()) {
            g_selfTestDone = true;
            g_lastSelfTest = nowMs;
            g_lastSelfTest = nowMs;
            CellData* cells = CMapData::Instance->CellDatas;
            if (cells) {
                // 改 4x4 一小片，放在视野中心附近，方便肉眼与截图确认
                int changed = 0;
                for (int yy = 196; yy < 200; ++yy) {
                    for (int xx = 196; xx < 200; ++xx) {
                        int idx = xx + yy * g_stride;
                        cells[idx].Overlay = (unsigned char)g_selfTestValue;
                        cells[idx].OverlayData = 1;
                        ++changed;
                    }
                }
                Logger::Info("[Collab] selftest: wrote overlay=%d on %d cells (196..199,196..199)", g_selfTestValue, changed);

                // 构造一个 6x6 多选区域，验证选区同步
                for (int yy2 = 196; yy2 < 202; ++yy2)
                    for (int xx2 = 196; xx2 < 202; ++xx2)
                        MultiSelection::SelectedCoords.insert(MapCoord{ xx2, yy2 });
                Logger::Info("[Collab] selftest: selection 6x6 at (196,196)");

                // 顺带验证 INI 节同步（触发/脚本都在 INI 里）
                CINI* doc = &CINI::CurrentDocument;
                if (doc) {
                    INISection* sec = doc->AddOrGetSection("CollabTest");
                    if (sec) {
                        if (auto* v = sec->TryGetString("By")) *v = g_name.c_str();
                        else doc->InsertPair(sec->GetEntities(), "By", g_name.c_str());
                        std::string ownKey = "Own_" + g_name;   // 每个实例写自己独有的键，用来验证"不同键可共存"
                        if (auto* v2 = sec->TryGetString(ownKey.c_str())) *v2 = "1";
                        else doc->InsertPair(sec->GetEntities(), ownKey.c_str(), "1");
                        Logger::Info("[Collab] selftest: wrote INI [CollabTest] By=%s + %s", g_name.c_str(), ownKey.c_str());
                        // 物件同步自测：把一条真实 [Structures] 记录挪到对端光标旁边，
                        // 对端收到后应重建对象列表并重绘，画面里出现这栋建筑
                        if (g_selfTestObject) {
                            INISection* st = doc->AddOrGetSection("Structures");
                            if (st) {
                                // 有现成的建筑就复制它（House/TypeID 一定是合法的）；
                                // 空白图没有 [Structures] 时用一个必然存在的 RA2 建筑
                                std::string v = "Americans,GAPOWR,256,100,100,0,None,0,0,1,0,0,None,None,None,0,0";
                                if (st->GetEntities().size() > 0) {
                                    auto it0 = st->GetEntities().begin();
                                    if (it0->second) v = (const char*)it0->second;
                                }
                                std::vector<std::string> f;
                                std::string cur;
                                for (char ch : v) { if (ch == ',') { f.push_back(cur); cur.clear(); } else cur.push_back(ch); }
                                f.push_back(cur);
                                if (f.size() > 5) {
                                    int tx = 100, ty = 100;
                                    for (auto& pr : g_peers) {
                                        if (pr.second.has) { tx = pr.second.cx + 3; ty = pr.second.cy + 3; break; }
                                    }
                                    if (g_selfTestObjectWalk && g_mapW > 8 && g_mapH > 8) {
                                        // 压测：每次换一个位置，持续触发「重建 + 重绘」
                                        tx = 2 + ((tx + g_selfTestObjSeq) % (g_mapW - 4));
                                        ty = 2 + ((ty + g_selfTestObjSeq) % (g_mapH - 4));
                                    } else {
                                        // 目测：贴着对方光标放，保证一定在视野里
                                        if (tx < 2) tx = 2;
                                        if (ty < 2) ty = 2;
                                        if (tx > g_mapW - 4) tx = g_mapW - 4;
                                        if (ty > g_mapH - 4) ty = g_mapH - 4;
                                    }
                                    ++g_selfTestObjSeq;
                                    f[3] = std::to_string(ty);      // Y
                                    f[4] = std::to_string(tx);      // X
                                    std::string nv = f[0];
                                    for (size_t k = 1; k < f.size(); ++k) { nv += ","; nv += f[k]; }
                                    // 注意：InsertPair 对已存在的键不覆盖，压测时要显式改写
                                    if (auto* ex = st->TryGetString("999")) *ex = nv.c_str();
                                    else doc->InsertPair(st->GetEntities(), "999", nv.c_str());
                                    Logger::Info("[Collab] selftest: placed structure key=999 type=%s at (%d,%d)",
                                                 f[1].c_str(), tx, ty);
                                    if (g_selfTestDeleteDelayMs > 0) {
                                        g_selfTestDeletePending = true;
                                        g_selfTestDeleteAt = nowMs + g_selfTestDeleteDelayMs;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    __try {
        g_session->Tick(GetTickCount64());

        CIsoView* view = CIsoView::GetInstance();
        if (view && view->GetSafeHwnd()) {
            collab::CursorState c{};
            c.player = g_session->MyId();
            int mx = 0, my = 0;
            POINT p{view->MouseCurrentPosition.x, view->MouseCurrentPosition.y};
            ClientToMap(view, p, mx, my);
            if (!CellInRange(mx, my)) {
                // 鼠标不在有效格子上（视图外 / 地图菱形外）：本帧不发光标，避免对端拿 (-1,-1) 去换算
                mx = -1; my = -1;
            }
            c.cx = (int16_t)mx;
            c.cy = (int16_t)my;
            c.vx = (int32_t)view->ViewPosition.x;
            c.vy = (int32_t)view->ViewPosition.y;
            c.tool = (uint16_t)CIsoView::CurrentCommand->Command;
            int bx = view->BrushSizeX;
            c.brush = (uint8_t)(bx < 0 ? 0 : (bx > 255 ? 255 : bx));

            // 多选区域包围盒
            if (!MultiSelection::SelectedCoords.empty()) {
                int minX = 99999, minY = 99999, maxX = -1, maxY = -1;
                for (auto& mc : MultiSelection::SelectedCoords) {
                    if (mc.X < minX) minX = mc.X;
                    if (mc.X > maxX) maxX = mc.X;
                    if (mc.Y < minY) minY = mc.Y;
                    if (mc.Y > maxY) maxY = mc.Y;
                }
                if (maxX >= 0) {
                    c.hasSel = true;
                    c.sel.x0 = (int16_t)minX; c.sel.y0 = (int16_t)minY;
                    c.sel.x1 = (int16_t)maxX; c.sel.y1 = (int16_t)maxY;
                }
            }

            if (g_selfTestBadCursor) {
                static int badSeq = 0;
                ++badSeq;
                if (badSeq % 40 < 20) { c.cx = -1; c.cy = -1; }
                else { c.cx = 30000; c.cy = 30000; }
            } else if (mx < 0 || my < 0) {
                c.cx = -1; c.cy = -1;   // 让对端 OnCursor 走"忽略"分支
            }

            char key[128];
            _snprintf_s(key, sizeof(key), _TRUNCATE, "%d/%d/%u/%u/%d/%d,%d,%d,%d", c.cx, c.cy, c.tool, c.brush,
                        c.hasSel ? 1 : 0, c.sel.x0, c.sel.y0, c.sel.x1, c.sel.y1);
            if (g_lastCursorKey != key) {
                g_lastCursorKey = key;
                g_session->SendCursor(c);
            }
        }

        if (g_objDirty && !g_objRefreshFailed) {
            g_objDirty = false;
            RefreshObjectLists();
        }

        if (g_iniDirty && CFinalSunDlg::Instance) {
            g_iniDirty = false;
            CFinalSunDlg::Instance->Update();
        }

        SyncOverlay();

        {   // 诊断：把对端格坐标换算成客户区坐标打日志，限速 1 秒
            static uint32_t lastLog = 0;
            uint32_t now32 = (uint32_t)GetTickCount();
            if (!g_peers.empty() && (now32 - lastLog) > 1000) {
                lastLog = now32;
                static std::map<uint32_t, std::pair<int,int>> lastPos;
                for (auto& kv : g_peers) {
                    int px = 0, py = 0;
                    if (view) MapToClient(view, kv.second.cx, kv.second.cy, px, py);
                    auto it = lastPos.find(kv.first);
                    if (it == lastPos.end() || it->second.first != px || it->second.second != py) {
                        lastPos[kv.first] = { px, py };
                        Logger::Info("[Collab] draw peer=%u cell=(%d,%d) client=(%d,%d)",
                                     kv.first, kv.second.cx, kv.second.cy, px, py);
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Logger::Error("[Collab] exception in Tick, disabling collab");
        Shutdown();
    }
}

void CCollabExt::Shutdown()
{
    if (g_session) { g_session->Shutdown(); g_session.reset(); }
    if (g_overlay) { DestroyWindow(g_overlay); g_overlay = nullptr; }
    if (g_hWnd) {
        if (g_timer) { KillTimer(g_hWnd, 1); g_timer = 0; }
        DestroyWindow(g_hWnd);
        g_hWnd = nullptr;
    }
    g_started = false;
    g_enabled = false;
}
