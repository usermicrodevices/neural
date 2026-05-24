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
                    if (job.data.size() >= 3) {
                        uint8_t serialize_flag = job.data[0];
                        uint16_t tag_len = ntohs(*(uint16_t*)&job.data[1]);
                        if (job.data.size() < 3 + tag_len)
                            throw std::runtime_error("Invalid LEARN payload: insufficient data");
                        std::string tags(job.data.begin()+3, job.data.begin()+3+tag_len);
                        std::string data(job.data.begin()+3+tag_len, job.data.end());
                        // Re‑pack for the master [flag][tags][data] with a delimiter
                        std::vector<uint8_t> master_payload;
                        master_payload.push_back(serialize_flag);
                        master_payload.insert(master_payload.end(), tags.begin(), tags.end());
                        master_payload.push_back(0); // delimiter
                        master_payload.insert(master_payload.end(), data.begin(), data.end());
                        sock.send(Message{0x01, master_payload});
                    } else
                        sock.send(Message{0x01, job.data});
                    server.set_training_started();
                    while (true) {
                        Message msg = sock.recv();
                        if (msg.cmd == 0x02) {
                            int pct = msg.payload.empty() ? 0 : static_cast<int>(msg.payload[0]);
                            server.update_progress(pct);
                        } else if (msg.cmd == 0x03) {
                            if (msg.payload.size() >= sizeof(memory_usage) + 1) {
                                std::memcpy(&memory_usage, msg.payload.data(), sizeof(memory_usage));
                                uint8_t result_byte = msg.payload[sizeof(memory_usage)];
                                server.set_memory_usage(memory_usage);
                                server.set_last_file_result(static_cast<int>(result_byte));
                                server.set_training_done();
                                job.done.set_value();
                                break;
                            }
                        }
                    }
                } catch (const std::exception& err) {
                    job.done.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobAdmin::SERIALIZE: {
                try {
                    sock.send(Message{0x08, {}});
                    Message ack = sock.recv();
                    if (ack.cmd == 0x09) {
                        job.done.set_value();
                    } else {
                        Logger::Error("WorkerAdmin::runChild unexpected response {:02X}", static_cast<unsigned>(ack.cmd));
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
