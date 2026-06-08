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
                    case JobTypeAdmin::SRC_TYPES: {
                        Logger::Info("Master: received {:02X}, querying src_type table", static_cast<unsigned>(JobTypeAdmin::SRC_TYPES));
                        nlohmann::json json_result = store_->get_source_types();
                        std::string json_str = json_result.dump();
                        Logger::Info("Master: query returned {} bytes", json_str.size());
                        admin_sock_->send(Message{JobTypeAdmin::SRC_TYPES, {json_str.begin(), json_str.end()}});
                        Logger::Info("Master: sent {:02X} response", static_cast<unsigned>(JobTypeAdmin::SRC_TYPES));
                        break;
                    }
                    case JobTypeAdmin::TRAIN: {
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
                            admin_sock_->send(Message{TRAIN_PROGRESS, {static_cast<uint8_t>(pct)}});
                        };
                        store_->add_document(data, tags, progress_cb, serialize_flag);
                        size_t mem_usage = store_->get_memory_usage();
                        UploadResult res = store_->get_last_upload_result();
                        std::vector<uint8_t> done_payload(sizeof(mem_usage) + 1);
                        std::memcpy(done_payload.data(), &mem_usage, sizeof(mem_usage));
                        done_payload[sizeof(mem_usage)] = static_cast<uint8_t>(res);
                        admin_sock_->send(Message{JobTypeAdmin::TRAIN_DONE, done_payload});
                        break;
                    }
                    case JobTypeAdmin::TRAIN_UML: {
                        auto it = msg.payload.begin();
                        auto end = msg.payload.end();
                        if (it + 8 > end) {
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        uint32_t net_name_len;
                        std::memcpy(&net_name_len, &*it, 4);
                        uint32_t name_len = ntohl(net_name_len);
                        it += 4;
                        if (it + name_len > end) {
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        std::string name(it, it + name_len);
                        it += name_len;
                        if (it + 4 > end) {
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        uint32_t net_schema_len;
                        std::memcpy(&net_schema_len, &*it, 4);
                        uint32_t schema_len = ntohl(net_schema_len);
                        it += 4;
                        if (it + schema_len > end) {
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        std::string uml_schema(it, it + schema_len);
                        it += schema_len;
                        if (it + 2 > end) {
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        uint16_t count;
                        std::memcpy(&count, &*it, 2);
                        count = ntohs(count);
                        it += 2;
                        std::vector<std::pair<uint8_t, std::string>> sources;
                        bool parse_ok = true;
                        for (uint16_t i = 0; i < count; ++i) {
                            if (it == end) { parse_ok = false; break; }
                            uint8_t src_type = *it++;
                            if (src_type == 0) src_type = 1;
                            if (it + 4 > end) { parse_ok = false; break; }
                            uint32_t size;
                            std::memcpy(&size, &*it, 4);
                            size = ntohl(size);
                            it += 4;
                            if (it + size > end) { parse_ok = false; break; }
                            std::string content(it, it + size);
                            it += size;
                            sources.emplace_back(src_type, content);
                        }
                        if (!parse_ok || sources.size() != count) {
                            Logger::Error("WorkerMaster::run: TRAIN_UML parse failed (expected {} sources, got {})", count, sources.size());
                            admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {0}});
                            break;
                        }
                        bool ok = store_->create_uml_container(name, uml_schema, sources);
                        admin_sock_->send(Message{JobTypeAdmin::TRAIN_UML_DONE, {static_cast<uint8_t>(ok ? 1 : 0)}});
                        break;
                    }
                    case JobTypeAdmin::GET_TABLE: {
                        auto it = msg.payload.begin();
                        std::string table, filter;
                        while (it != msg.payload.end() && *it != 0) table.push_back(*it++);
                        if (it != msg.payload.end()) ++it;
                        while (it != msg.payload.end() && *it != 0) filter.push_back(*it++);
                        if (it != msg.payload.end()) ++it;
                        int offset = 0, limit = 100;
                        if (it + 8 <= msg.payload.end()) {
                            uint32_t net_offset, net_limit;
                            std::memcpy(&net_offset, &*it, 4);
                            std::memcpy(&net_limit, &*it + 4, 4);
                            offset = ntohl(net_offset);
                            limit = ntohl(net_limit);
                        }
                        try {
                            Logger::Trace("WorkerMaster::run: get_table_data {}", table);
                            nlohmann::json result = store_->get_table_data(table, filter, offset, limit);
                            std::string json_str = result.dump();
                            Logger::Trace("WorkerMaster::run: send response for table  {}", table);
                            admin_sock_->send(Message{JobTypeAdmin::GET_TABLE, {json_str.begin(), json_str.end()}});
                        } catch (const std::exception& err) {
                            nlohmann::json errjson = {{"error", err.what()}};
                            std::string err_str = errjson.dump();
                            admin_sock_->send(Message{JobTypeAdmin::GET_TABLE, {err_str.begin(), err_str.end()}});
                        }
                        break;
                    }
                    case JobTypeAdmin::LIST_TABLES: {
                        nlohmann::json result = store_->list_tables();
                        std::string json_str = result.dump();
                        admin_sock_->send(Message{JobTypeAdmin::LIST_TABLES, {json_str.begin(), json_str.end()}});
                        break;
                    }
                    case JobTypeAdmin::SERIALIZE: {
                        store_->serialize();
                        admin_sock_->send(Message{JobTypeAdmin::SERIALIZE, {}});
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
