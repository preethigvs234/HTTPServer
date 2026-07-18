// Thread-safe asynchronous logger.
//
// Shutdown is race-free: running_ is set to false *inside* the mutex, and
// flush_loop() checks it *inside* the mutex after the queue swap. This closes
// the window where a producer push between swap() and the exit check would
// lose messages on shutdown.

#include "logger.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <stdexcept>

namespace http {

Logger::Logger(const std::string& log_file) {
    const std::filesystem::path p(log_file);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    file_.open(log_file, std::ios::app);
    if (!file_.is_open())
        throw std::runtime_error("Logger: cannot open log file: " + log_file);

    io_thread_ = std::thread(&Logger::flush_loop, this);
}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (io_thread_.joinable()) io_thread_.join();
}

void Logger::log(LogLevel level, const std::string& message) {
    std::ostringstream oss;
    oss << '[' << timestamp() << "] [" << level_str(level) << "] " << message;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(oss.str());
    }
    cv_.notify_one();
}

void Logger::log_request(const std::string& client_ip,
                         const std::string& method,
                         const std::string& uri,
                         int                status,
                         long long          latency_ms)
{
    std::ostringstream oss;
    oss << '[' << timestamp() << "] [ACCESS] "
        << client_ip  << ' '
        << method     << ' '
        << uri        << ' '
        << status     << ' '
        << latency_ms << "ms";

    const std::string entry = oss.str();
    {
        // stdout write under the same lock so access lines and the startup
        // banner (printed by Server::run) are never interleaved.
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(entry);
        std::cout << entry << '\n';
    }
    cv_.notify_one();
}

void Logger::flush_loop() {
    while (true) {
        std::queue<std::string> local;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            std::swap(local, queue_);

            // Check exit condition inside the lock: guarantees no producer
            // push is missed between the swap and this check.
            if (!running_ && local.empty()) return;
        }
        while (!local.empty()) {
            file_ << local.front() << '\n';
            local.pop();
        }
        file_.flush();
    }
}

std::string Logger::timestamp() {
    using namespace std::chrono;
    const auto now    = system_clock::now();
    const auto time_t = system_clock::to_time_t(now);
    const auto ms     = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_info{};
    localtime_r(&time_t, &tm_info);

    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

const char* Logger::level_str(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default:              return "?????";
    }
}

} // namespace http
