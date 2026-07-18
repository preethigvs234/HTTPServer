// HTTP server orchestrator.
//
// Concurrency:
//   main thread  — socket_.setup() → [accept loop] → pool_.stop()
//   worker N     — parse_request → router_.route → send_response → close(fd)
//   signal path  — stop() → socket_.close_socket()
//
// stop() only sets running_ and calls close_socket(). close_socket() is
// idempotent and async-signal-safe (atomic exchange on the fd). Closing the
// listening socket causes accept() to return an error, which breaks the loop.
//
// All stats_ fields are std::atomic so worker threads update metrics without
// any mutex on the hot path.

#include "server.h"
#include "parser.h"

#include <csignal>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <unistd.h>

namespace http {

static Server* g_server_instance = nullptr;

static void signal_handler(int /*signum*/) {
    if (g_server_instance) {
        // write() is async-signal-safe; std::strlen is not.
        const char msg[] = "\n[Signal] Graceful shutdown initiated...\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        g_server_instance->stop();
    }
}

Server::Server(Config cfg)
    : config_(std::move(cfg))
    , start_time_(std::chrono::steady_clock::now())
{
    // Logger and router are constructed before the thread pool so that no
    // worker threads start before the listening socket is ready.
    logger_ = std::make_unique<Logger>(config_.log_file);
    router_ = std::make_unique<Router>(config_.document_root);
}

Server::~Server() {
    if (running_.load()) stop();
}

void Server::run() {
    g_server_instance = this;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN); // write to closed socket → EPIPE, not SIGPIPE

    socket_.setup(config_.port, config_.backlog);
    pool_    = std::make_unique<ThreadPool>(static_cast<std::size_t>(config_.workers));
    running_ = true;

    logger_->log(LogLevel::INFO,
                 "Server started on port " + std::to_string(config_.port) +
                 " with " + std::to_string(config_.workers) + " workers");

    std::cout << "\n╔══════════════════════════════════════════╗\n"
              << "║   High-Performance Multi-threaded HTTP   ║\n"
              << "║   Server v1.0  –  port "
              << std::setw(5) << config_.port << "              ║\n"
              << "╚══════════════════════════════════════════╝\n"
              << "  Workers  : " << config_.workers       << "\n"
              << "  DocRoot  : " << config_.document_root << "\n"
              << "  Log file : " << config_.log_file      << "\n"
              << "  Press Ctrl+C to stop\n\n";

    while (running_.load()) {
        struct sockaddr_in client_addr{};
        const int client_fd = socket_.accept_client(client_addr);
        if (client_fd < 0) continue;

        const std::string client_ip = addr_to_string(client_addr);

        // Track peak concurrency with a lock-free CAS loop. Two workers can
        // observe the same prev_max simultaneously, so a single fetch_add is
        // not sufficient.
        const uint64_t cur = stats_.active_connections.fetch_add(1) + 1;
        uint64_t prev_max  = stats_.max_concurrent.load();
        while (cur > prev_max &&
               !stats_.max_concurrent.compare_exchange_weak(prev_max, cur)) {}

        pool_->submit([this, client_fd, client_ip]() {
            handle_client(client_fd, client_ip);
        });
    }

    logger_->log(LogLevel::INFO, "Accept loop exited – draining task queue...");
    pool_->stop();
    socket_.close_socket();
    print_stats();

    g_server_instance = nullptr;
}

void Server::stop() noexcept {
    running_.store(false);
    socket_.close_socket(); // unblocks accept() immediately
}

void Server::handle_client(int client_fd, const std::string& client_ip) {
    using Clock = std::chrono::steady_clock;
    const auto t_start = Clock::now();

    auto opt_request = parse_request(client_fd, config_.recv_timeout_ms);

    std::string  method = "-";
    std::string  uri    = "-";
    HttpResponse response;

    if (!opt_request) {
        response.status_code  = 400;
        response.status_text  = "Bad Request";
        response.content_type = "text/html; charset=utf-8";
        response.body         = Router::error_page(400, "Bad Request");
    } else {
        method   = opt_request->method;
        uri      = opt_request->uri;
        response = router_->route(*opt_request);
        if (response.status_code == 500) ++stats_.errors_500;
    }

    Router::send_response(client_fd, response);
    ::close(client_fd);

    const long long lat_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 Clock::now() - t_start).count();

    ++stats_.total_requests;
    stats_.total_latency_ms += static_cast<uint64_t>(lat_ms);
    stats_.active_connections.fetch_sub(1);

    logger_->log_request(client_ip, method, uri, response.status_code, lat_ms);
}

void Server::print_stats() const {
    const double elapsed_s = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_time_).count();

    const uint64_t reqs   = stats_.total_requests.load();
    const double   rps    = (elapsed_s > 0.0) ? static_cast<double>(reqs) / elapsed_s : 0.0;
    const double   avg_ms = stats_.avg_latency_ms();
    const uint64_t peak   = stats_.max_concurrent.load();
    const uint64_t e500   = stats_.errors_500.load();

    std::cout << "\n──────────────────────────────────────────\n"
              << "  Performance Summary\n"
              << "──────────────────────────────────────────\n"
              << "  Uptime            : " << std::fixed << std::setprecision(2)
                                          << elapsed_s << " s\n"
              << "  Total Requests    : " << reqs      << "\n"
              << "  Requests / second : " << std::setprecision(1) << rps    << "\n"
              << "  Avg Latency       : " << std::setprecision(2) << avg_ms << " ms\n"
              << "  Peak Concurrent   : " << peak      << "\n"
              << "  500 Errors        : " << e500      << "\n"
              << "──────────────────────────────────────────\n\n";

    logger_->log(LogLevel::INFO,
                 "Shutdown complete. Requests=" + std::to_string(reqs) +
                 " RPS=" + std::to_string(static_cast<int>(rps)) +
                 " AvgLatency=" + std::to_string(avg_ms) + "ms");
}

} // namespace http
