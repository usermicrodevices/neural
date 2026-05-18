#include <exception>
#include <string>

#include "logger.hpp"
#include "worker_master.hpp"

int main(int argc, char* argv[]) {
    std::string args_str;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) args_str += " ";
        args_str += argv[i];
    }
    Logger::Info("Start server: {}", args_str);
    std::unique_ptr<WorkerMaster> instance;
    try {
        instance = std::make_unique<WorkerMaster>(/*args*/);
    } catch (const std::exception& err) {
        Logger::Error("Fatal init master: {}", err.what());
        return 1;
    }
    try {
        Logger::Info("Run master process...");
        instance->run();
    } catch (const std::exception& err) {
        Logger::Error("Fatal master.run(): {}", err.what());
        return 1;
    }
    return 0;
}
