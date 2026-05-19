#include "client.hpp"

static std::string build_response(const std::string& content_type, const std::string& body) {
    std::string resp = "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: " + content_type + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
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
        if (ec) { Logger::Error("HttpClientSrv::handle_request read headers: {}", ec.message()); return; }
        std::string method, path, version, content_type;
        int content_length = 0;
        parse_request(headers, method, path, version, content_type, content_length);
        Logger::Trace("HttpClientSrv::handle_request: {} {}", method, path);
        auto respond_html = [&](const std::string& body) {
            std::string resp = build_response("text/html", body);
            asio::write(*s, asio::buffer(resp), ec);
        };
        if (method == "GET" && (path == "/" || path == "/ask")) {
            std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <title>AI Knowledge Assistant</title>
    <style>
        body { font-family: sans-serif; max-width: 800px; margin: auto; padding: 20px; }
        #chat { border: 1px solid #ccc; height: 400px; overflow-y: auto; padding: 10px; margin-bottom: 10px; background: #f9f9f9; }
        .message { margin-bottom: 10px; }
        .user { color: blue; font-weight: bold; }
        .assistant { color: green; }
        .feedback { margin-left: 10px; font-size: 0.8em; cursor: pointer; }
        .feedback span { margin: 0 5px; }
        .thumbs-up, .thumbs-down { cursor: pointer; user-select: none; }
        .thumbs-up:hover, .thumbs-down:hover { opacity: 0.7; }
        textarea { width: 100%; padding: 8px; }
        button { padding: 8px 16px; margin-top: 5px; }
    </style>
</head>
<body>
    <h2>Ask a Question</h2>
    <div id="chat"></div>
    <div>
    <label>Confidence threshold:
    <input type="range" id="thresholdSlider" min="0" max="1" step="0.0001" value="0.0001" style="width:100%">
    <span id="thresholdValue">0.0001</span>
    </label>
    </div>
    <textarea id="promptInput" rows="3" placeholder="Type your question here..."></textarea><br>
    <button id="sendBtn">Send</button>
    <button id="clearBtn">Clear Chat</button>
    <script>
        const slider = document.getElementById('thresholdSlider');
        const thresholdSpan = document.getElementById('thresholdValue');
        slider.addEventListener('input', () => {
            thresholdSpan.textContent = slider.value;
        });
        const chatDiv = document.getElementById('chat');
        const promptInput = document.getElementById('promptInput');
        const sendBtn = document.getElementById('sendBtn');
        function addMessage(role, text, chunkId = null) {
            const msgDiv = document.createElement('div');
            msgDiv.className = 'message';
            const roleSpan = document.createElement('span');
            roleSpan.className = role;
            roleSpan.textContent = role === 'user' ? 'You: ' : 'Assistant: ';
            const textSpan = document.createElement('span');
            textSpan.textContent = text;
            msgDiv.appendChild(roleSpan);
            msgDiv.appendChild(textSpan);
            if (role === 'assistant' && chunkId !== null && chunkId !== -1) {
                const feedbackSpan = document.createElement('span');
                feedbackSpan.className = 'feedback';
                feedbackSpan.innerHTML = ` <span class="thumbs-up" data-chunk="${chunkId}" data-question="">👍</span> <span class="thumbs-down" data-chunk="${chunkId}" data-question="">👎</span>`;
                msgDiv.appendChild(feedbackSpan);
            }
            chatDiv.appendChild(msgDiv);
            chatDiv.scrollTop = chatDiv.scrollHeight;
            if (role === 'assistant' && chunkId !== null && chunkId !== -1) {
                const up = msgDiv.querySelector('.thumbs-up');
                const down = msgDiv.querySelector('.thumbs-down');
                const questionText = textSpan.textContent; // or store question separately
                up.addEventListener('click', () => sendFeedback(chunkId, true, questionText));
                down.addEventListener('click', () => sendFeedback(chunkId, false, questionText));
            }
        }
        async function sendFeedback(chunkId, isPositive, questionText) {
            console.log(`Feedback: chunk ${chunkId}, positive=${isPositive}, question=${questionText}`);
            try {
                const response = await fetch('/feedback', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ chunk_id: chunkId, positive: isPositive, question: questionText })
                });
                if (!response.ok) throw new Error('Feedback failed');
                const result = await response.json();
                console.log('Feedback saved:', result);
            } catch (err) {
                console.error('Feedback error:', err);
            }
        }
        async function askQuestion() {
            const prompt = promptInput.value.trim();
            if (!prompt) return;
            const threshold = slider.value;
            addMessage('user', prompt);
            promptInput.value = '';
            sendBtn.disabled = true;
            try {
                const response = await fetch('/ask', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: `prompt=${encodeURIComponent(prompt)}&threshold=${encodeURIComponent(threshold)}`
                });
                const data = await response.json();
                addMessage('assistant', data.answer, data.chunk_id);
            } catch (err) {
                addMessage('assistant', 'Error: ' + err.message);
            } finally {
                sendBtn.disabled = false;
                promptInput.focus();
            }
        }
        sendBtn.addEventListener('click', askQuestion);
        document.getElementById('clearBtn').addEventListener('click', () => {document.getElementById('chat').innerHTML = '';});
        promptInput.addEventListener('keypress', (e) => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); askQuestion(); } });
        promptInput.focus();
    </script>
</body>
</html>)";
            respond_html(html);
            return;
        }
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
            double threshold = -1.0; // -1 means use default
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
                auto req = json::parse(json_body);
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
                auto ans = json::parse(answer_json);
                std::string answer_text = ans.value("answer", "No answer");
                json response;
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
            } catch (const std::exception& e) {
                json error;
                error["error"] = {{"message", e.what()}, {"type", "invalid_request_error"}, {"code", 400}};
                std::string resp = build_response("application/json", error.dump());
                asio::write(*s, asio::buffer(resp), ec);
            }
            return;
        }
    } catch (const std::exception& err) {
        Logger::Error("HttpClientSrv::handle_request: {}", err.what());
    }
}
