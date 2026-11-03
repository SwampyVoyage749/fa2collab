// 本地回环联测：Host + 2 Client，验证握手、光标路由、租约互斥、地形增量、断线释放
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../src/collab_session.h"
#include "../src/collab_inimerge.h"

using namespace collab;

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("  [FAIL] %s\n", msg); ++g_fail; } else { printf("  [ok]   %s\n", msg); } } while (0)

struct Recorder : Sink {
    std::vector<Peer> joined;
    std::vector<uint32_t> left;
    std::vector<CursorState> cursors;
    std::vector<std::pair<Rect, bool>> leases;
    std::vector<std::pair<uint32_t, size_t>> tiles;
    std::map<int, int> cellTiles;
    unsigned iniCount = 0;
    std::vector<std::string> chats;
    bool disconnected = false;

    void OnPeerJoin(const Peer& p) override { joined.push_back(p); }
    void OnPeerLeave(uint32_t id) override { left.push_back(id); }
    void OnCursor(const CursorState& c) override { cursors.push_back(c); }
    void OnLeaseResult(const Rect& r, bool ok, uint32_t, uint32_t) override { leases.emplace_back(r, ok); }
    void OnTileDelta(uint32_t, uint32_t chunk, const std::vector<CellEdit>& e) override {
        tiles.emplace_back(chunk, e.size());
        for (auto& cell : e) cellTiles[cell.index] = cell.tile;
    }
    void OnIniSection(const std::string&, const std::vector<std::pair<std::string,std::string>>&) override { ++iniCount; }
    void OnChat(uint32_t, const std::string& t) override { chats.push_back(t); }
    void OnDisconnected() override { disconnected = true; }
};

static bool Pump(std::vector<Session*>& ss, int rounds = 40) {
    for (int i = 0; i < rounds; ++i) {
        uint64_t now = GetTickCount64();
        for (auto* s : ss) s->Tick(now);
        Sleep(10);
    }
    return true;
}

int main() {
    WinsockInit wsa;

    Recorder ra, rb, rc;
    uint16_t port = 0;
    auto host = CreateHost(0, "host", 0xAABBCCDD, 30000, &ra, &port);
    CHECK(host != nullptr, "host 创建成功并绑定随机端口");
    if (!host) return 1;
    printf("  host 端口 = %u\n", (unsigned)port);

    auto ca = CreateClient("127.0.0.1", port, "alice", 0xAABBCCDD, &rb);
    auto cb = CreateClient("127.0.0.1", port, "bob", 0xAABBCCDD, &rc);
    CHECK(ca && cb, "两个客户端连接成功");

    std::vector<Session*> all{host.get(), ca.get(), cb.get()};
    Pump(all);

    CHECK(ca->MyId() != 0 && cb->MyId() != 0 && ca->MyId() != cb->MyId(), "客户端拿到不同且非 0 的玩家 id");
    printf("  [dbg] host peers=%zu alice peers=%zu bob peers=%zu\n",
           host->Peers().size(), ca->Peers().size(), cb->Peers().size());
    for (auto& p : ca->Peers()) printf("  [dbg] alice sees id=%u name=%s\n", p.id, p.name.c_str());
    for (auto& p : cb->Peers()) printf("  [dbg] bob sees id=%u name=%s\n", p.id, p.name.c_str());
    printf("  [dbg] my ids: alice=%u bob=%u\n", ca->MyId(), cb->MyId());
    CHECK(host->Peers().size() == 2, "Host 看到 2 个玩家");
    CHECK(ca->Peers().size() == 2, "alice 看到 2 个（host id=0 + bob）");

    // 光标路由
    CursorState c{};
    c.cx = 42; c.cy = 7; c.vx = 100; c.vy = -5; c.tool = 3; c.brush = 2; c.hasSel = true;
    c.sel = Rect{40, 5, 44, 9};
    ca->SendCursor(c);
    Pump(all);
    printf("  [dbg] cursors: host=%zu alice=%zu bob=%zu\n", ra.cursors.size(), rb.cursors.size(), rc.cursors.size());
    CHECK(!ra.cursors.empty() && ra.cursors.back().cx == 42 && ra.cursors.back().hasSel, "host 收到 alice 光标");
    CHECK(!rc.cursors.empty() && rc.cursors.back().player == ca->MyId(), "bob 收到转发光标且 sender 正确");

    // 租约互斥
    Rect r{0, 0, 1, 1};
    ca->RequestLease(r);
    Pump(all);
    CHECK(!rb.leases.empty() && rb.leases.back().second, "alice 抢到 (0,0)-(1,1) 区块");
    // alice 拿到租约后再发地形
    std::vector<CellEdit> edits{{0, 1, 100, -1}, {1, 2, 101, -1}};
    ca->SendTileDelta(ChunkKey(0, 0), edits);
    Pump(all);
    CHECK(!rc.tiles.empty() && rc.tiles.back().second == 2, "bob 收到 2 个单元格增量");

    cb->RequestLease(r);
    Pump(all);
    CHECK(!rc.leases.empty() && rc.leases.back().second == false, "bob 抢同一区块被拒");

    // 越权写入被拒
    cb->SendTileDelta(ChunkKey(0, 0), edits);
    Pump(all);
    CHECK(rc.leases.back().second == false, "bob 越权写被 Host 拒（LeaseDeny）");

    // 聊天
    ca->SendChat("hi");
    Pump(all);
    CHECK(!rc.chats.empty() && rc.chats.back() == "hi", "聊天转发成功");

    // 断线清理
    uint32_t removed = cb->MyId();
    cb->Shutdown();
    Pump(all);
    CHECK(!ra.left.empty() && ra.left.back() == removed, "Host 感知 bob 掉线");
    CHECK(host->Peers().size() == 1, "掉线后 Host 玩家数回到 1");

    // 释放后区块可被他人抢
    ca->ReleaseLease(r);
    Pump(all);
    rc.leases.clear();
    auto cc = CreateClient("127.0.0.1", port, "carol", 0xAABBCCDD, &rc);
    CHECK(cc != nullptr, "carol 重新连入");
    std::vector<Session*> all2{host.get(), ca.get(), cc.get()};
    Pump(all2);
    cc->RequestLease(r);
    Pump(all2);
    CHECK(!rc.leases.empty() && rc.leases.back().second, "区块释放后可被新玩家抢到");

    // ---- INI 节三方合并（纯算法）----
    {
        using M = std::map<std::string, std::string>;
        // 场景1：两人改不同键 -> 都保留
        M base1{{"0", "a"}, {"1", "b"}, {"2", "c"}};
        M local1{{"0", "A"}, {"1", "b"}, {"2", "c"}};
        M remote1{{"0", "a"}, {"1", "B"}, {"2", "c"}};
        auto r1 = MergeIniSection(base1, local1, remote1);
        CHECK(r1.conflicts.empty() && r1.merged["0"] == "A" && r1.merged["1"] == "B" && r1.merged["2"] == "c",
              "合并: 不同键互不影响");

        // 场景2：两人改同一键成不同值 -> 冲突，默认保留本地
        M local2{{"0", "A"}, {"1", "b"}, {"2", "c"}};
        M remote2{{"0", "Z"}, {"1", "b"}, {"2", "c"}};
        auto r2 = MergeIniSection(base1, local2, remote2);
        CHECK(r2.conflicts.size() == 1 && r2.conflicts[0] == "0" && r2.merged["0"] == "A",
              "合并: 同键冲突检测 + 本地优先");

        // 场景3：远端删除一个键、本地没动 -> 删除生效
        M local3{{"0", "a"}, {"1", "b"}, {"2", "c"}};
        M remote3{{"0", "a"}, {"2", "c"}};
        auto r3 = MergeIniSection(base1, local3, remote3);
        CHECK(r3.merged.count("1") == 0 && r3.conflicts.empty(), "合并: 单方删除生效");

        // 场景4：远端删除、本地改了同键 -> 冲突，保留本地改动
        M local4{{"0", "a"}, {"1", "B"}, {"2", "c"}};
        auto r4 = MergeIniSection(base1, local4, remote3);
        CHECK(r4.conflicts.size() == 1 && r4.merged["1"] == "B", "合并: 删/改冲突保留改动");

        // 场景5：新键（两边各加一个）-> 都保留
        M local5 = base1; local5["3"] = "L";
        M remote5 = base1; remote5["4"] = "R";
        auto r5 = MergeIniSection(base1, local5, remote5);
        CHECK(r5.merged.count("3") && r5.merged.count("4") && r5.conflicts.empty(), "合并: 双方新增键共存");
    }
    // Multiple edits queued before the lease arrives must accumulate by cell.
    {
        CellEdit first, second, latest;
        first.index = 200; first.tile = 10;
        second.index = 201; second.tile = 20;
        latest = first; latest.tile = 30;
        ca->SendTileDelta(ChunkKey(3,3), {first});
        ca->SendTileDelta(ChunkKey(3,3), {second});
        ca->SendTileDelta(ChunkKey(3,3), {latest});
        Pump(all2);
        CHECK(ra.cellTiles[200] == 30 && ra.cellTiles[201] == 20,
              "pending delta keeps different cells and latest value for the same cell");
        CHECK(rc.cellTiles[200] == 30 && rc.cellTiles[201] == 20,
              "merged pending delta reaches other peers");
    }
    // Raw peers exercise packet validation independently of our serializer.
    auto rawPeer = [&]() {
        auto link = std::make_unique<Link>();
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
        InetPtonA(AF_INET, "127.0.0.1", &a.sin_addr);
        if (::connect(sock, (sockaddr*)&a, sizeof(a)) != 0) {
            closesocket(sock); return std::unique_ptr<Link>();
        }
        link->adopt(sock); return link;
    };
    auto sendRaw = [&](Link& link, MsgType type, const Writer& w) {
        link.send(type,w); link.pump(); Pump(all2,10);
    };
    auto hello = [&](Link& link, const char* build) {
        Writer w; w.str("raw"); w.u32(0xAABBCCDD); w.str(build);
        sendRaw(link,MsgType::Hello,w);
    };
    {
        size_t before = ra.tiles.size();
        auto raw = rawPeer(); CHECK(raw != nullptr, "raw test connection established");
        if (raw) {
            Writer w; w.u32(0); w.u32(0);
            sendRaw(*raw, MsgType::TileDelta,w);
            CHECK(ra.tiles.size() == before, "pre-handshake tile packet has no side effects");
        }
    }
    {
        size_t before = host->Peers().size();
        auto raw = rawPeer();
        if (raw) { hello(*raw,"1.5.3"); }
        CHECK(raw && host->Peers().size() == before, "incompatible legacy build rejected");
    }
    {
        size_t before = ra.tiles.size();
        auto raw = rawPeer();
        if (raw) {
            hello(*raw,kBuildTag);
            Writer w; w.u32(0); w.u32(0xFFFFFFFF);
            sendRaw(*raw,MsgType::TileDelta,w);
        }
        CHECK(raw && ra.tiles.size() == before, "huge wire count rejected before allocation");
    }
    {
        unsigned before = ra.iniCount;
        auto raw = rawPeer();
        if (raw) {
            hello(*raw,kBuildTag);
            Writer w; w.str("Structures"); w.u16(1); w.str("0");
            sendRaw(*raw,MsgType::IniSection,w);
        }
        CHECK(raw && ra.iniCount == before, "truncated INI cannot apply partial section or delete keys");
    }
    {
        auto raw = rawPeer(); size_t before = ra.left.size();
        if (raw) { hello(*raw,kBuildTag); Writer w; sendRaw(*raw,MsgType::Bye,w); }
        CHECK(raw && ra.left.size() == before+1, "explicit Bye while socket stays open notifies leave");
    }
    {
        CellEdit e; e.index = 15;
        CHECK(CellEditInChunk(e, 1024, 32, ChunkKey(0,0)), "valid cell at chunk edge accepted");
        CHECK(!CellEditInChunk(e, 15, 32, ChunkKey(0,0)), "cell index equal to allocation count rejected");
        CHECK(!CellEditInChunk(e, 1024, 0, ChunkKey(0,0)), "zero stride rejected before division");
        CHECK(!CellEditInChunk(e, 1024, 32, ChunkKey(1,0)), "cell cannot write outside leased chunk");
        e.index = -1;
        CHECK(!CellEditInChunk(e, 1024, 32, 0), "negative cell index rejected");
        e.index = 0x7FFFFFFF;
        CHECK(!CellEditInChunk(e, 1024, 32, 0), "large positive index rejected");
        e.index = 0; e.overlay = 256;
        CHECK(!CellEditInChunk(e, 1024, 32, 0), "out of range field rejected before narrowing");
    }
    {
        Listener server; CHECK(server.open(0), "legacy server listener established");
        Recorder rec;
        auto client = CreateClient("127.0.0.1", server.port(), "new", 123, &rec);
        Link peer;
        CHECK(client && server.accept(peer), "new client connects to legacy server");
        if (client && peer.valid()) {
            Writer w; w.u32(1); w.u32(123); w.u32(30000); w.u16(0);
            peer.send(MsgType::Welcome,w); peer.pump(); client->Tick(GetTickCount64());
            CHECK(!client->Connected() && rec.joined.empty() && rec.disconnected,
                  "legacy Welcome rejected before any peer callback");
            client->Shutdown();
        }
        server.close();
    }
    host->Shutdown();
    printf("\n%s (%d 项失败)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
