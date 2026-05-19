#pragma once

#include <iostream>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class Logger {
public:
    template<typename... Args> static void Trace(const std::string& fmt, Args... args) {
        logger_->trace(fmt::runtime("👣"+fmt), args...);
    }
    template<typename... Args> static void Debug(const std::string& fmt, Args... args) {
        logger_->debug(fmt::runtime("🚧"+fmt), args...);
    }
    template<typename... Args> static void Info(const std::string& fmt, Args... args) {
        logger_->info(fmt::runtime("📝"+fmt), args...);
    }
    template<typename... Args> static void Warn(const std::string& fmt, Args... args) {
        logger_->warn(fmt::runtime("⚠️"+fmt), args...);
    }
    template<typename... Args> static void Error(const std::string& fmt, Args... args) {
        logger_->error(fmt::runtime("🚫"+fmt), args...);
    }
    template<typename... Args> static void Critical(const std::string& fmt, Args... args) {
        logger_->critical(fmt::runtime("🧨"+fmt), args...);
    }

private:
    static std::shared_ptr<spdlog::logger> logger_;
};
