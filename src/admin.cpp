#include "admin.hpp"

static std::string build_response(const std::string& content_type, const std::string& body) {
    std::string resp = "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: " + content_type + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}

static std::string read_headers(asio::ip::tcp::socket& socket, asio::error_code& ec) {
    std::string headers;
    char c;
    std::string term = "\r\n\r\n";
    size_t matched = 0;
    while (matched < term.size()) {
        size_t n = socket.read_some(asio::buffer(&c, 1), ec); (void)n;
        if (ec) return headers;
        headers += c;
        if (c == term[matched]) ++matched;
        else matched = (c == term[0] ? 1 : 0);
    }
    if (headers.size() >= 4) headers.resize(headers.size() - 4);
    return headers;
}

static void parse_request(const std::string& headers,
                          std::string& method, std::string& path, std::string& version,
                          std::string& content_type, int& content_length) {
    std::istringstream stream(headers);
    stream >> method >> path >> version;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("content-type:", 0) == 0) {
            content_type = line.substr(13);
            size_t first = content_type.find_first_not_of(" \t");
            if (first != std::string::npos) content_type = content_type.substr(first);
        } else if (lower.rfind("content-length:", 0) == 0) {
            std::string val = line.substr(15);
            size_t first = val.find_first_not_of(" \t");
            if (first != std::string::npos) val = val.substr(first);
            try { content_length = std::stoi(val); } catch (...) { content_length = -1; }
        }
    }
}

static void read_body(asio::ip::tcp::socket& socket, int content_length,
                      std::vector<char>& body, asio::error_code& ec) {
    body.resize(content_length);
    size_t total = 0;
    while (total < static_cast<size_t>(content_length)) {
        size_t n = socket.read_some(asio::buffer(body.data() + total, content_length - total), ec);
        if (ec) return;
        total += n;
    }
}

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
        //Logger::Trace("HttpAdminSrv::handle_request: {} {}", method, path);
        auto respond_html = [&](const std::string& body) {
            std::string resp = build_response("text/html", body);
            asio::write(*s, asio::buffer(resp), ec);
        };
        if (method == "GET" && path == "/") {
            //double mu = static_cast<double>(memory_usage_)/1048576;
            //Logger::Trace("HttpAdminSrv::handle_request: neural memory usage: {} (double={})", std::to_string(mu), mu);
            respond_html(R"(<!DOCTYPE html>
<html>
<head>
    <title>Admin - Upload Document</title>
    <style>
        body { font-family: sans-serif; max-width: 600px; margin: auto; padding: 20px; }
        #progress-container { width: 100%; background: #f0f0f0; margin: 20px 0; display: none; }
        #progress-bar { width: 0%; height: 30px; background: #4caf50; text-align: center; line-height: 30px; color: white; }
        #status { margin: 10px 0; }
        button:disabled { opacity: 0.6; }
    </style>
</head>
<body>
    <h2>Upload Document for Learning</h2>
    <p><span id="memoryUsage">Memory: )" + std::to_string(static_cast<double>(memory_usage_)/1048576) + R"( MB</span></p>
    <form id="uploadForm" enctype="multipart/form-data">
        <input type="file" name="file" id="fileInput" required><br><br>
        <button type="submit" id="submitBtn">Learn</button>
    </form>
    <div id="progress-container"><div id="progress-bar">0%</div></div>
    <div id="status"></div>
    <div>
        <label><input type="checkbox" id="serializeCheckbox" checked> Serialize to disk after learning</label>
        <button id="serializeNowBtn">Serialize Now</button>
    </div>
    <script>
    const form = document.getElementById('uploadForm');
    const fileInput = document.getElementById('fileInput');
    const submitBtn = document.getElementById('submitBtn');
    const progressContainer = document.getElementById('progress-container');
    const progressBar = document.getElementById('progress-bar');
    const statusDiv = document.getElementById('status');
    const serializeCheckbox = document.getElementById('serializeCheckbox');
    const serializeBtn = document.getElementById('serializeNowBtn');
    let pollInterval = null;
    function setControlsDisabled(disabled) {
        serializeCheckbox.disabled = disabled;
        serializeBtn.disabled = disabled;
    }
    fileInput.addEventListener('change', () => {
        progressContainer.style.display = 'none';
    progressBar.style.width = '0%';
    progressBar.textContent = '0%';
    statusDiv.innerHTML = '';
    if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
    }
    submitBtn.disabled = false;
    setControlsDisabled(false);
    });
    async function pollProgress() {
        try {
            const res = await fetch('/progress');
            const data = await res.json();
            if (data.training) {
                progressContainer.style.display = 'block';
            progressBar.style.width = data.progress + '%';
            progressBar.textContent = data.progress + '%';
            statusDiv.textContent = 'Learning in progress...';
            setControlsDisabled(true);
            } else {
                if (pollInterval) clearInterval(pollInterval);
                progressBar.style.width = '100%';
                progressBar.textContent = '100%';
            statusDiv.innerHTML = '<span style="color:green;">Learning Complete! You can upload another document.</span>';
            if (data.memory) {
                const memMB = (data.memory / (1024*1024)).toFixed(2);
                document.getElementById('memoryUsage').innerHTML = `Memory used: ${memMB} MB`;
            }
            submitBtn.disabled = false;
            fileInput.disabled = false;
            setControlsDisabled(false);
            }
        } catch (err) {
            console.error('Progress poll error:', err);
        }
    }
    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const file = fileInput.files[0];
        if (!file) return;
                          const formData = new FormData();
        formData.append('file', file);
                          formData.append('serialize', serializeCheckbox.checked ? '1' : '0');
                          submitBtn.disabled = true;
        fileInput.disabled = true;
        setControlsDisabled(true);
        statusDiv.innerHTML = '';
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';
    progressBar.textContent = '0%';
    try {
        const response = await fetch('/', {
            method: 'POST',
            body: formData
        });
        const result = await response.json();
        if (result.status === 'accepted') {
            if (pollInterval) clearInterval(pollInterval);
                          pollInterval = setInterval(pollProgress, 500);
        } else {
            throw new Error(result.message || 'Upload failed');
        }
    } catch (err) {
        statusDiv.innerHTML = '<span style="color:red;">Error: ' + err.message + '</span>';
    submitBtn.disabled = false;
    fileInput.disabled = false;
    setControlsDisabled(false);
    progressContainer.style.display = 'none';
    }
    });
    document.getElementById('serializeNowBtn').addEventListener('click', async () => {
        serializeBtn.disabled = true;
        try {
            const response = await fetch('/serialize', { method: 'POST' });
            const result = await response.json();
            if (result.status === 'ok') {
                const statusDiv = document.getElementById('status');
                statusDiv.innerHTML = '<span style="color:blue;">Serialized to disk.</span>';
    setTimeout(() => {
        if (statusDiv.innerHTML === '<span style="color:blue;">Serialized to disk.</span>')
            statusDiv.innerHTML = '';
    }, 2000);
            } else {
                throw new Error(result.message || 'Serialization failed');
            }
        } catch (err) {
            console.error('Serialize error:', err);
            const statusDiv = document.getElementById('status');
            statusDiv.innerHTML = '<span style="color:red;">Error: ' + err.message + '</span>';
        } finally {
            try {
                const res = await fetch('/progress');
                const data = await res.json();
                serializeBtn.disabled = data.training;
            } catch (e) {
                serializeBtn.disabled = false;
            }
        }
    });
    </script>
    </body>
</html>)");
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
            Logger::Info("HttpAdminSrv::handle_request: document received bytes {}", data.size());
            bool serialize_flag = true;
            std::string serialize_value;
            size_t pos = 0;
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
            payload.insert(payload.end(), data.begin(), data.end());
            auto future = enqueue_learn(payload);
            std::thread([this, future = std::move(future), respond_html = std::move(respond_html), s]() mutable {
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
        else if (method == "POST" && path == "/retrain") {
            respond_html("<html><body><h2>Retraining started</h2></body></html>");
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
    } catch (const std::exception& err) {
        Logger::Error("HttpAdminSrv::handle_request: {}", err.what());
    }
}

int HttpAdminSrv::get_progress() const {return current_progress_;}

bool HttpAdminSrv::is_training() const {return is_training_;}

void HttpAdminSrv::set_training_started() {
    std::lock_guard<std::mutex> lock(progress_mtx_);
    is_training_ = true;
    current_progress_ = 0;   // optional
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
