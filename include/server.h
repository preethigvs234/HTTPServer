/**
 * @file server.h
 * @brief Top-level HTTP server orchestrator.
 */

#pragma once

#include "config.h"
#include "socket.h"
#include "router.h"
#include "logger.h"
#include "threadpool.h"

#include <atomic>
#include <memory>
#include <chrono>

namespace http {

/** @brief Lock-free performance counters updated by worker threads. */
struct ServerStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_latency_ms{0};
    std::atomic<uint64_t> max_concurrent{0};
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> errors_500{0};

    double avg_latency_ms() const noexcept {
        const uint64_t n = total_requests.load();
        return n ? static_cast<double>(total_latency_ms.load()) / n : 0.0;
    }
};

/**
 * @brief Coordinates all server subsystems for the lifetime of the process.
 *
 * run() blocks until stop() is called (typically via SIGINT/SIGTERM).
 */
class Server {
public:
    explicit Server(Config cfg);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    /** @brief Bind, listen, and enter the accept loop. Blocks until stop(). */
    void run();

    /** @brief Signal the accept loop to exit. Async-signal-safe. */
    void stop() noexcept;

    const ServerStats& stats() const noexcept { return stats_; }

private:
    void handle_client(int client_fd, const std::string& client_ip);
    void print_stats() const;

    Config                      config_;
    SocketManager               socket_;
    std::unique_ptr<Logger>     logger_;
    std::unique_ptr<Router>     router_;
    std::unique_ptr<ThreadPool> pool_;
    ServerStats                 stats_;
    std::atomic<bool>           running_{false};
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace http
