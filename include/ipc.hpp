#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <stdexcept>

#include <arpa/inet.h>

#include "logger.hpp"

struct Message {
    uint8_t cmd;
    std::vector<uint8_t> payload;
};

class IpcSocket {
public:
    explicit IpcSocket(int fd);
    ~IpcSocket();
    void send(const Message& msg);
    Message recv();
    int fd() const;

private:
    int fd_;
    std::vector<uint8_t> pack_message(uint8_t cmd, const std::vector<uint8_t>& payload);
    void read_all(void* buf, size_t n);
    void write_all(const void* buf, size_t n);
};
