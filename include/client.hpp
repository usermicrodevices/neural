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

using json = nlohmann::json;
struct JobPrompt {
    std::string prompt;
    double threshold;
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

private:
    void do_accept();
    void handle_request(std::shared_ptr<asio::ip::tcp::socket> sock);
    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::queue<JobPrompt> jobs_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};
