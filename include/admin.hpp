#pragma once

#include <algorithm>
#include <arpa/inet.h>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <variant>
#include <thread>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "logger.hpp"
#include "ipc.hpp"

struct JobAdmin {
    JobTypeAdmin type;
    std::vector<uint8_t> data;
    std::promise<std::string> response;
};

class HttpAdminSrv {
public:
    HttpAdminSrv(asio::io_context& io, unsigned short port);
    ~HttpAdminSrv();
    void start();
    void stop();
    std::future<std::string> enqueue_learn(const std::vector<uint8_t>& data);
    bool dequeue_learn(std::vector<uint8_t>& data, std::promise<void>& promise);
    std::future<std::string> enqueue_src_types();
    bool dequeue_src_types(JobAdmin& job);
    std::future<std::string> enqueue_train_uml(const std::vector<uint8_t>& payload);
    std::future<std::string> enqueue_list_uml();
    int current_progress() const;
    bool is_training() const;
    void update_progress(int pct);
    void set_training_done();
    void set_training_started();
    void set_memory_usage(size_t value);
    size_t memory_usage() const;
    std::future<std::string> enqueue_list_tables();
    std::future<std::string> enqueue_get_table(const std::string& table, const std::string& filter, int offset = 0, int limit = 100);
    std::future<std::string> enqueue_serialize();
    bool dequeue_serialize(std::promise<void>& promise);
    bool dequeue(JobAdmin& job);
    void set_last_file_result(int result);
    int get_and_clear_last_file_result();
    std::future<std::string> enqueue_shutdown();

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
    void send_response(std::shared_ptr<asio::ip::tcp::socket> s, const std::string& body);
    void send_json_response(std::shared_ptr<asio::ip::tcp::socket> s, const nlohmann::json& json_obj, int status_code=200);
    void get_favicon(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_config(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_progress(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_src_types(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_list_uml(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_train_uml(std::shared_ptr<asio::ip::tcp::socket> s);
    void get_or_post_show_db(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, asio::error_code& ec);
    void post_get_table(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, asio::error_code& ec);
    void post_root(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, const std::string& content_type);
    void post_train_uml(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, const std::string& content_type);
    void post_serialize(std::shared_ptr<asio::ip::tcp::socket> s);
    void post_shutdown(std::shared_ptr<asio::ip::tcp::socket> s);
    void handle_request(std::shared_ptr<asio::ip::tcp::socket> sock);
};
