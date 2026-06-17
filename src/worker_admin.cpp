#include "worker_admin.hpp"

void WorkerAdmin::runChild(int child_fd, size_t memory_usage) {
    IpcSocket sock(child_fd);
    asio::io_context io;
    try {
        HttpAdminSrv server(io, 8080);
        server.set_memory_usage(memory_usage);
        server.start();
        while (true) {
            JobAdmin job;
            if (!server.dequeue(job))
                break;
            switch(job.type){
            case JobTypeAdmin::SRC_TYPES: {
                sock.send(Message{JobTypeAdmin::SRC_TYPES, {}});
                Message msg = sock.recv();
                if (msg.cmd == JobTypeAdmin::SRC_TYPES) {
                    std::string json(msg.payload.begin(), msg.payload.end());
                    job.response.set_value(json);
                } else {
                    job.response.set_exception(
                        std::make_exception_ptr(
                            std::runtime_error(
                                "WorkerAdmin::runChild: unexpected response for src_types")));
                }
                break;
            }
            case JobTypeAdmin::TRAIN: {
                try {
                    if (job.data.size() >= 3) {
                        uint8_t serialize_flag = job.data[0];
                        uint16_t tag_len = ntohs(*(uint16_t*)&job.data[1]);
                        if (job.data.size() < static_cast<size_t>(3 + tag_len))
                            throw std::runtime_error("WorkerAdmin::runChild Invalid TRAIN payload: insufficient data");
                        std::string tags(job.data.begin()+3, job.data.begin()+3+tag_len);
                        std::string data(job.data.begin()+3+tag_len, job.data.end());
                        std::vector<uint8_t> master_payload;
                        master_payload.push_back(serialize_flag);
                        master_payload.insert(master_payload.end(), tags.begin(), tags.end());
                        master_payload.push_back(0); // delimiter
                        master_payload.insert(master_payload.end(), data.begin(), data.end());
                        sock.send(Message{JobTypeAdmin::TRAIN, master_payload});
                    } else
                        sock.send(Message{JobTypeAdmin::TRAIN, job.data});
                    server.set_training_started();
                    while (true) {
                        Message msg = sock.recv();
                        if (msg.cmd == JobTypeAdmin::TRAIN_PROGRESS) {
                            int pct = msg.payload.empty() ? 0 : static_cast<int>(msg.payload[0]);
                            server.update_progress(pct);
                        } else if (msg.cmd == JobTypeAdmin::TRAIN_DONE) {
                            if (msg.payload.size() >= sizeof(memory_usage) + 1) {
                                std::memcpy(&memory_usage, msg.payload.data(), sizeof(memory_usage));
                                uint8_t result_byte = msg.payload[sizeof(memory_usage)];
                                server.set_memory_usage(memory_usage);
                                server.set_last_file_result(static_cast<int>(result_byte));
                                server.set_training_done();
                                job.response.set_value("");
                                break;
                            }
                        }
                    }
                } catch (const std::exception& err) {
                    job.response.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobTypeAdmin::TRAIN_PROGRESS: break;
            case JobTypeAdmin::TRAIN_DONE: break;
            case JobTypeAdmin::TRAIN_UML: {
                try {
                    server.set_training_started();
                    sock.send(Message{JobTypeAdmin::TRAIN_UML, job.data});
                    Message ack = sock.recv();
                    if (ack.cmd == JobTypeAdmin::TRAIN_UML_DONE) {
                        uint8_t result = ack.payload.empty() ? 0 : ack.payload[0];
                        server.set_training_done();
                        if (result == 1) {
                            job.response.set_value("");
                        } else if (result == 2) {
                            job.response.set_value("duplicate");
                        } else {
                            job.response.set_exception(
                                std::make_exception_ptr(
                                    std::runtime_error("TRAIN_UML failed: database insert error")));
                        }
                    } else {
                        server.set_training_done();
                        throw std::runtime_error("WorkerAdmin::runChild unexpected response for TRAIN_UML");
                    }
                } catch (const std::exception& err) {
                    server.set_training_done();
                    job.response.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobTypeAdmin::TRAIN_UML_DONE: break;
            case JobTypeAdmin::LIST_UML: {
                try {
                    sock.send(Message{JobTypeAdmin::LIST_UML, job.data});
                    Message ack = sock.recv();
                    if (ack.cmd == JobTypeAdmin::LIST_UML_DONE) {
                        job.response.set_value(std::string(ack.payload.begin(), ack.payload.end()));
                    } else {
                        throw std::runtime_error("WorkerAdmin::runChild unexpected response for LIST_UML");
                    }
                } catch (const std::exception& err) {
                    job.response.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobTypeAdmin::LIST_UML_DONE: break;
            case JobTypeAdmin::COMPOSE: {
                try {
                    sock.send(Message{JobTypeAdmin::COMPOSE, job.data});
                    Message ack = sock.recv();
                    if (ack.cmd == JobTypeAdmin::COMPOSE_DONE) {
                        job.response.set_value(std::string(ack.payload.begin(), ack.payload.end()));
                    } else {
                        throw std::runtime_error("WorkerAdmin::runChild unexpected response for COMPOSE");
                    }
                } catch (const std::exception& err) {
                    job.response.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobTypeAdmin::COMPOSE_DONE: break;
            case JobTypeAdmin::GET_TABLE: {
                auto it = job.data.begin();
                std::string table_name, filter_text;
                while (it != job.data.end() && *it != 0) table_name.push_back(*it++);
                if (it != job.data.end()) ++it;
                while (it != job.data.end() && *it != 0) filter_text.push_back(*it++);
                if (it != job.data.end()) ++it;
                int offset = 0, limit = 100;
                if (it + 8 <= job.data.end()) {
                    uint32_t net_offset, net_limit;
                    std::memcpy(&net_offset, &*it, 4);
                    std::memcpy(&net_limit, &*it + 4, 4);
                    offset = ntohl(net_offset);
                    limit = ntohl(net_limit);
                    it += 8;
                }
                std::vector<uint8_t> payload;
                payload.insert(payload.end(), table_name.begin(), table_name.end());
                payload.push_back(0);
                payload.insert(payload.end(), filter_text.begin(), filter_text.end());
                payload.push_back(0);
                uint32_t net_offset = htonl(offset);
                uint32_t net_limit = htonl(limit);
                payload.insert(payload.end(), (uint8_t*)&net_offset, (uint8_t*)&net_offset + 4);
                payload.insert(payload.end(), (uint8_t*)&net_limit, (uint8_t*)&net_limit + 4);
                sock.send(Message{JobTypeAdmin::GET_TABLE, payload});
                Message resp = sock.recv();
                if (resp.cmd == JobTypeAdmin::GET_TABLE) {
                    std::string json(resp.payload.begin(), resp.payload.end());
                    job.response.set_value(json);
                } else {
                    job.response.set_exception(std::make_exception_ptr(std::runtime_error("Unexpected response for GET_TABLE")));
                }
                break;
            }
            case JobTypeAdmin::LIST_TABLES: {
                sock.send(Message{JobTypeAdmin::LIST_TABLES, {}});
                Message resp = sock.recv();
                if (resp.cmd == JobTypeAdmin::LIST_TABLES) {
                    std::string json(resp.payload.begin(), resp.payload.end());
                    job.response.set_value(json);
                } else {
                    job.response.set_exception(std::make_exception_ptr(std::runtime_error("WorkerAdmin::runChild Unexpected response for LIST_TABLES")));
                }
                break;
            }
            case JobTypeAdmin::SERIALIZE: {
                try {
                    sock.send(Message{JobTypeAdmin::SERIALIZE, {}});
                    Message ack = sock.recv();
                    if (ack.cmd == JobTypeAdmin::SERIALIZE) {
                        job.response.set_value("");
                    } else {
                        //Logger::Error("WorkerAdmin::runChild unexpected response {:02X}", static_cast<unsigned>(ack.cmd));
                        throw std::runtime_error("WorkerAdmin::runChild unexpected response");
                    }
                } catch (const std::exception& err) {
                    //Logger::Error("WorkerAdmin::runChild serialize error: {}", err.what());
                    job.response.set_exception(std::make_exception_ptr(err));
                }
                break;
            }
            case JobTypeAdmin::SHUTDOWN: {
                job.response.set_value("");
                server.stop();
                return;
            }
            default: break;
        }
    }
    server.stop();
    } catch (const std::exception& err) {
        Logger::Error("Admin worker failed to start: {}", err.what());
        exit(1);
    }
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
