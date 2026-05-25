#pragma once

#include <algorithm>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <functional>

#include <asio.hpp>

#include "logger.hpp"

struct JobAdmin {
    enum Type { LEARN, SERIALIZE, SHUTDOWN } type;
    std::vector<uint8_t> data;
    std::promise<void> done;
};

class HttpAdminSrv {
public:
    HttpAdminSrv(asio::io_context& io, unsigned short port);
    ~HttpAdminSrv();
    void start();
    void stop();
    std::future<void> enqueue_learn(const std::vector<uint8_t>& data);
    bool dequeue_learn(std::vector<uint8_t>& data, std::promise<void>& promise);
    int get_progress() const;
    bool is_training() const;
    void update_progress(int pct);
    void set_training_done();
    void set_training_started();
    void set_memory_usage(size_t value);
    size_t get_memory_usage() const;
    std::future<void> enqueue_serialize();
    bool dequeue_serialize(std::promise<void>& promise);
    bool dequeue(JobAdmin& job);
    void set_last_file_result(int result);
    int get_and_clear_last_file_result();
    std::future<void> enqueue_shutdown();

private:
    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::queue<JobAdmin> jobs_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    int current_progress_ = 0;
    bool is_training_ = false;
    mutable std::mutex progress_mtx_;
    size_t memory_usage_ = 0;
    mutable std::mutex mem_mtx_;
    int last_file_result_ = 0;
    std::mutex result_mtx_;

    void do_accept();
    void handle_request(std::shared_ptr<asio::ip::tcp::socket> sock);
};
