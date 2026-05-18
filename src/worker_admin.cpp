#include "worker_admin.hpp"

void WorkerAdmin::runChild(int child_fd, size_t memory_usage) {
    IpcSocket sock(child_fd);
    asio::io_context io;
    HttpAdminSrv server(io, 8080);
    Logger::Info("WorkerAdmin::runChild: neural memory usage = {}", memory_usage);
    server.set_memory_usage(memory_usage);
    server.start();
    while (true) {
        JobAdmin job;
        if (!server.dequeue(job))
            break;
        switch(job.type){
            case JobAdmin::LEARN: {
                try {
                    sock.send(Message{0x01, job.data});
                    server.set_training_started();
                    while (true) {
                        Message msg = sock.recv();
                        if (msg.cmd == 0x02) {
                            int pct = msg.payload.empty() ? 0 : static_cast<int>(msg.payload[0]);
                            server.update_progress(pct);
                        } else if (msg.cmd == 0x03) {
                            if (msg.payload.size() >= sizeof(memory_usage))
                                std::memcpy(&memory_usage, msg.payload.data(), sizeof(memory_usage));
                            server.set_memory_usage(memory_usage);
                            server.set_training_done();
                            job.done.set_value();
                            break;
                        }
                    }
                } catch (const std::exception& err) {
                    job.done.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobAdmin::SERIALIZE: {
                //Logger::Trace("WorkerAdmin: processing SERIALIZE job");
                try {
                    //Logger::Trace("WorkerAdmin: sending CMD_SERIALIZE (0x08) with empty payload");
                    sock.send(Message{0x08, {}});
                    //Logger::Trace("WorkerAdmin: message sent, waiting for ACK");
                    Message ack = sock.recv();
                    //Logger::Trace("WorkerAdmin: received ACK, cmd={}, payload size={}", (int)ack.cmd, ack.payload.size());
                    if (ack.cmd == 0x09) {
                        job.done.set_value();
                        //Logger::Trace("WorkerAdmin: promise set");
                    } else {
                        throw std::runtime_error("unexpected response");
                    }
                } catch (const std::exception& err) {
                    Logger::Error("WorkerAdmin::runChild serialize error: {}", err.what());
                    job.done.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
        }
    }
    server.stop();
}

WorkerAdmin::WorkerAdmin(int child_fd, int admin_fd, int client_fd, size_t memory_usage) {
    pid_ = fork();
    if (pid_ == 0) {
        close(admin_fd);
        close(client_fd);
        runChild(child_fd, memory_usage);
        exit(0);
    } else if (pid_ > 0) {
        close(child_fd);
    } else {
        throw std::runtime_error("fork failed");
    }
}

WorkerAdmin::~WorkerAdmin() { if (pid_ > 0) kill(pid_, SIGTERM); }

pid_t WorkerAdmin::getPid() const { return pid_; }
