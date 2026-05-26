#include "worker_master.hpp"

WorkerMaster::WorkerMaster() : running_(true) {
    store_ = std::make_unique<DocumentStore>("data.db");
    createSocketpairs();
    spawnWorkers();
}

WorkerMaster::~WorkerMaster() { stop(); }

void WorkerMaster::createSocketpairs() {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, admin_sv_) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, client_sv_) != 0) {
        throw std::runtime_error("socketpair failed");
    }
    admin_sock_ = std::make_unique<IpcSocket>(admin_sv_[0]);
    client_sock_ = std::make_unique<IpcSocket>(client_sv_[0]);
}

void WorkerMaster::spawnWorkers() {
    size_t mem_usage = store_->get_memory_usage();
    admin_worker_ = std::make_unique<WorkerAdmin>(admin_sv_[1], admin_sv_[0], client_sv_[0], mem_usage);
    client_worker_ = std::make_unique<WorkerClient>(client_sv_[1], admin_sv_[0], client_sv_[0]);
    admin_pid_ = admin_worker_->getPid();
    client_pid_ = client_worker_->getPid();
}

void WorkerMaster::run() {
    std::atomic<bool> running{true};
    std::thread admin_thread([this, &running]() {
        while (running.load()) {
            try {
                Message msg = admin_sock_->recv();
                switch(msg.cmd){
                    case 0x0C: {
                        Logger::Info("Master: received 0x0C, querying src_type table");
                        nlohmann::json json_result = store_->get_source_types();
                        std::string json_str = json_result.dump();
                        Logger::Info("Master: query returned {} bytes", json_str.size());
                        admin_sock_->send(Message{0x0D, {json_str.begin(), json_str.end()}});
                        Logger::Info("Master: sent 0x0D response");
                        break;
                    }
                    case 0x01: {
                        if (msg.payload.size() < 1) continue;
                        bool serialize_flag = (msg.payload[0] != 0);
                        auto delim_it = std::find(msg.payload.begin() + 1, msg.payload.end(), 0);
                        if (delim_it == msg.payload.end()) {
                            Logger::Error("WorkerMaster::run admin thread: invalid payload (missing delimiter)");
                            break;
                        }
                        size_t delim = delim_it - msg.payload.begin();
                        std::string tags(msg.payload.begin() + 1, msg.payload.begin() + delim);
                        std::string data(msg.payload.begin() + delim + 1, msg.payload.end());
                        auto progress_cb = [this](int pct) {
                            admin_sock_->send(Message{0x02, {static_cast<uint8_t>(pct)}});
                        };
                        store_->add_document(data, tags, progress_cb, serialize_flag);
                        size_t mem_usage = store_->get_memory_usage();
                        UploadResult res = store_->get_last_upload_result();
                        std::vector<uint8_t> done_payload(sizeof(mem_usage) + 1);
                        std::memcpy(done_payload.data(), &mem_usage, sizeof(mem_usage));
                        done_payload[sizeof(mem_usage)] = static_cast<uint8_t>(res);
                        admin_sock_->send(Message{0x03, done_payload});
                        break;
                    }
                    case 0x0A: {
                        auto it = msg.payload.begin();
                        // Skip two zero bytes (old sources_type)
                        it++; it++;
                        std::string name, uml_schema;
                        while (it != msg.payload.end() && *it != 0) name.push_back(*it++);
                        if (it != msg.payload.end()) ++it;
                        while (it != msg.payload.end() && *it != 0) uml_schema.push_back(*it++);
                        if (it != msg.payload.end()) ++it;
                        // Read number of source pairs
                        if (it + 2 > msg.payload.end()) break;
                        uint16_t count;
                        std::memcpy(&count, &*it, 2);
                        count = ntohs(count);
                        it += 2;
                        std::vector<std::pair<uint8_t, std::string>> sources;
                        for (uint16_t i = 0; i < count; ++i) {
                            if (it == msg.payload.end()) break;
                            uint8_t src_type = *it++;
                            if (it + 4 > msg.payload.end()) break;
                            uint32_t size;
                            std::memcpy(&size, &*it, 4);
                            size = ntohl(size);
                            it += 4;
                            if (it + size > msg.payload.end()) break;
                            std::string content(it, it + size);
                            it += size;
                            sources.emplace_back(src_type, content);
                        }
                        store_->create_uml_container(name, uml_schema, sources);
                        admin_sock_->send(Message{0x0B, {}});
                        break;
                    }
                    case 0x08: {
                        store_->serialize();
                        admin_sock_->send(Message{0x09, {}});
                        break;
                    }
                }
            } catch (const std::exception& err) {
                if (std::string(err.what()).find("read failed") != std::string::npos) {
                    Logger::Info("Admin thread: child process terminated, shutting down.");
                    kill(admin_pid_, SIGTERM);
                    kill(client_pid_, SIGTERM);
                    std::exit(0);
                }
                else {
                    Logger::Error("WorkerMaster::run admin thread error: {}", err.what());
                    running.store(false);
                    break;
                }
            }
        }
    });
    std::thread client_thread([this, &running]() {
        while (running.load()) {
            try {
                Message msg = client_sock_->recv();
                if (msg.cmd == 0x04) {
                    if (msg.payload.size() < sizeof(double)) continue;
                    double threshold;
                    std::memcpy(&threshold, msg.payload.data(), sizeof(double));
                    std::string prompt(msg.payload.begin() + sizeof(double), msg.payload.end());
                    std::string answer = store_->get_answer(prompt, threshold);
                    client_sock_->send(Message{0x05, {answer.begin(), answer.end()}});
                }
                else if (msg.cmd == 0x06) {
                    if (msg.payload.size() < 5) continue;
                    int chunk_id = 0;
                    std::memcpy(&chunk_id, msg.payload.data(), 4);
                    chunk_id = ntohl(chunk_id);
                    std::string tag(msg.payload.begin() + 4, msg.payload.end());
                    std::replace(tag.begin(), tag.end(), ' ', '_');
                    store_->add_tag(tag, chunk_id);
                    client_sock_->send(Message{0x07, {}});
                }
            } catch (const std::exception& err) {
                Logger::Error("Client thread error: {}", err.what());
                running.store(false);
                break;
            }
        }
    });
    admin_thread.join();
    client_thread.join();
}

IpcSocket& WorkerMaster::getAdminSocket() const { return *admin_sock_; }
IpcSocket& WorkerMaster::getClientSocket() const { return *client_sock_; }
DocumentStore& WorkerMaster::getStore() { return *store_; }
void WorkerMaster::stop() { running_ = false; }
