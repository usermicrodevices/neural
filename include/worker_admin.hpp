#pragma once

#include <csignal>
#include <unistd.h>

#include <sys/types.h>

#include <asio.hpp>

#include "logger.hpp"
#include "ipc.hpp"
#include "admin.hpp"

class WorkerAdmin {
public:
    WorkerAdmin(int child_fd, int admin_fd, int client_fd, size_t memory_usage=0);
    ~WorkerAdmin();
    pid_t getPid() const;

private:
    pid_t pid_;
    static void runChild(int child_fd, size_t memory_usage=0);
};
