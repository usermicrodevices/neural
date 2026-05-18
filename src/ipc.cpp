#include "ipc.hpp"

IpcSocket::IpcSocket(int fd) : fd_(fd) {}

IpcSocket::~IpcSocket() { if (fd_ != -1) close(fd_); }

int IpcSocket::fd() const { return fd_; }

std::vector<uint8_t> IpcSocket::pack_message(uint8_t cmd, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> m;
    m.reserve(5 + payload.size());
    m.push_back(cmd);
    uint32_t len = payload.size();
    m.push_back( static_cast<uint8_t>(len) );
    m.push_back( static_cast<uint8_t>(len >> 8) );
    m.push_back( static_cast<uint8_t>(len >> 16) );
    m.push_back( static_cast<uint8_t>(len >> 24) );
    m.insert(m.end(), payload.begin(), payload.end());
    //Logger::Trace("IpcSocket::pack_message: created vector size {}", m.size());
    return m;
}

void IpcSocket::send(const Message& msg) {
    //Logger::Trace("IpcSocket::send: cmd={}, payload size={}", msg.cmd, msg.payload.size());
    auto data = pack_message(msg.cmd, msg.payload);
    //Logger::Trace("IpcSocket::send: packed size={}", data.size());
    write_all(data.data(), data.size());
    //Logger::Trace("IpcSocket::send: write_all done");
}

Message IpcSocket::recv() {
    //Logger::Trace("IpcSocket::recv: reading header");
    uint8_t header[5];
    read_all(header, 5);
    uint32_t len = static_cast<uint32_t>(header[1]) |
                   (static_cast<uint32_t>(header[2]) << 8) |
                   (static_cast<uint32_t>(header[3]) << 16) |
                   (static_cast<uint32_t>(header[4]) << 24);
    //Logger::Trace("IpcSocket::recv: cmd={}, payload len={}", header[0], len);
    std::vector<uint8_t> payload(len);
    if (len > 0)
        read_all(payload.data(), len);
    //Logger::Trace("IpcSocket::recv: read payload of {} bytes", len);
    return Message{header[0], std::move(payload)};
}

void IpcSocket::read_all(void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd_, (uint8_t*)buf + off, n - off);
        if (r <= 0) throw std::runtime_error("IpcSocket read failed");
        off += r;
    }
}

void IpcSocket::write_all(const void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd_, (const uint8_t*)buf + off, n - off);
        if (w <= 0) throw std::runtime_error("IpcSocket write failed");
        off += w;
    }
}
