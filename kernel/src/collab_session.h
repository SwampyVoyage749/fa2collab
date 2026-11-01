// 会话层：Host / Client，含区块租约表与消息路由（单线程，Tick 驱动）
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "collab_protocol.h"
#include "collab_transport.h"

namespace collab {

struct Peer {
    uint32_t id = 0;
    std::string name;
};

struct CursorState {
    uint32_t player = 0;
    int16_t cx = 0, cy = 0;      // 当前瓦片
    int32_t vx = 0, vy = 0;      // 视口左上角瓦片偏移
    uint16_t tool = 0;
    uint8_t brush = 0;
    bool hasSel = false;
    Rect sel;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void OnPeerJoin(const Peer&) {}
    virtual void OnPeerLeave(uint32_t /*id*/) {}
    virtual void OnCursor(const CursorState&) {}
    virtual void OnLeaseResult(const Rect&, bool /*granted*/, uint32_t /*chunks*/, uint32_t /*owner*/) {}
    virtual void OnTileDelta(uint32_t /*from*/, uint32_t /*chunk*/, const std::vector<CellEdit>&) {}
    virtual void OnChat(uint32_t /*from*/, const std::string&) {}
    virtual void OnIniSection(const std::string& /*name*/,
                              const std::vector<std::pair<std::string, std::string>>& /*kv*/) {}
    virtual void OnDisconnected() {}
};

struct LeaseEntry {
    uint32_t owner = 0;
    uint64_t expiry = 0;   // GetTickCount64 毫秒
};

class LeaseTable {
public:
    bool Grant(const Rect& r, uint32_t owner, uint32_t ttlMs, uint64_t now);
    void Release(const Rect& r, uint32_t owner);
    bool Owned(uint32_t chunk, uint32_t owner, uint64_t now) const;
    uint32_t Owner(uint32_t chunk, uint64_t now) const;
    uint32_t CountOwnedBy(uint32_t owner, uint64_t now) const;
    uint32_t Expire(uint64_t now);          // 返回过期数量
    void ClearOwner(uint32_t owner);
    static void ForEachChunk(const Rect& r, const std::function<void(uint32_t)>& fn);
private:
    std::map<uint32_t, LeaseEntry> map_;
};

class Session {
public:
    virtual ~Session() = default;
    virtual void Tick(uint64_t now) = 0;
    virtual bool Connected() const = 0;
    virtual uint32_t MyId() const = 0;
    virtual std::vector<Peer> Peers() const = 0;
    virtual void SendCursor(const CursorState&) = 0;
    virtual void RequestLease(const Rect&) = 0;
    virtual void ReleaseLease(const Rect&) = 0;
    virtual void SendTileDelta(uint32_t chunk, const std::vector<CellEdit>&) = 0;
    virtual void SendChat(const std::string&) = 0;
    virtual void SendIniSection(const std::string& name,
                                const std::vector<std::pair<std::string, std::string>>& kv) = 0;
    virtual void Shutdown() = 0;
};

std::unique_ptr<Session> CreateHost(uint16_t port, const std::string& name, uint32_t mapHash,
                                    uint32_t leaseTtlMs, Sink* sink, uint16_t* actualPort = nullptr);
std::unique_ptr<Session> CreateClient(const std::string& host, uint16_t port, const std::string& name,
                                      uint32_t mapHash, Sink* sink);

} // namespace collab
