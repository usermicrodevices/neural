#pragma once

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "logger.hpp"

struct JobPrompt {
    std::string prompt;
    double threshold;
    std::promise<std::string> answer;
};

struct JobUMLSearch {
    std::string query;
    double threshold;
    std::promise<std::string> answer;
};

struct JobListUML {
    std::promise<std::string> answer;
};

struct JobCompose {
    std::vector<std::string> block_names;
    std::promise<std::string> answer;
};

class HttpClientSrv {
public:
    HttpClientSrv(asio::io_context& io, unsigned short port);
    ~HttpClientSrv();
    void start();
    void stop();
    std::future<std::string> enqueue_ask(const std::string& prompt, double threshold = -1.0);
    bool dequeue_ask(std::string& prompt, double& threshold, std::promise<std::string>& promise);
    void enqueue_uml_search(const std::string& query, double threshold, std::promise<std::string>&& promise);
    bool dequeue_uml_search(std::string& query, double& threshold, std::promise<std::string>& promise);
    bool try_dequeue_uml_search(std::string& query, double& threshold, std::promise<std::string>& promise);
    void enqueue_list_uml(std::promise<std::string>&& promise);
    bool dequeue_list_uml(std::promise<std::string>& promise);
    void enqueue_compose(const std::vector<std::string>& block_names, std::promise<std::string>&& promise);
    bool dequeue_compose(std::vector<std::string>& block_names, std::promise<std::string>& promise);
    enum class DequeueResult { NONE, PROMPT, UML_SEARCH, LIST_UML, COMPOSE };
    DequeueResult dequeue_any(std::string& prompt, double& threshold, std::promise<std::string>& promise,
                              std::string& uml_query, double& uml_threshold, std::promise<std::string>& uml_promise,
                              std::promise<std::string>& list_uml_promise,
                              std::vector<std::string>& compose_block_names, std::promise<std::string>& compose_promise);

private:
    void do_accept();
    void handle_request(std::shared_ptr<asio::ip::tcp::socket> sock);
    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::queue<JobPrompt> jobs_;
    std::queue<JobUMLSearch> uml_search_jobs_;
    std::queue<JobListUML> list_uml_jobs_;
    std::queue<JobCompose> compose_jobs_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};
