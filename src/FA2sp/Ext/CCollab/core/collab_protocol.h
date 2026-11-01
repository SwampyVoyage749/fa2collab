// FA2SP Collab core — 协议层（不依赖 FA2 内部结构，可独立单测）
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace collab {

constexpr uint32_t kMagic = 0x43464132u;   // "2AFC" 小端
constexpr uint32_t kProtoVersion = 1;
constexpr const char* kBuildTag = "1.5.3";

// 头：magic(4) type(2) flags(2) len(4)
constexpr size_t kHeaderSize = 12;
constexpr size_t kMaxPayload = 1u << 20;   // 1 MiB 上限，防恶意/错包

enum class MsgType : uint16_t {
    Hello       = 1,
    Welcome     = 2,
    PeerJoin    = 3,
    PeerLeave   = 4,
    Cursor      = 5,
    LeaseReq    = 6,
    LeaseGrant  = 7,
    LeaseDeny   = 8,
    LeaseRelease = 9,
    TileDelta   = 10,
    ObjDelta    = 11,
    Chat        = 12,
    Ping        = 13,
    Bye         = 14,
    IniSection  = 15,   // 地图 INI 单节同步（触发/小队/脚本等都在这里）
};

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v)   { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void i32(int32_t v)  { u32(uint32_t(v)); }
    void i16(int16_t v)  { u16(uint16_t(v)); }
    void str(const std::string& s) { u16(uint16_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
    void raw(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
    size_t size() const { return b.size(); }
};

struct Reader {
    const uint8_t* p; size_t n; size_t off = 0;
    bool ok = true;
    Reader(const void* data, size_t len) : p((const uint8_t*)data), n(len) {}
    bool need(size_t k) { if (!ok || off + k > n) { ok = false; return false; } return true; }
    uint8_t  u8()  { if (!need(1)) return 0; return p[off++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = uint16_t(p[off] | (p[off + 1] << 8)); off += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = uint32_t(p[off]) | (uint32_t(p[off+1]) << 8)
                         | (uint32_t(p[off+2]) << 16) | (uint32_t(p[off+3]) << 24); off += 4; return v; }
    int32_t  i32() { return int32_t(u32()); }
    int16_t  i16() { return int16_t(u16()); }
    std::string str() {
        uint16_t len = u16(); if (!need(len)) return {};
        std::string s((const char*)p + off, len); off += len; return s;
    }
};

// ---- 消息体 ----
struct PeerInfo { uint32_t id = 0; std::string name; };

struct Rect { int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

// 单元格改动：单元格线性下标 + 高度 + 地形 + 覆盖图
struct CellEdit {
    int32_t index = 0;
    int16_t height = 0;
    int32_t tile = 0;
    int16_t sub = 0;
    int32_t overlay = 0;
    int32_t ovdata = 0;
    // 物件槽（-1 = 空）：车辆 / 三个步兵子格 / 飞行器 / 建筑
    int16_t unit = -1, inf0 = -1, inf1 = -1, inf2 = -1, air = -1, struc = -1;
};

inline uint32_t ChunkKey(int cx, int cy) { return (uint32_t(cy & 0xFFFF) << 16) | uint32_t(cx & 0xFFFF); }
inline int ChunkX(uint32_t k) { return int(k & 0xFFFF); }
inline int ChunkY(uint32_t k) { return int((k >> 16) & 0xFFFF); }

} // namespace collab
