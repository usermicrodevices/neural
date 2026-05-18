#pragma once

#include <csignal>
#include <unistd.h>
#include <sys/types.h>

#include <asio.hpp>

#include "logger.hpp"
#include "ipc.hpp"
#include "client.hpp"

class WorkerClient {
public:
    WorkerClient(int child_fd, int admin_fd, int client_fd);
    ~WorkerClient();
    pid_t getPid() const;

private:
    pid_t pid_;
    static void runChild(int child_fd);
};
