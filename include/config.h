#pragma once

#include <string>

namespace http {

// Loaded once at startup; treated as read-only afterwards, so thread-safe
// to share across workers without any locking.
struct Config {
    int         port            = 8080;
    int         workers         = 8;
    std::string document_root   = "public";
    std::string log_file        = "logs/server.log";
    int         backlog         = 128;
    int         recv_timeout_ms = 5000;

    // Throws std::runtime_error if the file cannot be opened or a value is invalid.
    static Config from_file(const std::string& path);
    static Config defaults() noexcept { return Config{}; }
};

} // namespace http
