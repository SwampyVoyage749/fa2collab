#include "collab_session.h"

#include <algorithm>
#include <functional>

namespace collab {

// ---------------- LeaseTable ----------------
void LeaseTable::ForEachChunk(const Rect& r, const std::function<void(uint32_t)>& fn) {
    int x0 = std::min(r.x0, r.x1), x1 = std::max(r.x0, r.x1);
    int y0 = std::min(r.y0, r.y1), y1 = std::max(r.y0, r.y1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            fn(ChunkKey(x, y));
}

bool LeaseTable::Grant(const Rect& r, uint32_t owner, uint32_t ttlMs, uint64_t now) {
    bool free_ = true;
    ForEachChunk(r, [&](uint32_t k) {
        auto it = map_.find(k);
        if (it != map_.end() && it->second.expiry > now && it->second.owner != owner) free_ = false;
    });
    if (!free_) return false;
    ForEachChunk(r, [&](uint32_t k) {
        LeaseEntry e; e.owner = owner; e.expiry = now + ttlMs;
        map_[k] = e;
    });
    return true;
}

void LeaseTable::Release(const Rect& r, uint32_t owner) {
    ForEachChunk(r, [&](uint32_t k) {
        auto it = map_.find(k);
        if (it != map_.end() && it->second.owner == owner) map_.erase(it);
    });
}

bool LeaseTable::Owned(uint32_t chunk, uint32_t owner, uint64_t now) const {
    auto it = map_.find(chunk);
    return it != map_.end() && it->second.owner == owner && it->second.expiry > now;
}

uint32_t LeaseTable::Owner(uint32_t chunk, uint64_t now) const {
    auto it = map_.find(chunk);
    if (it == map_.end() || it->second.expiry <= now) return 0;
    return it->second.owner;
}

uint32_t LeaseTable::CountOwnedBy(uint32_t owner, uint64_t now) const {
    uint32_t n = 0;
    for (auto& kv : map_)
        if (kv.second.owner == owner && kv.second.expiry > now) ++n;
    return n;
}

uint32_t LeaseTable::Expire(uint64_t now) {
    uint32_t n = 0;
    for (auto it = map_.begin(); it != map_.end();) {
        if (it->second.expiry <= now) { it = map_.erase(it); ++n; }
        else ++it;
    }
    return n;
}

void LeaseTable::ClearOwner(uint32_t owner) {
    for (auto it = map_.begin(); it != map_.end();) {
        if (it->second.owner == owner) it = map_.erase(it);
        else ++it;
    }
}

// ---------------- 序列化辅助 ----------------
static void WriteRect(Writer& w, const Rect& r) {
    w.i16(r.x0); w.i16(r.y0); w.i16(r.x1); w.i16(r.y1);
}
static Rect ReadRect(Reader& rd) {
    Rect r;
    r.x0 = rd.i16(); r.y0 = rd.i16(); r.x1 = rd.i16(); r.y1 = rd.i16();
    return r;
}
static void WriteCursor(Writer& w, const CursorState& c) {
    w.u32(c.player); w.i16(c.cx); w.i16(c.cy); w.i32(c.vx); w.i32(c.vy);
    w.u16(c.tool); w.u8(c.brush); w.u8(c.hasSel ? 1 : 0);
    if (c.hasSel) WriteRect(w, c.sel);
}
static CursorState ReadCursor(Reader& rd) {
    CursorState c;
    c.player = rd.u32(); c.cx = rd.i16(); c.cy = rd.i16();
    c.vx = rd.i32(); c.vy = rd.i32(); c.tool = rd.u16(); c.brush = rd.u8();
    c.hasSel = rd.u8() != 0;
    if (c.hasSel) c.sel = ReadRect(rd);
    return c;
}

constexpr uint64_t kPingIntervalMs = 2000;
constexpr uint64_t kTimeoutMs = 30000;

// ---------------- Host ----------------
class HostSession : public Session {
public:
    HostSession(uint16_t port, std::string name, uint32_t mapHash, uint32_t ttl, Sink* sink, uint16_t* actualPort)
        : name_(std::move(name)), mapHash_(mapHash), ttl_(ttl), sink_(sink) {
        if (listener_.open(port) && actualPort) *actualPort = listener_.port();
    }
    ~HostSession() override { Shutdown(); }

    bool Connected() const override { return listener_.valid(); }
    uint32_t MyId() const override { return myId_; }

    std::vector<Peer> Peers() const override {
        std::vector<Peer> v;
        for (auto& c : conns_)
            if (c->hello) v.push_back(Peer{c->id, c->name});
        return v;
    }

    void SendCursor(const CursorState& c) override {
        Writer w; WriteCursor(w, c);
        Broadcast(MsgType::Cursor, w, 0);
    }

    void RequestLease(const Rect& r) override {
        // Host 是权威方，自己直接判定
        uint64_t now = GetTickCount64();
        bool ok = leases_.Grant(r, myId_, ttl_, now);
        if (sink_) sink_->OnLeaseResult(r, ok, ok ? 1u : 0u, 0);
    }

    void ReleaseLease(const Rect& r) override { leases_.Release(r, myId_); }

    void SendTileDelta(uint32_t chunk, const std::vector<CellEdit>& edits) override {
        // Host 是权威方：自己改图不需要租约
        Writer w; w.u32(chunk); w.u32(uint32_t(edits.size()));
        for (auto& e : edits) { w.i32(e.index); w.i16(e.height); w.i32(e.tile); w.i16(e.sub); w.i32(e.overlay); w.i32(e.ovdata); w.i16(e.unit); w.i16(e.inf0); w.i16(e.inf1); w.i16(e.inf2); w.i16(e.air); w.i16(e.struc); }
        if (sink_) sink_->OnTileDelta(myId_, chunk, edits);
        Broadcast(MsgType::TileDelta, w, 0);
    }

    void SendIniSection(const std::string& name,
                        const std::vector<std::pair<std::string, std::string>>& kv) override {
        if (sink_) sink_->OnIniSection(name, kv);
        Writer w; w.str(name); w.u16((uint16_t)kv.size());
        for (auto& p : kv) { w.str(p.first); w.str(p.second); }
        Broadcast(MsgType::IniSection, w, 0);
    }

    void SendChat(const std::string& t) override {
        Writer w; w.u32(myId_); w.str(t);
        if (sink_) sink_->OnChat(myId_, t);
        Broadcast(MsgType::Chat, w, 0);
    }

    void Shutdown() override {
        for (auto& c : conns_) c->link->close();
        conns_.clear();
        listener_.close();
    }

    void Tick(uint64_t now) override {
        if (!listener_.valid()) return;

        for (;;) {
            auto c = std::make_unique<Conn>();
            std::string peer;
            if (!listener_.accept(*c->link, &peer)) break;
            c->id = nextId_++;
            c->lastRecv = now;
            c->link->setPeer(peer);
            conns_.push_back(std::move(c));
        }

        for (auto& c : conns_) {
            if (!c->link->pump()) { DropConn(c.get()); continue; }
            MsgType t; std::vector<uint8_t> p;
            while (c->link->recvFrame(t, p)) {
                c->lastRecv = now;
                Handle(*c, t, p.data(), p.size(), now);
            }
            if (now - c->lastRecv > kTimeoutMs) DropConn(c.get());
        }

        conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                    [](const std::unique_ptr<Conn>& c) { return !c->link->valid(); }),
                     conns_.end());

        leases_.Expire(now);
        if (now - lastPing_ > kPingIntervalMs) {
            lastPing_ = now;
            Writer w; w.u32(uint32_t(now & 0xFFFFFFFFu));
            Broadcast(MsgType::Ping, w, 0);
        }
    }

private:
    struct Conn {
        std::unique_ptr<Link> link = std::make_unique<Link>();
        uint32_t id = 0;
        std::string name;
        bool hello = false;
        uint64_t lastRecv = 0;
    };

    void DropConn(Conn* c) {
        if (!c || !c->link->valid()) return;
        c->link->close();
        if (c->hello) {
            leases_.ClearOwner(c->id);
            Writer w; w.u32(c->id);
            Broadcast(MsgType::PeerLeave, w, c->id);
            if (sink_) sink_->OnPeerLeave(c->id);
        }
    }

    void Broadcast(MsgType t, const Writer& w, uint32_t exceptId) {
        for (auto& c : conns_) {
            if (!c->link->valid() || !c->hello || c->id == exceptId) continue;
            c->link->send(t, w);
        }
    }

    void Handle(Conn& c, MsgType t, const void* data, size_t len, uint64_t now) {
        Reader rd(data, len);
        switch (t) {
        case MsgType::Hello: {
            c.name = rd.str();
            uint32_t hash = rd.u32();
            rd.str();  // build
            if (hash != mapHash_) { c.link->close(); return; }

            // 先取"其他人"，再把自己标记为已握手，避免 Welcome 里出现自己
            std::vector<Peer> others = Peers();
            others.insert(others.begin(), Peer{myId_, name_});   // Host 自己也算房间里的一员（id=0）
            c.hello = true;

            Writer w;
            w.u32(c.id); w.u32(mapHash_); w.u32(ttl_);
            w.u16(uint16_t(others.size()));
            for (auto& p : others) { w.u32(p.id); w.str(p.name); }
            c.link->send(MsgType::Welcome, w);

            Writer j; j.u32(c.id); j.str(c.name);
            Broadcast(MsgType::PeerJoin, j, c.id);
            if (sink_) sink_->OnPeerJoin(Peer{c.id, c.name});
            break;
        }
        case MsgType::Cursor: {
            CursorState cs = ReadCursor(rd);
            cs.player = c.id;
            if (sink_) sink_->OnCursor(cs);
            Writer w; WriteCursor(w, cs);
            Broadcast(MsgType::Cursor, w, c.id);
            break;
        }
        case MsgType::LeaseReq: {
            Rect r = ReadRect(rd);
            bool ok = leases_.Grant(r, c.id, ttl_, now);
            if (ok) {
                Writer w; w.u32(1); WriteRect(w, r); w.u32(ttl_);
                c.link->send(MsgType::LeaseGrant, w);
            } else {
                uint32_t owner = leases_.Owner(ChunkKey(r.x0, r.y0), now);
                Writer w; WriteRect(w, r); w.u32(owner);
                c.link->send(MsgType::LeaseDeny, w);
            }
            break;
        }
        case MsgType::LeaseRelease: {
            Rect r = ReadRect(rd);
            leases_.Release(r, c.id);
            break;
        }
        case MsgType::TileDelta: {
            uint32_t chunk = rd.u32();
            uint32_t n = rd.u32();
            std::vector<CellEdit> edits;
            edits.reserve(n);
            for (uint32_t i = 0; i < n && rd.ok; ++i) {
                CellEdit e; e.index = rd.i32(); e.height = rd.i16(); e.tile = rd.i32(); e.sub = rd.i16(); e.overlay = rd.i32(); e.ovdata = rd.i32(); e.unit = rd.i16(); e.inf0 = rd.i16(); e.inf1 = rd.i16(); e.inf2 = rd.i16(); e.air = rd.i16(); e.struc = rd.i16();
                edits.push_back(e);
            }
            if (!leases_.Owned(chunk, c.id, now)) {
                // 块空闲则补授；被别人占着就拒绝，绝不覆盖
                bool ok = leases_.Grant(Rect{int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk)),
                                             int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk))},
                                        c.id, ttl_, now);
                if (!ok) {
                    Writer w; WriteRect(w, Rect{int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk)),
                                                int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk))});
                    w.u32(leases_.Owner(chunk, now));
                    c.link->send(MsgType::LeaseDeny, w);
                    return;
                }
            }
            if (sink_) sink_->OnTileDelta(c.id, chunk, edits);
            Writer w; w.u32(chunk); w.u32(uint32_t(edits.size()));
            for (auto& e : edits) { w.i32(e.index); w.i16(e.height); w.i32(e.tile); w.i16(e.sub); w.i32(e.overlay); w.i32(e.ovdata); w.i16(e.unit); w.i16(e.inf0); w.i16(e.inf1); w.i16(e.inf2); w.i16(e.air); w.i16(e.struc); }
            Broadcast(MsgType::TileDelta, w, c.id);
            break;
        }
        case MsgType::IniSection: {
            std::string name = rd.str();
            uint16_t n = rd.u16();
            std::vector<std::pair<std::string, std::string>> kv;
            for (uint16_t i = 0; i < n && rd.ok; ++i) {
                std::string k = rd.str();
                std::string v = rd.str();
                kv.emplace_back(std::move(k), std::move(v));
            }
            if (sink_) sink_->OnIniSection(name, kv);
            Writer w; w.str(name); w.u16((uint16_t)kv.size());
            for (auto& p : kv) { w.str(p.first); w.str(p.second); }
            Broadcast(MsgType::IniSection, w, c.id);
            break;
        }
        case MsgType::Chat: {
            std::string text = rd.str();
            if (sink_) sink_->OnChat(c.id, text);
            Writer w; w.u32(c.id); w.str(text);
            Broadcast(MsgType::Chat, w, c.id);
            break;
        }
        case MsgType::Bye:
            c.link->close();
            break;
        default:
            break;
        }
    }

    Listener listener_;
    std::vector<std::unique_ptr<Conn>> conns_;
    uint32_t nextId_ = 1;
    uint32_t myId_ = 0;   // Host 自身视为 0 号玩家
    std::string name_;
    std::string host_;
    uint16_t port_ = 0;
    uint64_t lastRetry_ = 0;
    uint64_t lastRecv_ = 0;
    uint32_t mapHash_;
    uint32_t ttl_;
    Sink* sink_;
    LeaseTable leases_;
    uint64_t lastPing_ = 0;
};

// ---------------- Client ----------------
class ClientSession : public Session {
public:
    ClientSession(const std::string& host, uint16_t port, std::string name, uint32_t mapHash, Sink* sink)
        : name_(std::move(name)), mapHash_(mapHash), sink_(sink) {
        host_ = host; port_ = port;
        if (Connect(host, port)) {
            Writer w;
            w.str(name_); w.u32(mapHash_); w.str(kBuildTag);
            link_.send(MsgType::Hello, w);
            connected_ = true;
        }
    }
    ~ClientSession() override { Shutdown(); }

    bool Connected() const override { return connected_ && link_.valid(); }
    uint32_t MyId() const override { return myId_; }

    std::vector<Peer> Peers() const override { return peers_; }

    void SendCursor(const CursorState& c) override {
        if (!Connected()) return;
        CursorState copy = c;
        copy.player = myId_;
        Writer w; WriteCursor(w, copy);
        link_.send(MsgType::Cursor, w);
    }

    void RequestLease(const Rect& r) override {
        if (!Connected()) return;
        Writer w; WriteRect(w, r);
        link_.send(MsgType::LeaseReq, w);
    }

    void ReleaseLease(const Rect& r) override {
        if (!Connected()) return;
        Writer w; WriteRect(w, r);
        link_.send(MsgType::LeaseRelease, w);
    }

    void SendTileDelta(uint32_t chunk, const std::vector<CellEdit>& edits) override {
        if (!Connected()) return;
        // 先排队，拿到租约再发，避免“未授权就广播”的竞态
        pending_[chunk] = edits;
        RequestLease(Rect{int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk)),
                          int16_t(ChunkX(chunk)), int16_t(ChunkY(chunk))});   // 块坐标
    }

    void SendChat(const std::string& t) override {
        if (!Connected()) return;
        Writer w; w.str(t);   // 发送方 id 由 Host 填写，客户端不乱塞
        link_.send(MsgType::Chat, w);
    }

    void SendIniSection(const std::string& name,
                        const std::vector<std::pair<std::string, std::string>>& kv) override {
        if (!Connected()) return;
        Writer w; w.str(name); w.u16((uint16_t)kv.size());
        for (auto& p : kv) { w.str(p.first); w.str(p.second); }
        link_.send(MsgType::IniSection, w);
    }

    void Shutdown() override {
        if (link_.valid()) {
            Writer w;
            link_.send(MsgType::Bye, w);
            link_.pump();
        }
        link_.close();
        connected_ = false;
        stopped_ = true;
    }

    void Tick(uint64_t now) override {
        if (stopped_) return;
        // 断线自动重连：UI 线程偶尔会被加载地图阻塞，不能靠一次握手定生死
        if (!connected_ || !link_.valid()) {
            if (now - lastRetry_ > 3000) {
                lastRetry_ = now;
                if (Connect(host_, port_)) {
                    Writer w;
                    w.str(name_); w.u32(mapHash_); w.str(kBuildTag);
                    link_.send(MsgType::Hello, w);
                    connected_ = true;
                    peers_.clear();
                    myId_ = 0;
                    lastPing_ = now;
                    lastRecv_ = now;
                }
            }
            return;
        }
        if (!link_.pump()) {
            if (connected_) { connected_ = false; if (sink_) sink_->OnDisconnected(); }
            link_.close();
            return;
        }
        MsgType t; std::vector<uint8_t> p;
        while (link_.recvFrame(t, p)) Handle(t, p.data(), p.size());
        if (now - lastPing_ > kPingIntervalMs) {
            lastPing_ = now;
            Writer w; w.u32(uint32_t(now & 0xFFFFFFFFu));
            link_.send(MsgType::Ping, w);
        }
    }

private:
    bool Connect(const std::string& host, uint16_t port) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        if (InetPtonA(AF_INET, host.c_str(), &a.sin_addr) != 1) { closesocket(s); return false; }
        // 非阻塞连接 + 3 秒超时
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        int rc = ::connect(s, (sockaddr*)&a, sizeof(a));
        if (rc != 0) {
            int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK) { closesocket(s); return false; }
            fd_set wf, ef;
            FD_ZERO(&wf); FD_SET(s, &wf);
            FD_ZERO(&ef); FD_SET(s, &ef);
            timeval tv{6, 0};
            int sel = select(0, nullptr, &wf, &ef, &tv);
            if (sel <= 0 || FD_ISSET(s, &ef)) { closesocket(s); return false; }
        }
        return link_.adopt(s);
    }

    void Handle(MsgType t, const void* data, size_t len) {
        Reader rd(data, len);
        switch (t) {
        case MsgType::Welcome: {
            myId_ = rd.u32();
            rd.u32();           // mapHash
            rd.u32();           // ttl
            uint16_t n = rd.u16();
            peers_.clear();
            for (uint16_t i = 0; i < n; ++i) {
                Peer p; p.id = rd.u32(); p.name = rd.str();
                peers_.push_back(p);
                if (sink_) sink_->OnPeerJoin(p);   // 已在房间里的玩家也要通知一遍
            }
            break;
        }
        case MsgType::PeerJoin: {
            Peer p; p.id = rd.u32(); p.name = rd.str();
            bool exists = false;
            for (auto& q : peers_) if (q.id == p.id) exists = true;
            if (!exists) peers_.push_back(p);
            if (sink_ && !exists) sink_->OnPeerJoin(p);
            break;
        }
        case MsgType::PeerLeave: {
            uint32_t id = rd.u32();
            peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                        [id](const Peer& p) { return p.id == id; }),
                         peers_.end());
            if (sink_) sink_->OnPeerLeave(id);
            break;
        }
        case MsgType::Cursor:
            if (sink_) sink_->OnCursor(ReadCursor(rd));
            break;
        case MsgType::LeaseGrant: {
            uint32_t n = rd.u32();
            Rect r = ReadRect(rd);
            rd.u32();
            if (sink_) sink_->OnLeaseResult(r, true, n, 0);

            uint32_t chunk = ChunkKey(r.x0, r.y0);   // 租约矩形是块坐标
            auto it = pending_.find(chunk);
            if (it != pending_.end()) {
                Writer w; w.u32(chunk); w.u32(uint32_t(it->second.size()));
                for (auto& e : it->second) {
                    w.i32(e.index); w.i16(e.height); w.i32(e.tile); w.i16(e.sub);
                    w.i32(e.overlay); w.i32(e.ovdata);
                    w.i16(e.unit); w.i16(e.inf0); w.i16(e.inf1); w.i16(e.inf2);
                    w.i16(e.air); w.i16(e.struc);
                }
                link_.send(MsgType::TileDelta, w);
                pending_.erase(it);
            }
            break;
        }
        case MsgType::LeaseDeny: {
            Rect r = ReadRect(rd);
            uint32_t owner = rd.u32();
            pending_.erase(ChunkKey(r.x0, r.y0));
            if (sink_) sink_->OnLeaseResult(r, false, 0, owner);
            break;
        }
        case MsgType::TileDelta: {
            uint32_t chunk = rd.u32();
            uint32_t n = rd.u32();
            std::vector<CellEdit> edits;
            edits.reserve(n);
            for (uint32_t i = 0; i < n && rd.ok; ++i) {
                CellEdit e; e.index = rd.i32(); e.height = rd.i16(); e.tile = rd.i32(); e.sub = rd.i16(); e.overlay = rd.i32(); e.ovdata = rd.i32(); e.unit = rd.i16(); e.inf0 = rd.i16(); e.inf1 = rd.i16(); e.inf2 = rd.i16(); e.air = rd.i16(); e.struc = rd.i16();
                edits.push_back(e);
            }
            if (sink_) sink_->OnTileDelta(0, chunk, edits);
            break;
        }

        case MsgType::IniSection: {
            std::string name = rd.str();
            uint16_t n = rd.u16();
            std::vector<std::pair<std::string, std::string>> kv;
            for (uint16_t i = 0; i < n && rd.ok; ++i) {
                std::string k = rd.str();
                std::string v = rd.str();
                kv.emplace_back(std::move(k), std::move(v));
            }
            if (sink_) sink_->OnIniSection(name, kv);
            break;
        }
        case MsgType::Chat: {
            uint32_t from = rd.u32();
            std::string text = rd.str();
            if (sink_) sink_->OnChat(from, text);
            break;
        }
        default:
            break;
        }
    }

    Link link_;
    std::string name_;
    std::string host_;
    uint16_t port_ = 0;
    uint64_t lastRetry_ = 0;
    uint64_t lastRecv_ = 0;
    uint32_t mapHash_;
    Sink* sink_;
    uint32_t myId_ = 0;
    bool connected_ = false;
    bool stopped_ = false;
    std::vector<Peer> peers_;
    std::map<uint32_t, std::vector<CellEdit>> pending_;
    uint64_t lastPing_ = 0;
};

// ---------------- 工厂 ----------------
std::unique_ptr<Session> CreateHost(uint16_t port, const std::string& name, uint32_t mapHash,
                                    uint32_t leaseTtlMs, Sink* sink, uint16_t* actualPort) {
    auto s = std::make_unique<HostSession>(port, name, mapHash, leaseTtlMs, sink, actualPort);
    if (!s->Connected()) return nullptr;
    return s;
}

std::unique_ptr<Session> CreateClient(const std::string& host, uint16_t port, const std::string& name,
                                      uint32_t mapHash, Sink* sink) {
    auto s = std::make_unique<ClientSession>(host, port, name, mapHash, sink);
    if (!s->Connected()) return nullptr;
    return s;
}

} // namespace collab
