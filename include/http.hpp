#pragma once

#include <filesystem>
#include <fstream>

#include <asio.hpp>

#include "logger.hpp"

// static std::string get_executable_dir() {
//     char result[PATH_MAX];
//     ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
//     if (count != -1) {
//         std::string path(result, count);
//         return path.substr(0, path.find_last_of('/'));
//     }
//     return "";
// }

static std::string content_type_from_path(const std::string& path) {
    if (path.size() >= 5 && path.compare(path.size()-5, 5, ".html") == 0) return "text/html";
    if (path.size() >= 3 && path.compare(path.size()-3, 3, ".js")  == 0) return "application/javascript";
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".css") == 0) return "text/css";
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".png") == 0) return "image/png";
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".jpg") == 0) return "image/jpg";
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".ico") == 0) return "image/ico";
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".svg") == 0) return "image/svg+xml";
    if (path.size() >= 5 && path.compare(path.size()-5, 5, ".json") == 0) return "application/json";
    return "text/plain";
}

static std::string read_file_into_string(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::string build_response(const std::string& content_type, const std::string& body, int status_code=200) {
    std::string reason;
    switch (status_code) {
        case 200: reason = "OK"; break;
        case 201: reason = "Created"; break;
        case 400: reason = "Bad Request"; break;
        case 401: reason = "Unauthorized"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 500: reason = "Internal Server Error"; break;
        default: reason = "Unknown";
    }
    std::string resp = "HTTP/1.1 " + std::to_string(status_code) + " " + reason + "\r\n";
    resp += "Content-Type: " + content_type + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}

static bool serve_static_file(asio::ip::tcp::socket& socket,
                              const std::string& request_path, asio::error_code& ec)
{
    if (request_path.empty()) return false;
    if (request_path.find("..") != std::string::npos) return false;
    std::string path = request_path;
    if (path[0] == '/') path = path.substr(1);
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) return false;
    std::string body = read_file_into_string(path);
    std::string content_type = content_type_from_path(request_path);
    std::string resp = build_response(content_type, body);
    asio::write(socket, asio::buffer(resp), ec);
    return true;
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
