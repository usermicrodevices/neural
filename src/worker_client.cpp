#include "worker_client.hpp"

void WorkerClient::runChild(int child_fd) {
    IpcSocket sock(child_fd);
    asio::io_context io;
    try {
        HttpClientSrv server(io, 8081);
        server.start();
        bool stop = false;
        while (!stop) {
            std::string prompt;
            double threshold;
            std::promise<std::string> answer_promise;
            std::string uml_query;
            double uml_threshold;
            std::promise<std::string> uml_promise;
            std::promise<std::string> list_uml_promise;
            std::vector<std::string> compose_block_names;
            std::promise<std::string> compose_promise;

            auto result = server.dequeue_any(prompt, threshold, answer_promise, uml_query, uml_threshold, uml_promise,
                                             list_uml_promise, compose_block_names, compose_promise);
            if (result == HttpClientSrv::DequeueResult::NONE) {
                break;
            } else if (result == HttpClientSrv::DequeueResult::LIST_UML) {
                try {
                    sock.send(Message{0x0C, {}});
                    Message ans = sock.recv();
                    std::string result_str(ans.payload.begin(), ans.payload.end());
                    list_uml_promise.set_value(result_str);
                } catch (const std::exception& e) {
                    list_uml_promise.set_exception(std::make_exception_ptr(e));
                    stop = true;
                    break;
                }
            } else if (result == HttpClientSrv::DequeueResult::COMPOSE) {
                try {
                    std::vector<uint8_t> payload;
                    for (const auto& name : compose_block_names) {
                        payload.insert(payload.end(), name.begin(), name.end());
                        payload.push_back(0);
                    }
                    sock.send(Message{0x0E, payload});
                    Message ans = sock.recv();
                    std::string result_str(ans.payload.begin(), ans.payload.end());
                    compose_promise.set_value(result_str);
                } catch (const std::exception& e) {
                    compose_promise.set_exception(std::make_exception_ptr(e));
                    stop = true;
                    break;
                }
            } else if (result == HttpClientSrv::DequeueResult::UML_SEARCH) {
                try {
                    std::vector<uint8_t> payload(sizeof(double));
                    std::memcpy(payload.data(), &uml_threshold, sizeof(double));
                    payload.insert(payload.end(), uml_query.begin(), uml_query.end());
                    sock.send(Message{0x0A, payload});
                    Message ans = sock.recv();
                    std::string result_str(ans.payload.begin(), ans.payload.end());
                    uml_promise.set_value(result_str);
                } catch (const std::exception& e) {
                    uml_promise.set_exception(std::make_exception_ptr(e));
                    stop = true;
                    break;
                }
            } else if (result == HttpClientSrv::DequeueResult::PROMPT) {
                try {
                    std::vector<uint8_t> payload(sizeof(double));
                    double thr = (threshold > 0) ? threshold : -1.0;
                    std::memcpy(payload.data(), &thr, sizeof(double));
                    payload.insert(payload.end(), prompt.begin(), prompt.end());
                    sock.send(Message{0x04, payload});
                    Message ans = sock.recv();
                    std::string answer(ans.payload.begin(), ans.payload.end());
                    answer_promise.set_value(answer);
                } catch (const std::exception& e) {
                    answer_promise.set_exception(std::make_exception_ptr(e));
                    stop = true;
                    break;
                }
            }
        }
        server.stop();
    } catch (const std::exception& err) {
        Logger::Error("Client worker failed to start: {}", err.what());
        exit(1);
    }
}

WorkerClient::WorkerClient(int child_fd, int admin_fd, int client_fd) {
    pid_ = fork();
    if (pid_ == 0) {
        close(admin_fd);
        close(client_fd);
        runChild(child_fd);
        exit(0);
    } else if (pid_ > 0) {
        close(child_fd);
    } else {
        throw std::runtime_error("fork failed");
    }
}

WorkerClient::~WorkerClient() { if (pid_ > 0) kill(pid_, SIGTERM); }

pid_t WorkerClient::getPid() const { return pid_; }
