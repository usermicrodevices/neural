#pragma once

#include <algorithm>
#include <atomic>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "logger.hpp"
#include "store.hpp"
#include "ipc.hpp"
#include "worker_admin.hpp"
#include "worker_client.hpp"

class WorkerMaster {
public:
    WorkerMaster();
    ~WorkerMaster();
    void run();
    IpcSocket& getAdminSocket() const;
    IpcSocket& getClientSocket() const;
    DocumentStore& getStore();
    void stop();

private:
    std::unique_ptr<DocumentStore> store_;
    int admin_sv_[2];
    int client_sv_[2];
    std::unique_ptr<IpcSocket> admin_sock_;
    std::unique_ptr<IpcSocket> client_sock_;
    std::unique_ptr<WorkerAdmin> admin_worker_;
    std::unique_ptr<WorkerClient> client_worker_;
    std::atomic<bool> running_;
    pid_t admin_pid_;
    pid_t client_pid_;

    void createSocketpairs();
    void spawnWorkers();
};
