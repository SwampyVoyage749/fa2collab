// 传输层：非阻塞 TCP + 帧编解码
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "collab_protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace collab {

struct WinsockInit {
    WinsockInit();
    ~WinsockInit();
};

class Link {
public:
    Link() = default;
    ~Link() { close(); }
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    bool adopt(SOCKET s);                 // 接管一个已连接的 socket（设为非阻塞）
    void close();
    bool valid() const { return sock_ != INVALID_SOCKET; }
    SOCKET raw() const { return sock_; }

    // 发送：写入缓冲并在 pump() 中尽量发完
    void send(MsgType t, const Writer& w);
    void sendFramed(MsgType t, const void* payload, size_t len);

    // 收：返回 true 表示有完整帧；type/payload 由调用方解析
    bool recvFrame(MsgType& t, std::vector<uint8_t>& payload);
    // 处理读写事件；返回 false 表示连接已断开
    bool pump();

    std::string peer() const { return peer_; }
    void setPeer(const std::string& p) { peer_ = p; }

private:
    bool flushOut();

    SOCKET sock_ = INVALID_SOCKET;
    std::vector<uint8_t> in_;
    std::deque<std::vector<uint8_t>> out_;
    size_t outHead_ = 0;
    bool wantRead_ = false;
    std::string peer_;
};

class Listener {
public:
    bool open(uint16_t port);
    void close();
    bool valid() const { return sock_ != INVALID_SOCKET; }
    // 有连接则填充 link 并返回 true
    bool accept(Link& link, std::string* peerOut = nullptr);
    uint16_t port() const { return port_; }
private:
    SOCKET sock_ = INVALID_SOCKET;
    uint16_t port_ = 0;
};

// 把 sockaddr 转成 "ip:port"
std::string AddrToString(const sockaddr_in& a);

} // namespace collab
