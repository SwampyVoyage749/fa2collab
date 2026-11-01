#include "collab_transport.h"

#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

namespace collab {

WinsockInit::WinsockInit() {
    WSADATA d{};
    WSAStartup(MAKEWORD(2, 2), &d);
}
WinsockInit::~WinsockInit() { WSACleanup(); }

static void SetNonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

static void SetNoDelay(SOCKET s, bool on) {
    BOOL v = on ? TRUE : FALSE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&v, sizeof(v));
}

std::string AddrToString(const sockaddr_in& a) {
    char ip[64] = {0};
    inet_ntop(AF_INET, (const void*)&a.sin_addr, ip, sizeof(ip));
    char buf[80];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s:%u", ip, (unsigned)ntohs(a.sin_port));
    return std::string(buf);
}

bool Link::adopt(SOCKET s) {
    if (s == INVALID_SOCKET) return false;
    close();
    sock_ = s;
    SetNonBlocking(sock_);
    SetNoDelay(sock_, true);
    return true;
}

void Link::close() {
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    in_.clear();
    out_.clear();
    outHead_ = 0;
}

void Link::sendFramed(MsgType t, const void* payload, size_t len) {
    if (!valid()) return;
    std::vector<uint8_t> f(kHeaderSize + len);
    uint32_t magic = kMagic, l = uint32_t(len);
    uint16_t type = uint16_t(t), flags = 0;
    memcpy(&f[0], &magic, 4);
    memcpy(&f[4], &type, 2);
    memcpy(&f[6], &flags, 2);
    memcpy(&f[8], &l, 4);
    if (len) memcpy(&f[kHeaderSize], payload, len);
    out_.push_back(std::move(f));
}

void Link::send(MsgType t, const Writer& w) {
    if (w.size() > kMaxPayload) return;
    sendFramed(t, w.b.empty() ? nullptr : w.b.data(), w.size());
}

bool Link::flushOut() {
    while (!out_.empty()) {
        auto& f = out_.front();
        size_t remaining = f.size() - outHead_;
        int n = ::send(sock_, (const char*)f.data() + outHead_, (int)remaining, 0);
        if (n > 0) {
            outHead_ += size_t(n);
            if (outHead_ == f.size()) {
                out_.pop_front();
                outHead_ = 0;
            }
        } else {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return true;
            if (e == WSAENOTCONN || e == WSAECONNRESET) return false;
            return false;
        }
    }
    return true;
}

bool Link::pump() {
    if (!valid()) return false;
    if (!flushOut()) return false;

    // 读：一次尝试读一坨，直到 WSAEWOULDBLOCK
    char buf[16384];
    for (;;) {
        int n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n > 0) {
            in_.insert(in_.end(), buf, buf + n);
        } else if (n == 0) {
            return false; // 对端关闭
        } else {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) break;
            if (e == WSAEINTR) continue;
            return false;
        }
    }
    return true;
}

bool Link::recvFrame(MsgType& t, std::vector<uint8_t>& payload) {
    if (in_.size() < kHeaderSize) return false;
    uint32_t magic = 0, len = 0;
    uint16_t type = 0;
    memcpy(&magic, &in_[0], 4);
    memcpy(&type, &in_[4], 2);
    memcpy(&len, &in_[8], 4);
    if (magic != kMagic || len > kMaxPayload) {
        // 协议错乱：直接断开，避免越界
        in_.clear();
        return false;
    }
    if (in_.size() < kHeaderSize + len) return false;
    payload.assign(in_.begin() + kHeaderSize, in_.begin() + kHeaderSize + len);
    in_.erase(in_.begin(), in_.begin() + kHeaderSize + len);
    t = MsgType(type);
    return true;
}

bool Listener::open(uint16_t port) {
    close();
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    BOOL reuse = TRUE;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(sock_, (sockaddr*)&a, sizeof(a)) != 0) { close(); return false; }
    if (listen(sock_, 8) != 0) { close(); return false; }
    SetNonBlocking(sock_);
    sockaddr_in got{};
    int gl = sizeof(got);
    getsockname(sock_, (sockaddr*)&got, &gl);
    port_ = ntohs(got.sin_port);
    return true;
}

void Listener::close() {
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
}

bool Listener::accept(Link& link, std::string* peerOut) {
    if (!valid()) return false;
    sockaddr_in a{};
    int al = sizeof(a);
    SOCKET s = ::accept(sock_, (sockaddr*)&a, &al);
    if (s == INVALID_SOCKET) return false;
    std::string p = AddrToString(a);
    if (!link.adopt(s)) return false;
    link.setPeer(p);
    if (peerOut) *peerOut = p;
    return true;
}

} // namespace collab
