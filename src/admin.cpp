#include "http.hpp"
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

bool HttpAdminSrv::dequeue(JobAdmin& job) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !jobs_.empty() || stop_; });
    if (stop_ && jobs_.empty()) return false;
    job = std::move(jobs_.front());
    jobs_.pop();
    return true;
}

std::future<std::string> HttpAdminSrv::enqueue_learn(const std::vector<uint8_t>& data) {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::TRAIN;
    job.data = data;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

std::future<std::string> HttpAdminSrv::enqueue_src_types() {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::SRC_TYPES;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

bool HttpAdminSrv::dequeue_src_types(JobAdmin& job) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !jobs_.empty() || stop_; });
    if (stop_ && jobs_.empty()) return false;
    job = std::move(jobs_.front());
    jobs_.pop();
    return true;
}

std::future<std::string> HttpAdminSrv::enqueue_train_uml(const std::vector<uint8_t>& payload) {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::TRAIN_UML;
    job.data = payload;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

std::future<std::string> HttpAdminSrv::enqueue_list_tables() {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::LIST_TABLES;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

std::future<std::string> HttpAdminSrv::enqueue_get_table(const std::string& table, const std::string& filter, int offset, int limit) {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::GET_TABLE;
    std::vector<uint8_t> data;
    data.insert(data.end(), table.begin(), table.end());
    data.push_back(0);
    data.insert(data.end(), filter.begin(), filter.end());
    data.push_back(0);
    uint32_t net_offset = htonl(offset);
    uint32_t net_limit = htonl(limit);
    data.insert(data.end(), (uint8_t*)&net_offset, (uint8_t*)&net_offset + 4);
    data.insert(data.end(), (uint8_t*)&net_limit, (uint8_t*)&net_limit + 4);
    job.data = std::move(data);
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

std::future<std::string> HttpAdminSrv::enqueue_serialize() {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::SERIALIZE;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

void HttpAdminSrv::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (ec)
        {
            if (ec == asio::error::operation_aborted)
                Logger::Debug("HttpAdminSrv::do_accept asio::async_accept: {}; {}; {}", ec.value(), ec.message(), ec.category().name());
            else
                Logger::Error("HttpAdminSrv::do_accept asio::async_accept: {}", ec.message());
        }
        else
            std::thread(&HttpAdminSrv::handle_request, this,
                        std::make_shared<asio::ip::tcp::socket>(std::move(socket))).detach();
        if (!stop_) do_accept();
    });
}

std::future<std::string> HttpAdminSrv::enqueue_shutdown() {
    std::promise<std::string> p;
    auto f = p.get_future();
    JobAdmin job;
    job.type = JobTypeAdmin::SHUTDOWN;
    job.response = std::move(p);
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push(std::move(job)); }
    cv_.notify_one();
    return f;
}

void HttpAdminSrv::send_response(std::shared_ptr<asio::ip::tcp::socket> s, const std::string& body) {
    std::string resp = build_response("text/html", body);
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if (ec) Logger::Error("HttpAdminSrv::send_response asio::write: {}", ec.message());
}

void HttpAdminSrv::get_favicon(std::shared_ptr<asio::ip::tcp::socket> s) {
    std::string svg = read_file_into_string("static/admin/favicon.svg");
    std::string resp = build_response("image/svg+xml", svg);
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::get_favicon asio::write: {}", ec.message());
}

void HttpAdminSrv::get_config(std::shared_ptr<asio::ip::tcp::socket> s) {
    std::string page = R"(
    <!DOCTYPE html>
    <html>
    <head><meta charset="UTF-8"><title>Configuration</title><style>body{font-family:sans-serif;max-width:800px;margin:2rem auto;padding:1rem;}</style></head>
    <body>
    <h1>⚙️ Service Configuration</h1>
    <p>Settings page – coming soon.</p>
    <p><a href="/">Back to Admin</a></p>
    </body>
    </html>
    )";
    std::string resp = build_response("text/html", page);
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::get_config asio::write: {}", ec.message());
}

void HttpAdminSrv::get_progress(std::shared_ptr<asio::ip::tcp::socket> s) {
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
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::get_progress asio::write: {}", ec.message());
}

void HttpAdminSrv::get_src_types(std::shared_ptr<asio::ip::tcp::socket> s) {
    //Logger::Trace("HttpAdminSrv::get_src_types: entering, enqueueing request");
    auto future = enqueue_src_types();
    //Logger::Trace("get_src_types: waiting for future");
    std::string json;
    try {
        json = future.get();
        Logger::Info("get_src_types: future completed, json size={}", json.size());
    } catch (const std::exception& err) {
        Logger::Warn("HttpAdminSrv::get_src_types {}", err.what());
        json = "[{\"id\":1,\"name\":\"C++\"},{\"id\":2,\"name\":\"Python\"}]";
    }
    std::string resp = build_response("application/json", json);
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::get_src_types asio::write: {}", ec.message());
}

void HttpAdminSrv::get_train_uml(std::shared_ptr<asio::ip::tcp::socket> s) {
    std::ifstream file("static/admin/admin_uml.html");
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string resp = build_response("text/html", content);
        asio::error_code ec;
        asio::write(*s, asio::buffer(resp), ec);
        if(ec) Logger::Error("HttpAdminSrv::get_train_uml write error: {}", ec.message());
    } else {
        send_response(s, "<html><body><h2>Page not found</h2></body></html>");
    }
}

void HttpAdminSrv::send_json_response(std::shared_ptr<asio::ip::tcp::socket> s, const nlohmann::json& json_obj, int status_code) {
    std::string resp = build_response("application/json", json_obj.dump(), status_code);
    asio::error_code ec;
    asio::write(*s, asio::buffer(resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::send_json_response: {}", ec.message());
}

void HttpAdminSrv::post_get_table(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, asio::error_code& ec) {
    Logger::Trace("HttpAdminSrv::post_get_table content_length={}", content_length);
    std::vector<char> body;
    read_body(*s, content_length, body, ec);
    if (ec) {
        Logger::Error("HttpAdminSrv::post_get_table body read: {}", ec.message());
        send_json_response(s, {{"error", "Invalid JSON"}}, 400);
        return;
    }
    std::string body_str(body.begin(), body.end());
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body_str);
    } catch (...) {
        send_json_response(s, {{"error", "Invalid JSON"}}, 400);
        return;
    }
    Logger::Trace("HttpAdminSrv::post_get_table body={}", req.dump());
    std::string table = req.value("table", "");
    std::string filter = req.value("filter", "");
    int offset = req.value("offset", 0);
    int limit = req.value("limit", 100);
    if (limit > 500) limit = 500;
    if (table.empty()) {
        auto future = enqueue_list_tables();
        try {
            std::string json_str = future.get();
            nlohmann::json json_obj = nlohmann::json::parse(json_str);
            send_json_response(s, json_obj);
        } catch (const std::exception& err) {
            Logger::Error("HttpAdminSrv::post_get_table send_json_response: {}", err.what());
            send_json_response(s, {{"error", err.what()}}, 400);
        }
    } else {
        Logger::Trace("HttpAdminSrv::post_get_table enqueue_get_table {}", table);
        try {
            auto future = enqueue_get_table(table, filter, offset, limit);
            Logger::Trace("HttpAdminSrv::post_get_table future.wait_for {}", table);
            auto status = future.wait_for(std::chrono::seconds(10));
            if (status != std::future_status::ready) {
                auto status_str = (status == std::future_status::timeout) ? "timeout" : "deferred";
                Logger::Error("HttpAdminSrv::post_get_table status {}", status_str);
                send_json_response(s, {{"error", "Request timeout (no response from worker)"}}, 504);
                return;
            }
            Logger::Trace("HttpAdminSrv::post_get_table future.get {}", table);
            std::string json_str = future.get();
            Logger::Trace("HttpAdminSrv::post_get_table json::parse {}", table);
            nlohmann::json json_obj = nlohmann::json::parse(json_str);
            Logger::Trace("HttpAdminSrv::post_get_table send_json_response {}", table);
            send_json_response(s, json_obj);
        } catch (const std::exception& err) {
            Logger::Error("HttpAdminSrv::post_get_table send_json_response: {}; {}", table, err.what());
            send_json_response(s, {{"error", err.what()}}, 400);
        }
    }
    Logger::Trace("HttpAdminSrv::post_get_table FINISH");
}

void HttpAdminSrv::get_or_post_show_db(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, asio::error_code& ec) {
    if (content_length <= 0) {
        serve_static_file(*s, "static/admin/admin_show_db.html", ec);
        if(ec) Logger::Error("HttpAdminSrv::get_show_db write error: {}", ec.message());
        return;
    }
    std::vector<char> body;
    read_body(*s, content_length, body, ec);
    if (ec) {
        Logger::Error("HttpAdminSrv::get_show_db body read: {}", ec.message());
        send_json_response(s, R"({"error":"Invalid JSON"})", 400);
        return;
    }
    std::string body_str(body.begin(), body.end());
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body_str);
    } catch (...) {
        send_json_response(s, R"({"error":"Invalid JSON"})", 400);
        return;
    }
    if (req.value("cmd", "") == "list_tables") {
        auto future = enqueue_list_tables();
        try {
            std::string json_str = future.get();
            nlohmann::json json_obj = nlohmann::json::parse(json_str);
            send_json_response(s, json_obj);
        } catch (const std::exception& err) {
            nlohmann::json err_obj = {{"error", err.what()}};
            send_json_response(s, err_obj, 400);
        }
    } else {
        send_json_response(s, R"({"error":"Unknown command"})", 500);
    }
}

void HttpAdminSrv::post_root(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, const std::string& content_type) {
    asio::error_code ec;
    if (content_length <= 0) {
        Logger::Warn("HttpAdminSrv::post_root: POST without Content-Length");
        send_response(s, "<html><body><h2>Error: Content-Length required</h2></body></html>");
        return;
    }
    std::vector<char> body;
    read_body(*s, content_length, body, ec);
    if (ec) {
        Logger::Error("HttpAdminSrv::post_root read_body: {}", ec.message());
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
        send_response(s, "<html><body><h2>Error: No document content received</h2></body></html>");
        return;
    }
    Logger::Trace("HttpAdminSrv::post_root: document received bytes {};", data.size());
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
    Logger::Trace("HttpAdminSrv::post_root: tags received {};", tags);
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
            if(ec) Logger::Error("HttpAdminSrv::post_root asio::write: {}", ec.message());
        }
    }).detach();
    std::string resp = R"({"status":"accepted","message":"Learning started"})";
    std::string http_resp = build_response("application/json", resp);
    asio::write(*s, asio::buffer(http_resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::post_root asio::write: {}", ec.message());
}

void HttpAdminSrv::post_serialize(std::shared_ptr<asio::ip::tcp::socket> s) {
    auto future = enqueue_serialize();
    std::thread([this, future = std::move(future), s]() mutable {
    asio::error_code ec;
    try {
        future.get();
        std::string resp = R"({"status":"ok","message":"Serialized"})";
        asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
        if(ec) Logger::Error("HttpAdminSrv::post_serialize asio::write: {}", ec.message());
    } catch (const std::exception& err) {
        std::string resp = R"({"status":"error","message":")" + std::string(err.what()) + "\"}";
            asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
            if(ec) Logger::Error("HttpAdminSrv::post_serialize asio::write: {}", ec.message());
        }
    }).detach();
}

void HttpAdminSrv::post_train_uml(std::shared_ptr<asio::ip::tcp::socket> s, int content_length, const std::string& content_type) {
    asio::error_code ec;
    if (content_length <= 0) {
        send_response(s, "<html><body><h2>Error: Content-Length required</h2></body></html>");
        return;
    }
    std::vector<char> body;
    read_body(*s, content_length, body, ec);
    if (ec) { Logger::Error("read_body error: {}", ec.message()); return; }
    std::string bbody(body.begin(), body.end());
    std::string boundary;
    if (content_type.find("multipart/form-data") != std::string::npos) {
        auto pos = content_type.find("boundary=");
        if (pos != std::string::npos) boundary = "--" + content_type.substr(pos + 9);
    } else {
        send_response(s, "<html><body><h2>Only multipart/form-data allowed</h2></body></html>");
        return;
    }
    std::string uml_name;
    size_t pos = 0;
    while ((pos = bbody.find("name=\"uml_name\"", pos)) != std::string::npos) {
        size_t start = bbody.find("\r\n\r\n", pos);
        if (start != std::string::npos) {
            start += 4;
            size_t end = bbody.find(boundary, start);
            if (end != std::string::npos) {
                uml_name = bbody.substr(start, end - start);
                while (!uml_name.empty() && (uml_name.back() == '\r' || uml_name.back() == '\n')) uml_name.pop_back();
                break;
            }
        }
        pos = start;
    }
    if (uml_name.empty()) Logger::Warn("HttpAdminSrv::post_train_uml UML name is empty.");
    std::vector<std::pair<uint8_t, std::string>> source_pairs;
    pos = 0;
    while ((pos = bbody.find("name=\"source_files[]\"", pos)) != std::string::npos) {
        size_t file_start = bbody.find("\r\n\r\n", pos);
        if (file_start == std::string::npos) break;
        file_start += 4;
        size_t file_end = bbody.find(boundary, file_start);
        if (file_end == std::string::npos) break;
        std::string file_content_str = bbody.substr(file_start, file_end - file_start);
        while (!file_content_str.empty() && (file_content_str.back() == '\r' || file_content_str.back() == '\n'))
            file_content_str.pop_back();
        size_t type_pos = bbody.find("name=\"source_types[]\"", file_end);
        if (type_pos == std::string::npos) break;
        size_t type_start = bbody.find("\r\n\r\n", type_pos);
        if (type_start == std::string::npos) break;
        type_start += 4;
        size_t type_end = bbody.find(boundary, type_start);
        if (type_end == std::string::npos) break;
        std::string type_str = bbody.substr(type_start, type_end - type_start);
        while (!type_str.empty() && (type_str.back() == '\r' || type_str.back() == '\n')) type_str.pop_back();
        uint8_t src_type = static_cast<uint8_t>(std::stoi(type_str));
        source_pairs.push_back({src_type, file_content_str});
        pos = type_end;
    }
    std::string uml_content;
    pos = 0;
    while ((pos = bbody.find("name=\"uml_file\"", pos)) != std::string::npos) {
        size_t filename_start = bbody.find("filename=\"", pos);
        if (filename_start == std::string::npos) break;
        filename_start += 10;
        size_t filename_end = bbody.find("\"", filename_start);
        if (filename_end == std::string::npos) break;
        std::string filename = bbody.substr(filename_start, filename_end - filename_start);
        size_t data_start = bbody.find("\r\n\r\n", filename_end);
        if (data_start == std::string::npos) break;
        data_start += 4;
        size_t data_end = bbody.find(boundary, data_start);
        if (data_end == std::string::npos) break;
        uml_content = bbody.substr(data_start, data_end - data_start);
        while (!uml_content.empty() && (uml_content.back() == '\r' || uml_content.back() == '\n')) uml_content.pop_back();
        break;
    }
    std::vector<uint8_t> payload;
    payload.push_back(0); payload.push_back(0);
    payload.insert(payload.end(), uml_name.begin(), uml_name.end());
    payload.push_back(0);
    payload.insert(payload.end(), uml_content.begin(), uml_content.end());
    payload.push_back(0);
    uint16_t count = htons(source_pairs.size());
    payload.insert(payload.end(), (uint8_t*)&count, (uint8_t*)&count + 2);
    for (auto& p : source_pairs) {
        payload.push_back(p.first);
        uint32_t size = htonl(p.second.size());
        payload.insert(payload.end(), (uint8_t*)&size, (uint8_t*)&size + 4);
        payload.insert(payload.end(), p.second.begin(), p.second.end());
    }
    auto future = enqueue_train_uml(payload);
    std::thread([future = std::move(future), s]() mutable {
        asio::error_code ec;
        try {
            future.get();
            std::string resp = R"({"status":"ok","message":"Train UML finished"})";
            asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
        } catch (const std::exception& err) {
            std::string resp = R"({"status":"error","message":")" + std::string(err.what()) + "\"}";
            asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
            if(ec) Logger::Error("HttpAdminSrv::post_train_uml asio::write: {}", ec.message());
        }
    }).detach();
    std::string resp = R"({"status":"accepted","message":"Train UML started"})";
    asio::write(*s, asio::buffer(build_response("application/json", resp)), ec);
    if(ec) Logger::Error("HttpAdminSrv::post_train_uml asio::write: {}", ec.message());
}

void HttpAdminSrv::post_shutdown(std::shared_ptr<asio::ip::tcp::socket> s) {
    auto future = enqueue_shutdown();
    std::thread([future = std::move(future), s, this]() mutable {
        try {
            future.get();
        } catch (...) {}
    }).detach();
    std::string resp = R"({"status":"ok","message":"Service stopping..."})";
    std::string http_resp = build_response("application/json", resp);
    asio::error_code ec;
    asio::write(*s, asio::buffer(http_resp), ec);
    if(ec) Logger::Error("HttpAdminSrv::post_shutdown asio::write: {}", ec.message());
}

void HttpAdminSrv::handle_request(std::shared_ptr<asio::ip::tcp::socket> s) {
    try {
        asio::error_code ec;
        std::string headers = read_headers(*s, ec);
        if (ec) { Logger::Error("HttpAdminSrv::handle_request read headers: {}", ec.message()); return; }
        std::string method, path, version, content_type;
        int content_length = -1;
        parse_request(headers, method, path, version, content_type, content_length);
        //Logger::Trace("HttpAdminSrv::handle_request : {}; {}", method, path);
        if (method == "GET") {
            if (path == "/favicon.ico") {
                get_favicon(s);
                return;
            }
            else if (path == "/config") {
                get_config(s);
                return;
            }
            else if (path == "/progress") {
                get_progress(s);
                return;
            }
            else if (path == "/train_uml") {
                get_train_uml(s);
                return;
            }
            else if (path == "/src_types") {
                get_src_types(s);
                return;
            }
            else if (path == "/show_db") {
                get_or_post_show_db(s, content_length, ec);
                return;
            }
            else if (path == "/") {
                if (serve_static_file(*s, "static/admin/admin.html", ec)) return;
                send_response(s, "<html><body><h2>Admin page not found</h2></body></html>");
                return;
            }
            else if (path.rfind("/static/admin/", 0) == 0) {
                std::string rel_path = path.substr(1);
                if (serve_static_file(*s, rel_path, ec)) return;
            }
            std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            asio::write(*s, asio::buffer(not_found), ec);
            return;
        }
        else if (method == "POST") {
            if (path == "/") {
                post_root(s, content_length, content_type);
                return;
            }
            else if (path == "/train_uml") {
                post_train_uml(s, content_length, content_type);
                return;
            }
            else if (path == "/show_db") {
                get_or_post_show_db(s, content_length, ec);
                return;
            }
            else if (path == "/get_table") {
                post_get_table(s, content_length, ec);
                return;
            }
            else if (path == "/serialize") {
                post_serialize(s);
                return;
            }
            else if (path == "/stop") {
                post_shutdown(s);
                return;
            }
        }
        std::string not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        asio::write(*s, asio::buffer(not_allowed), ec);
        if(ec) Logger::Error("HttpAdminSrv::handle_request asio::write: {}", ec.message());
    } catch (const std::exception& err) {
        Logger::Error("HttpAdminSrv::handle_request: {}", err.what());
    }
}

int HttpAdminSrv::current_progress() const {return current_progress_;}

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
    memory_usage_ = value;
}
size_t HttpAdminSrv::memory_usage() const {
    std::lock_guard<std::mutex> lock(mem_mtx_);
    return memory_usage_;
}
