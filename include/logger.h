#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <atomic>

namespace http {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

// Workers push pre-formatted strings onto a queue; a dedicated I/O thread
// drains it. Workers hold the mutex only for a queue push, never for a
// disk write, keeping the hot path nearly lock-free.
class Logger {
public:
    explicit Logger(const std::string& log_file);
    ~Logger();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const std::string& message);

    void log_request(const std::string& client_ip,
                     const std::string& method,
                     const std::string& uri,
                     int                status,
                     long long          latency_ms);

private:
    void flush_loop();
    static std::string timestamp();
    static const char* level_str(LogLevel level) noexcept;

    std::ofstream           file_;
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::queue<std::string> queue_;
    std::thread             io_thread_;
    std::atomic<bool>       running_{true};
};

} // namespace http
