#include "html.hpp"
#include "admin.hpp"

HttpAdminSrv::HttpAdminSrv(asio::io_context& io, unsigned short port)
: io_(io), acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
current_progress_(0), is_training_(false) {}

HttpAdminSrv::~HttpAdminSrv() { stop(); }

void HttpAdminSrv::start() {
    thread_ = std::thread([this]() { do_accept(); io_.run(); });
}

void HttpAdminSrv::stop() {
    stop_ = true; cv_.notify_all(); acceptor_.close();
    if (thread_.joinable()) thread_.join();
}

void HttpAdminSrv::set_last_file_result(int result) {
    std::lock_guard<std::mutex> lock(result_mtx_);
    last_file_result_ = result;
}

int HttpAdminSrv::get_and_clear_last_file_result() {
    std::lock_guard<std::mutex> lock(result_mtx_);
    int res = last_file_result_;
    last_file_result_ = 0;
    return res;
}

std::future<void> HttpAdminSrv::enqueue_learn(const std::vector<uint8_t>& data) {
    std::promise<void> p; auto f = p.get_future();
    JobAdmin job;
    job.type = JobAdmin::LEARN;
    job.data = data;
    job.done = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

std::future<void> HttpAdminSrv::enqueue_serialize() {
    std::promise<void> p; auto f = p.get_future();
    JobAdmin job;
    job.type = JobAdmin::SERIALIZE;
    job.done = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

bool HttpAdminSrv::dequeue(JobAdmin& job) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !jobs_.empty() || stop_; });
    if (stop_ && jobs_.empty()) return false;
    job = std::move(jobs_.front());
    jobs_.pop();
    return true;
}

void HttpAdminSrv::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec)
            std::thread(&HttpAdminSrv::handle_request, this,
                        std::make_shared<asio::ip::tcp::socket>(std::move(socket))).detach();
        if (!stop_) do_accept();
    });
}

void HttpAdminSrv::handle_request(std::shared_ptr<asio::ip::tcp::socket> s) {
    try {
        asio::error_code ec;
        std::string headers = read_headers(*s, ec);
        if (ec) { Logger::Error("HttpAdminSrv::handle_request read headers: {}", ec.message()); return; }
        std::string method, path, version, content_type;
        int content_length = -1;
        parse_request(headers, method, path, version, content_type, content_length);
        auto respond_html = [&](const std::string& body) {
            std::string resp = build_response("text/html", body);
            asio::write(*s, asio::buffer(resp), ec);
        };
        if (method == "GET" && path == "/favicon.ico") {
            std::string svg = read_file_into_string("static/admin/favicon.svg");
            std::string resp = build_response("image/svg+xml", svg);
            asio::write(*s, asio::buffer(resp), ec);
            return;
        }
        if (method == "GET" && path == "/progress") {
            std::lock_guard<std::mutex> lock(progress_mtx_);
            std::string json = "{\"progress\":" + std::to_string(current_progress_) +
            ",\"training\":" + (is_training_ ? "true" : "false");
            if (!is_training_) {
                std::lock_guard<std::mutex> mem_lock(mem_mtx_);
                json += ",\"memory\":" + std::to_string(memory_usage_);
            }
            int fres = get_and_clear_last_file_result();
            if (fres != 0) {
                json += ",\"fileResult\":" + std::to_string(fres);
            }
            json += "}";
            std::string resp = build_response("application/json", json);
            asio::write(*s, asio::buffer(resp), ec);
            return;
        }
        if (method == "POST" && path == "/") {
            if (content_length <= 0) {
            Logger::Warn("HttpAdminSrv::handle_request: POST without Content-Length");
            respond_html("<html><body><h2>Error: Content-Length required</h2></body></html>");
            return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) {
                Logger::Error("HttpAdminSrv::handle_request body read: {}", ec.message());
                return;
            }
            std::string data;
            std::string boundary;
            std::string bbody(body.begin(), body.end());
            if (content_type.find("multipart/form-data") != std::string::npos) {
                auto pos = content_type.find("boundary=");
                if (pos != std::string::npos) boundary = "--" + content_type.substr(pos + 9);
                size_t start = bbody.find("\r\n\r\n");
                if (start != std::string::npos) {
                    start += 4;
                    size_t end = bbody.find(boundary, start);
                    if (end != std::string::npos) {
                        data = bbody.substr(start, end - start);
                        while (!data.empty() && (data.back() == '\r' || data.back() == '\n')) data.pop_back();
                    }
                }
            } else {
                data.assign(body.begin(), body.end());
            }
            if (data.empty()) {
                respond_html("<html><body><h2>Error: No document content received</h2></body></html>");
                return;
            }
            Logger::Trace("HttpAdminSrv::handle_request: document received bytes {};", data.size());
            std::string tags;
            size_t pos = 0;
            while ((pos = bbody.find("name=\"tags\"", pos)) != std::string::npos) {
                size_t start = bbody.find("\r\n\r\n", pos);
                if (start != std::string::npos) {
                    start += 4;
                    size_t end = bbody.find(boundary, start);
                    if (end != std::string::npos) {
                        tags = bbody.substr(start, end - start);
                        while (!tags.empty() && (tags.back() == '\r' || tags.back() == '\n'))
                            tags.pop_back();
                        break;
                    }
                }
                pos = start;
            }
            Logger::Trace("HttpAdminSrv::handle_request: tags received {};", tags);
            bool serialize_flag = true;
            std::string serialize_value;
            pos = 0;
            while ((pos = bbody.find("name=\"serialize\"", pos)) != std::string::npos) {
                size_t start = bbody.find("\r\n\r\n", pos);
                if (start != std::string::npos) {
                    start += 4;
                    size_t end = bbody.find(boundary, start);
                    if (end != std::string::npos) {
                        serialize_value = bbody.substr(start, end - start);
                        while (!serialize_value.empty() && (serialize_value.back() == '\r' || serialize_value.back() == '\n'))
                        serialize_value.pop_back();
                        serialize_flag = (serialize_value == "1");
                        break;
                    }
                }
                pos = start;
            }
            std::vector<uint8_t> payload;
            payload.push_back(serialize_flag ? 1 : 0);
            uint16_t tag_len = htons(tags.size());
            payload.insert(payload.end(), (uint8_t*)&tag_len, (uint8_t*)&tag_len + 2);
            payload.insert(payload.end(), tags.begin(), tags.end());
            payload.insert(payload.end(), data.begin(), data.end());
            auto future = enqueue_learn(payload);
            std::thread([this, future = std::move(future), s]() mutable {
                asio::error_code ec;
                try {
                    future.get();
                    std::string resp = R"({"status":"ok","message":"Learning Complete!"})";
                    std::string http_resp = build_response("application/json", resp);
                    asio::write(*s, asio::buffer(http_resp), ec);
                } catch (const std::exception& err) {
                    std::string resp = R"({"status":"error","message":")" + std::string(err.what()) + "\"}";
                    std::string http_resp = build_response("application/json", resp);
                    asio::write(*s, asio::buffer(http_resp), ec);
                }
            }).detach();
            std::string resp = R"({"status":"accepted","message":"Learning started"})";
            std::string http_resp = build_response("application/json", resp);
            asio::write(*s, asio::buffer(http_resp), ec);
            return;
        }
        else if (method == "POST" && path == "/serialize") {
            auto future = enqueue_serialize();
            std::thread([this, future = std::move(future), s]() mutable {
                asio::error_code ec;
                try {
                    future.get();
                    std::string resp = R"({"status":"ok","message":"Serialized"})";
                    asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
                } catch (const std::exception& err) {
                    std::string resp = R"({"status":"error","message":")" + std::string(err.what()) + "\"}";
                    asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
                }
            }).detach();
            return;
        }
        if (method == "GET") {
            if (path == "/") {
                if (serve_static_file(*s, "static/admin/admin.html", ec)) return;
                respond_html("<html><body><h2>Admin page not found</h2></body></html>");
                return;
            }
            if (path.rfind("/static/admin/", 0) == 0) {
                std::string rel_path = path.substr(1);
                if (serve_static_file(*s, rel_path, ec)) return;
            }
            std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            asio::write(*s, asio::buffer(not_found), ec);
            return;
        }
        std::string not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    asio::write(*s, asio::buffer(not_allowed), ec);
    } catch (const std::exception& err) {
        Logger::Error("HttpAdminSrv::handle_request: {}", err.what());
    }
}

int HttpAdminSrv::get_progress() const {return current_progress_;}

bool HttpAdminSrv::is_training() const {return is_training_;}

void HttpAdminSrv::set_training_started() {
    std::lock_guard<std::mutex> lock(progress_mtx_);
    is_training_ = true;
    current_progress_ = 0;
}

void HttpAdminSrv::update_progress(int pct) {
    std::lock_guard<std::mutex> lock(progress_mtx_);
    current_progress_ = pct;
    is_training_ = true;
}

void HttpAdminSrv::set_training_done() {
    std::lock_guard<std::mutex> lock(progress_mtx_);
    is_training_ = false;
    current_progress_ = 100;
}

void HttpAdminSrv::set_memory_usage(size_t value) {
    std::lock_guard<std::mutex> lock(mem_mtx_);
    Logger::Info("HttpAdminSrv::set_memory_usage: {}", value);
    memory_usage_ = value;
}
size_t HttpAdminSrv::get_memory_usage() const {
    std::lock_guard<std::mutex> lock(mem_mtx_);
    return memory_usage_;
}
