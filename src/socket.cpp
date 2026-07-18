/**
 * @file socket.cpp
 * @brief POSIX TCP listening socket – RAII wrapper around the server socket lifecycle.
 *
 * Socket lifecycle:
 *   socket() → setsockopt(SO_REUSEADDR) → bind() → listen() → [accept loop]
 *
 * Thread-safety note
 * ------------------
 * server_fd_ is stored as std::atomic<int> so that close_socket() (called
 * from the signal-handler path via Server::stop()) can race-free invalidate
 * the fd while accept_client() is blocked on the accept() syscall.  The OS
 * guarantees that closing a fd from another thread unblocks any blocking call
 * on that fd with EBADF/EINVAL, which we map to a clean -1 return.
 */

#include "socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace http {

SocketManager::~SocketManager() {
    close_socket();
}

void SocketManager::setup(int port, int backlog) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(
            std::string("socket() failed: ") + std::strerror(errno));
    }

    // Allow fast port reuse after server restart (avoids TIME_WAIT stall)
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(fd);
        throw std::runtime_error(
            std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
    }

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error(
            std::string("bind() failed on port ") + std::to_string(port) +
            ": " + std::strerror(errno));
    }

    if (::listen(fd, backlog) < 0) {
        ::close(fd);
        throw std::runtime_error(
            std::string("listen() failed: ") + std::strerror(errno));
    }

    server_fd_.store(fd);
}

int SocketManager::accept_client(struct sockaddr_in& client_addr) {
    int fd = server_fd_.load();
    if (fd < 0) return -1;

    socklen_t addr_len = sizeof(client_addr);
    int client_fd = ::accept(fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &addr_len);

    if (client_fd < 0) {
        // EINTR: interrupted by a signal – caller checks running_ flag.
        // All other errors (EBADF after close_socket, EINVAL, etc.) are also
        // non-fatal during shutdown.
        return -1;
    }
    return client_fd;
}

void SocketManager::close_socket() noexcept {
    // Exchange atomically; only the thread that gets a non-negative value closes.
    int fd = server_fd_.exchange(-1);
    if (fd >= 0) {
        ::close(fd);
    }
}

std::string addr_to_string(const struct sockaddr_in& addr) {
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace http
