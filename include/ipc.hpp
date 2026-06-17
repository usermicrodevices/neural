#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <stdexcept>

#include <arpa/inet.h>

#include "logger.hpp"

enum JobTypeAdmin {
    TRAIN,
    TRAIN_PROGRESS,
    TRAIN_DONE,
    SRC_TYPES,
    TRAIN_UML,
    TRAIN_UML_DONE,
    LIST_UML,
    LIST_UML_DONE,
    COMPOSE,
    COMPOSE_DONE,
    LIST_TABLES,
    GET_TABLE,
    SERIALIZE,
    SHUTDOWN,
    SEARCH_UML,
    SEARCH_UML_DONE,
    ADD_UMLEVENTS,
    ADD_UMLEVENTS_DONE,
    GET_UMLEVENTS,
    GET_UMLEVENTS_DONE,
    LIST_UMLEVENTS,
    LIST_UMLEVENTS_DONE
};

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

enum JobTypeClient : uint8_t {
    ASK = 0x01,
    ASK_DONE = 0x02,
    SEARCH_UML_CLIENT = 0x0A,
    SEARCH_UML_DONE_CLIENT = 0x0B,
    LIST_UML_CLIENT = 0x0C,
    LIST_UML_DONE_CLIENT = 0x0D,
    COMPOSE_CLIENT = 0x0E,
    COMPOSE_DONE_CLIENT = 0x0F,
    LIST_UMLEVENTS_CLIENT = 0x10,
    LIST_UMLEVENTS_DONE_CLIENT = 0x11,
};
