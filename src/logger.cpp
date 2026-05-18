#include "logger.hpp"

std::shared_ptr<spdlog::logger> Logger::logger_ = []() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_level(spdlog::level::trace);
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P] [%^%l%$] [%n] %v");
    auto logger = std::make_shared<spdlog::logger>("neural", sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);
    logger->set_error_handler([](const std::string& msg) {
        std::cerr << "🚫 " << msg << std::endl;
    });
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    return logger;
}();
