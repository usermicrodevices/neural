#include "http.hpp"
#include "client.hpp"
#include <filesystem>

static std::string generate_chat_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string id = "chatcmpl-";
    for (int i = 0; i < 24; ++i)
        id += hex[dis(gen)];
    return id;
}

static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int value = 0;
            std::istringstream is(in.substr(i + 1, 2));
            if (is >> std::hex >> value) { out += static_cast<char>(value); i += 2; }
            else out += in[i];
        } else if (in[i] == '+') out += ' ';
        else out += in[i];
    }
    return out;
}

HttpClientSrv::HttpClientSrv(asio::io_context& io, unsigned short port)
    : io_(io), acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {}

HttpClientSrv::~HttpClientSrv() { stop(); }

void HttpClientSrv::start() {
    thread_ = std::thread([this]() { do_accept(); io_.run(); });
}
void HttpClientSrv::stop() {
    stop_ = true; cv_.notify_all(); acceptor_.close();
    if (thread_.joinable()) thread_.join();
}

std::future<std::string> HttpClientSrv::enqueue_ask(const std::string& prompt, double threshold) {
    std::promise<std::string> p; auto f = p.get_future();
    { std::lock_guard<std::mutex> lock(mtx_); jobs_.push({prompt, threshold, std::move(p)}); }
    cv_.notify_one();
    return f;
}

bool HttpClientSrv::dequeue_ask(std::string& prompt, double& threshold, std::promise<std::string>& promise) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !jobs_.empty() || stop_; });
    if (stop_ && jobs_.empty()) return false;
    JobPrompt job = std::move(jobs_.front()); jobs_.pop();
    prompt = std::move(job.prompt);
    threshold = job.threshold;
    promise = std::move(job.answer);
    return true;
}

void HttpClientSrv::enqueue_uml_search(const std::string& query, double threshold, std::promise<std::string>&& promise) {
    std::lock_guard<std::mutex> lock(mtx_);
    uml_search_jobs_.push({query, threshold, std::move(promise)});
    cv_.notify_one();
}

bool HttpClientSrv::dequeue_uml_search(std::string& query, double& threshold, std::promise<std::string>& promise) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !uml_search_jobs_.empty() || stop_; });
    if (stop_ && uml_search_jobs_.empty()) return false;
    JobUMLSearch job = std::move(uml_search_jobs_.front()); uml_search_jobs_.pop();
    query = std::move(job.query);
    threshold = job.threshold;
    promise = std::move(job.answer);
    return true;
}

bool HttpClientSrv::try_dequeue_uml_search(std::string& query, double& threshold, std::promise<std::string>& promise) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (uml_search_jobs_.empty()) return false;
    JobUMLSearch job = std::move(uml_search_jobs_.front()); uml_search_jobs_.pop();
    query = std::move(job.query);
    threshold = job.threshold;
    promise = std::move(job.answer);
    return true;
}

void HttpClientSrv::enqueue_list_uml(std::promise<std::string>&& promise) {
    std::lock_guard<std::mutex> lock(mtx_);
    list_uml_jobs_.push({std::move(promise)});
    cv_.notify_one();
}

bool HttpClientSrv::dequeue_list_uml(std::promise<std::string>& promise) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !list_uml_jobs_.empty() || stop_; });
    if (stop_ && list_uml_jobs_.empty()) return false;
    JobListUML job = std::move(list_uml_jobs_.front()); list_uml_jobs_.pop();
    promise = std::move(job.answer);
    return true;
}

void HttpClientSrv::enqueue_compose(const std::vector<std::string>& block_names, std::promise<std::string>&& promise) {
    std::lock_guard<std::mutex> lock(mtx_);
    compose_jobs_.push({block_names, std::move(promise)});
    cv_.notify_one();
}

bool HttpClientSrv::dequeue_compose(std::vector<std::string>& block_names, std::promise<std::string>& promise) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !compose_jobs_.empty() || stop_; });
    if (stop_ && compose_jobs_.empty()) return false;
    JobCompose job = std::move(compose_jobs_.front()); compose_jobs_.pop();
    block_names = std::move(job.block_names);
    promise = std::move(job.answer);
    return true;
}

enum class DequeueResult { NONE, PROMPT, UML_SEARCH, LIST_UML, COMPOSE };

HttpClientSrv::DequeueResult HttpClientSrv::dequeue_any(std::string& prompt, double& threshold, std::promise<std::string>& promise,
                                          std::string& uml_query, double& uml_threshold, std::promise<std::string>& uml_promise,
                                          std::promise<std::string>& list_uml_promise,
                                          std::vector<std::string>& compose_block_names, std::promise<std::string>& compose_promise) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() { return !jobs_.empty() || !uml_search_jobs_.empty() || !list_uml_jobs_.empty() || !compose_jobs_.empty() || stop_; });
    if (stop_ && jobs_.empty() && uml_search_jobs_.empty() && list_uml_jobs_.empty() && compose_jobs_.empty()) return DequeueResult::NONE;
    if (!list_uml_jobs_.empty()) {
        JobListUML job = std::move(list_uml_jobs_.front()); list_uml_jobs_.pop();
        list_uml_promise = std::move(job.answer);
        return DequeueResult::LIST_UML;
    }
    if (!compose_jobs_.empty()) {
        JobCompose job = std::move(compose_jobs_.front()); compose_jobs_.pop();
        compose_block_names = std::move(job.block_names);
        compose_promise = std::move(job.answer);
        return DequeueResult::COMPOSE;
    }
    if (!uml_search_jobs_.empty()) {
        JobUMLSearch job = std::move(uml_search_jobs_.front()); uml_search_jobs_.pop();
        uml_query = std::move(job.query);
        uml_threshold = job.threshold;
        uml_promise = std::move(job.answer);
        return DequeueResult::UML_SEARCH;
    }
    if (!jobs_.empty()) {
        JobPrompt job = std::move(jobs_.front()); jobs_.pop();
        prompt = std::move(job.prompt);
        threshold = job.threshold;
        promise = std::move(job.answer);
        return DequeueResult::PROMPT;
    }
    return DequeueResult::NONE;
}

void HttpClientSrv::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) std::thread(&HttpClientSrv::handle_request, this, std::make_shared<asio::ip::tcp::socket>(std::move(socket))).detach();
        if (!stop_) do_accept();
    });
}

void HttpClientSrv::handle_request(std::shared_ptr<asio::ip::tcp::socket> s) {
    try {
        asio::error_code ec;
        std::string headers = read_headers(*s, ec);
        if (ec) { Logger::Error("HttpClientSrv::handle_request read headers: {};", ec.message()); return; }
        std::string method, path, version, content_type;
        int content_length = 0;
        parse_request(headers, method, path, version, content_type, content_length);
        //Logger::Trace("HttpClientSrv::handle_request: {}; {};", method, path);
        if (method == "GET" && path == "/favicon.ico") {
            std::string svg = read_file_into_string("static/client/favicon.svg");
            std::string resp = build_response("image/svg+xml", svg);
            asio::write(*s, asio::buffer(resp), ec);
            return;
        }
        if (method == "GET" && (path == "/" || path == "/ask")) {
            if (serve_static_file(*s, "static/client/client.html", ec)) return;
            std::string resp = build_response("text/plain", "Index not found");
            asio::write(*s, asio::buffer(resp), ec);
            return;
        }
        if (method == "GET" && path == "/list_uml") {
            try {
                std::promise<std::string> promise;
                auto fut = promise.get_future();
                enqueue_list_uml(std::move(promise));
                std::string result = fut.get();
                std::string http_resp = build_response("application/json", result);
                asio::write(*s, asio::buffer(http_resp), ec);
            } catch (const std::exception& e) {
                Logger::Error("GET /list_uml error: {}", e.what());
                std::string http_resp = build_response("application/json", "{\"error\":\"" + std::string(e.what()) + "\"}");
                asio::write(*s, asio::buffer(http_resp), ec);
            }
            return;
        }
        if (method == "GET") {
            if (serve_static_file(*s, path, ec)) {
                Logger::Trace("HttpClientSrv::handle_request file was served successfully: {}; {};", path, ec.message());
                return;
            }
            if (ec) {
                Logger::Error("HttpClientSrv::handle_request static file serve: {};", ec.message());
            }
            std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            asio::write(*s, asio::buffer(not_found), ec);
            return;
        }
        auto respond_html = [&](const std::string& body) {
            std::string resp = build_response("text/html", body);
            asio::write(*s, asio::buffer(resp), ec);
        };
        if (method == "POST" && (path == "/" || path == "/ask")) {
            if (content_length <= 0) {
                Logger::Warn("HttpClientSrv::handle_request: POST without Content-Length");
                respond_html("<html><body><h2>Error: Content-Length required</h2><a href=\"/\">Back</a></body></html>");
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) { Logger::Error("HttpClientSrv::handle_request body read: {}", ec.message()); return; }
            std::string bbody(body.begin(), body.end());
            std::string prompt;
            double threshold = -1.0; // -1 use default
            auto pos = bbody.find("prompt=");
            if (pos != std::string::npos) {
                prompt = bbody.substr(pos + 7);
                auto amp = prompt.find('&');
                if (amp != std::string::npos) {
                    std::string rest = prompt.substr(amp + 1);
                    prompt = prompt.substr(0, amp);
                    auto tpos = rest.find("threshold=");
                    if (tpos != std::string::npos) {
                        std::string tstr = rest.substr(tpos + 10);
                        auto end = tstr.find('&');
                        if (end != std::string::npos) tstr = tstr.substr(0, end);
                        try {
                            threshold = std::stod(tstr);
                        } catch (...) {}
                    }
                }
                prompt = url_decode(prompt);
            }
            if (prompt.empty()) {
                respond_html("<html><body><h2>Error: No prompt provided</h2><a href=\"/\">Back</a></body></html>");
                return;
            }
            Logger::Info("HttpClientSrv::handle_request: prompt '{}'", prompt);
            auto future = enqueue_ask(prompt, threshold);
            try {
                std::string answer = future.get();
                std::string resp = build_response("application/json", answer);
                asio::write(*s, asio::buffer(resp), ec);
            } catch (const std::exception& err) {
                std::string error_json = "{\"error\":\"" + std::string(err.what()) + "\"}";
                std::string resp = build_response("application/json", error_json);
                asio::write(*s, asio::buffer(resp), ec);
            }
        }
        else if (method == "POST" && path == "/feedback") {
            if (content_length <= 0) {
                respond_html("<h2>Error</h2>");
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) return;
            std::string jsonBody(body.begin(), body.end());
            int chunk_id = -1;
            bool positive = false;
            std::string question;
            auto find_int = [&](const std::string& key) -> int {
                size_t pos = jsonBody.find("\"" + key + "\":");
                if (pos == std::string::npos) return -1;
                pos = jsonBody.find(':', pos);
                if (pos == std::string::npos) return -1;
                pos++;
                while (pos < jsonBody.size() && (jsonBody[pos] == ' ' || jsonBody[pos] == '\t')) pos++;
                int val = 0;
                bool neg = false;
                if (jsonBody[pos] == '-') { neg = true; pos++; }
                while (pos < jsonBody.size() && std::isdigit(jsonBody[pos])) {
                    val = val * 10 + (jsonBody[pos] - '0');
                    pos++;
                }
                return neg ? -val : val;
            };
            auto find_bool = [&](const std::string& key) -> bool {
                size_t pos = jsonBody.find("\"" + key + "\":");
                if (pos == std::string::npos) return false;
                pos = jsonBody.find(':', pos);
                if (pos == std::string::npos) return false;
                pos++;
                while (pos < jsonBody.size() && (jsonBody[pos] == ' ' || jsonBody[pos] == '\t')) pos++;
                return jsonBody.compare(pos, 4, "true") == 0;
            };
            auto find_string = [&](const std::string& key) -> std::string {
                size_t pos = jsonBody.find("\"" + key + "\":");
                if (pos == std::string::npos) return "";
                pos = jsonBody.find(':', pos);
                if (pos == std::string::npos) return "";
                pos++;
                while (pos < jsonBody.size() && (jsonBody[pos] == ' ' || jsonBody[pos] == '\t')) pos++;
                if (jsonBody[pos] != '"') return "";
                pos++;
                size_t end = jsonBody.find('"', pos);
                if (end == std::string::npos) return "";
                return jsonBody.substr(pos, end - pos);
            };
            chunk_id = find_int("chunk_id");
            positive = find_bool("positive");
            question = find_string("question");
            if (chunk_id != -1 && !question.empty()) {
                Logger::Trace("HttpClientSrv::handle_request: Received feedback: chunk {} positive={} question='{}'", chunk_id, positive, question);
                uint32_t net_chunk = htonl(chunk_id);
                std::vector<uint8_t> payload(sizeof(net_chunk));
                std::memcpy(payload.data(), &net_chunk, 4);
                payload.insert(payload.end(), question.begin(), question.end());
            }
            std::string resp = "{\"status\":\"ok\"}";
            std::string http_resp = build_response("application/json", resp);
            asio::write(*s, asio::buffer(http_resp), ec);
            return;
        }
        else if (method == "POST" && path == "/v1/chat") {
            if (content_length <= 0) {
                respond_html("<html><body><h2>Error: Content-Length required</h2></body></html>");
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) {
                Logger::Error("HttpClientSrv::handle_request /v1/chat body read: {}", ec.message());
                return;
            }
            std::string json_body(body.begin(), body.end());
            Logger::Trace("HttpClientSrv::handle_request JSON: {}", json_body);
            try {
                auto req = nlohmann::json::parse(json_body);
                std::string user_prompt;
                if (req.contains("messages")) {
                    for (const auto& msg : req["messages"]) {
                        if (msg.value("role", "") == "user") {
                            user_prompt = msg.value("content", "");
                            break;
                        }
                    }
                }
                if (user_prompt.empty()) {
                    throw std::runtime_error("No user message found");
                }
                double threshold = 0.001;
                if (req.contains("confidence_threshold") && req["confidence_threshold"].is_number()) {
                    double ct = req["confidence_threshold"].get<double>();
                    threshold = std::max(0.0, std::min(1.0, ct));
                    if (threshold == 0.0) threshold = 0.0001;
                    Logger::Trace("HttpClientSrv::handle_request: using direct confidence_threshold {}", threshold);
                }
                else {
                    double temperature = 0.7;
                    if (req.contains("temperature") && req["temperature"].is_number())
                        temperature = req["temperature"].get<double>();
                    threshold = 0.001;
                    if (temperature >= 0 && temperature <= 1) {
                        threshold = 1.0 - temperature;
                        threshold = std::max(0.001, std::min(0.999, threshold));
                    }
                    Logger::Trace("HttpClientSrv::handle_request: using temperature {} mapped to threshold {}", temperature, threshold);
                }
                auto future = enqueue_ask(user_prompt, threshold);
                std::string answer_json = future.get();
                auto ans = nlohmann::json::parse(answer_json);
                std::string answer_text = ans.value("answer", "No answer");
                nlohmann::json response;
                response["id"] = generate_chat_id();
                response["object"] = "chat.completion";
                response["created"] = std::chrono::system_clock::now().time_since_epoch().count();
                response["model"] = req.value("model", "neural");
                response["choices"] = {
                    {
                        {"index", 0},
                        {"message", {{"role", "assistant"}, {"content", answer_text}}},
                        {"finish_reason", "stop"}
                    }
                };
                response["usage"] = {{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}};
                std::string resp_body = response.dump();
                std::string http_resp = build_response("application/json", resp_body);
                asio::write(*s, asio::buffer(http_resp), ec);
            } catch (const std::exception& err) {
                nlohmann::json error;
                error["error"] = {{"message", err.what()}, {"type", "invalid_request_error"}, {"code", 400}};
                std::string resp = build_response("application/json", error.dump());
                asio::write(*s, asio::buffer(resp), ec);
            }
            return;
        }
        if (method == "POST" && path == "/search_uml") {
            if (content_length <= 0) {
                Logger::Warn("HttpClientSrv::handle_request: POST /search_uml without Content-Length");
                std::string resp = build_response("application/json", "{\"error\":\"Content-Length required\"}");
                asio::write(*s, asio::buffer(resp), ec);
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) { Logger::Error("POST /search_uml body read: {}", ec.message()); return; }
            std::string json_body(body.begin(), body.end());
            try {
                auto req = nlohmann::json::parse(json_body);
                std::string query = req.at("query").get<std::string>();
                double threshold = req.value("threshold", 0.5);
                std::promise<std::string> promise;
                auto fut = promise.get_future();
                enqueue_uml_search(query, threshold, std::move(promise));
                std::string result = fut.get();
                std::string http_resp = build_response("application/json", result);
                asio::write(*s, asio::buffer(http_resp), ec);
            } catch (const std::exception& e) {
                Logger::Error("POST /search_uml error: {}", e.what());
                std::string err = "{\"error\":\"" + std::string(e.what()) + "\"}";
                std::string http_resp = build_response("application/json", err);
                asio::write(*s, asio::buffer(http_resp), ec);
            }
            return;
        }
        if (method == "POST" && path == "/compose") {
            if (content_length <= 0) {
                std::string resp = build_response("application/json", "{\"error\":\"Content-Length required\"}");
                asio::write(*s, asio::buffer(resp), ec);
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) { Logger::Error("POST /compose body read: {}", ec.message()); return; }
            std::string json_body(body.begin(), body.end());
            try {
                auto req = nlohmann::json::parse(json_body);
                std::vector<std::string> block_names;
                if (req.contains("blocks") && req["blocks"].is_array()) {
                    for (auto& b : req["blocks"]) {
                        block_names.push_back(b.get<std::string>());
                    }
                }
                std::promise<std::string> promise;
                auto fut = promise.get_future();
                enqueue_compose(block_names, std::move(promise));
                std::string result = fut.get();
                std::string http_resp = build_response("application/json", result);
                asio::write(*s, asio::buffer(http_resp), ec);
            } catch (const std::exception& e) {
                Logger::Error("POST /compose error: {}", e.what());
                std::string http_resp = build_response("application/json", "{\"error\":\"" + std::string(e.what()) + "\"}");
                asio::write(*s, asio::buffer(http_resp), ec);
            }
            return;
        }
        if (method == "POST" && path == "/build_project") {
            if (content_length <= 0) {
                std::string resp = build_response("application/json", "{\"error\":\"Content-Length required\"}");
                asio::write(*s, asio::buffer(resp), ec);
                return;
            }
            std::vector<char> body;
            read_body(*s, content_length, body, ec);
            if (ec) { Logger::Error("POST /build_project body read: {}", ec.message()); return; }
            std::string json_body(body.begin(), body.end());
            try {
                auto req = nlohmann::json::parse(json_body);
                std::vector<std::string> block_names;
                std::function<void(const nlohmann::json&)> extractNames;
                extractNames = [&](const nlohmann::json& node) {
                    if (node.is_string()) {
                        block_names.push_back(node.get<std::string>());
                    } else if (node.is_object()) {
                        if (node.contains("name")) block_names.push_back(node["name"].get<std::string>());
                        if (node.contains("children") && node["children"].is_array()) {
                            for (auto& ch : node["children"]) extractNames(ch);
                        }
                    }
                };
                if (req.contains("blocks") && req["blocks"].is_array()) {
                    for (auto& b : req["blocks"]) extractNames(b);
                }
                std::string project_name = req.value("project_name", "project");
                for (auto& c : project_name) { if (!std::isalnum(c) && c != '_' && c != '-') c = '_'; }
                if (project_name.empty()) project_name = "project";
                std::string project_type = req.value("project_type", "python");
                std::string table_name = req.value("table_name", "records");
                std::vector<std::string> columns;
                if (req.contains("columns") && req["columns"].is_array()) {
                    for (auto& c : req["columns"]) {
                        columns.push_back(c.get<std::string>());
                    }
                }

                std::promise<std::string> promise;
                auto fut = promise.get_future();
                enqueue_compose(block_names, std::move(promise));
                std::string result = fut.get();
                auto compose_data = nlohmann::json::parse(result);

                std::filesystem::path proj_dir = std::filesystem::path("projects") / project_name;
                std::error_code fs_ec;
                std::filesystem::create_directories(proj_dir, fs_ec);
                if (fs_ec) {
                    std::string err = "{\"error\":\"Failed to create directory: " + fs_ec.message() + "\"}";
                    std::string http_resp = build_response("application/json", err);
                    asio::write(*s, asio::buffer(http_resp), ec);
                    return;
                }

                std::vector<std::string> written_files;
                if (compose_data.contains("uml_composite")) {
                    std::ofstream ufo(proj_dir / "project.puml");
                    ufo << compose_data["uml_composite"].get<std::string>();
                    ufo.close();
                    written_files.push_back("project.puml");
                }
                if (compose_data.contains("blocks") && compose_data["blocks"].is_array()) {
                    for (auto& block : compose_data["blocks"]) {
                        std::string bname = block.value("name", "unknown");
                        if (block.contains("sources") && block["sources"].is_array()) {
                            for (auto& src : block["sources"]) {
                                std::string stype = src.value("type", "src");
                                std::string content = src.value("content", "");
                                bool match = false;
                                if (project_type == "python" && (stype.find("Python") != std::string::npos || stype.find("python") != std::string::npos)) match = true;
                                else if (project_type == "cpp" && (stype.find("C++") != std::string::npos || stype.find("cpp") != std::string::npos)) match = true;
                                else if (project_type == "java" && (stype.find("Java") != std::string::npos || stype.find("java") != std::string::npos)) match = true;
                                if (!match) continue;
                                std::string ext = ".txt";
                                if (stype.find("C++") != std::string::npos || stype.find("cpp") != std::string::npos) ext = ".cpp";
                                else if (stype.find("Python") != std::string::npos || stype.find("python") != std::string::npos) ext = ".py";
                                else if (stype.find("Java") != std::string::npos || stype.find("java") != std::string::npos) ext = ".java";
                                std::string fname = bname + ext;
                                std::ofstream sfo(proj_dir / fname);
                                sfo << content;
                                sfo.close();
                                written_files.push_back(fname);
                            }
                        }
                    }
                }

                if (project_type == "python") {
                    std::filesystem::path src_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "python";
                    std::vector<std::string> py_components = {
                        "data_table.py", "button_add.py", "button_edit.py", "button_delete.py",
                        "search_field.py", "table_row.py", "table_column.py",
                        "table_column_title.py", "table_row_title.py", "context_menu.py"
                    };
                    for (auto& comp : py_components) {
                        std::filesystem::path src = src_dir / comp;
                        if (std::filesystem::exists(src)) {
                            std::filesystem::copy_file(src, proj_dir / comp, std::filesystem::copy_options::overwrite_existing);
                            written_files.push_back(comp);
                        }
                    }

                    std::ofstream sfo(proj_dir / "server.py");
                    std::string columns_str = "[";
                    for (size_t i = 0; i < columns.size(); ++i) {
                        if (i > 0) columns_str += ", ";
                        columns_str += "\"" + columns[i] + "\"";
                    }
                    columns_str += "]";
                    sfo << "#!/usr/bin/env python3\n";
                    sfo << "import os, sys\n";
                    sfo << "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n";
                    sfo << "from data_table import DataTable\n";
                    sfo << "from button_add import ButtonAdd, Form\n";
                    sfo << "from button_edit import ButtonEdit\n";
                    sfo << "from button_delete import ButtonDelete\n";
                    sfo << "from search_field import SearchField, ButtonSearch\n";
                    sfo << "from table_row import TableRow\n";
                    sfo << "from table_column import TableColumn\n";
                    sfo << "from table_column_title import DataTableColumnTitle as TableColumnTitle\n";
                    sfo << "from table_row_title import TableRowTitle\n";
                    sfo << "from context_menu import ContextMenu, MenuItem\n";
                    sfo << "from http.server import HTTPServer, BaseHTTPRequestHandler\n";
                    sfo << "from urllib.parse import urlparse, parse_qs\n";
                    sfo << "import json\n\n";
                    sfo << "COLUMNS = " << columns_str << "\n";
                    sfo << "TABLE_NAME = \"" << table_name << "\"\n\n";
                    sfo.close();
                    written_files.push_back("server.py");
                }

                nlohmann::json resp;
                resp["status"] = "ok";
                resp["project_dir"] = proj_dir.string();
                resp["files"] = written_files;
                std::string http_resp = build_response("application/json", resp.dump());
                asio::write(*s, asio::buffer(http_resp), ec);
            } catch (const std::exception& e) {
                Logger::Error("POST /build_project error: {}", e.what());
                std::string http_resp = build_response("application/json", "{\"error\":\"" + std::string(e.what()) + "\"}");
                asio::write(*s, asio::buffer(http_resp), ec);
            }
            return;
        }
    } catch (const std::exception& err) {
        Logger::Error("HttpClientSrv::handle_request: {}", err.what());
    }
}

