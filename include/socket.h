/**
 * @file socket.h
 * @brief POSIX socket abstractions – server socket lifecycle management.
 *
 * SocketManager wraps the server-side POSIX TCP socket with RAII:
 *   socket() → setsockopt(SO_REUSEADDR) → bind() → listen() → accept() → close()
 *
 * Thread-safety
 * -------------
 * server_fd_ is std::atomic<int>.  close_socket() uses exchange() so that
 * concurrent calls from the signal handler and the accept loop are both safe:
 * exactly one caller sees a non-negative fd and closes it.
 */

#pragma once

#include <atomic>
#include <string>
#include <netinet/in.h>

namespace http {

class SocketManager {
public:
    SocketManager() = default;
    ~SocketManager();

    SocketManager(const SocketManager&)            = delete;
    SocketManager& operator=(const SocketManager&) = delete;

    /**
     * @brief Create, configure, bind, and listen on @p port.
     * @throws std::runtime_error on any POSIX failure.
     */
    void setup(int port, int backlog = 128);

    /**
     * @brief Block until a client connects; return the client fd.
     * @return Client fd (>= 0) on success, -1 on EINTR or socket closed.
     */
    int accept_client(struct sockaddr_in& client_addr);

    /** @brief Close the listening socket (idempotent, async-signal-safe). */
    void close_socket() noexcept;

    /** @return The raw server socket fd (-1 if not open). */
    int fd() const noexcept { return server_fd_.load(); }

private:
    std::atomic<int> server_fd_{-1};
};

/** @brief Convert a binary IPv4 address to a dotted-decimal string. */
std::string addr_to_string(const struct sockaddr_in& addr);

} // namespace http
