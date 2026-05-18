#include "worker_client.hpp"

void WorkerClient::runChild(int child_fd) {
    IpcSocket sock(child_fd);
    asio::io_context io;
    HttpClientSrv server(io, 8081);
    server.start();
    bool stop = false;
    while (!stop) {
        std::string prompt;
        double threshold;
        std::promise<std::string> answer_promise;
        if (!server.dequeue_ask(prompt, threshold, answer_promise)) break;
        try {
            std::vector<uint8_t> payload(sizeof(double));
            double thr = (threshold > 0) ? threshold : -1.0;
            std::memcpy(payload.data(), &thr, sizeof(double));
            payload.insert(payload.end(), prompt.begin(), prompt.end());
            sock.send(Message{0x04, payload});
            Message ans = sock.recv();
            std::string answer(ans.payload.begin(), ans.payload.end());
            answer_promise.set_value(answer);
        } catch (const std::exception& e) {
            answer_promise.set_exception(std::make_exception_ptr(e));
            stop = true;
            break;
        }
    }
    server.stop();
}

WorkerClient::WorkerClient(int child_fd, int admin_fd, int client_fd) {
    pid_ = fork();
    if (pid_ == 0) {
        close(admin_fd);
        close(client_fd);
        runChild(child_fd);
        exit(0);
    } else if (pid_ > 0) {
        close(child_fd);
    } else {
        throw std::runtime_error("fork failed");
    }
}

WorkerClient::~WorkerClient() { if (pid_ > 0) kill(pid_, SIGTERM); }

pid_t WorkerClient::getPid() const { return pid_; }
