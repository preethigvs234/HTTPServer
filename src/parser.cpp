// HTTP/1.1 request parser.
//
// Security caps enforced here (before the router sees anything):
//   - MAX_HEADER_SIZE (8 KiB): prevents header-flood memory exhaustion.
//   - MAX_BODY_SIZE   (1 MiB): prevents Content-Length abuse.
//   - URI is percent-decoded then lexically normalised (removes ".." segments)
//     before being handed to the router, which runs a second traversal check.
//   - '+' in the path is a literal plus per RFC 3986 §3.3; only the query
//     string decodes '+' as a space (application/x-www-form-urlencoded).

#include "parser.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace http {

static constexpr std::size_t MAX_HEADER_SIZE = 8192;    // 8 KiB
static constexpr std::size_t MAX_BODY_SIZE   = 1048576; // 1 MiB
static constexpr std::size_t READ_CHUNK      = 4096;

std::string HttpRequest::header(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const auto it = headers.find(lower);
    return (it != headers.end()) ? it->second : std::string{};
}

std::string url_decode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const char hi = encoded[i + 1];
            const char lo = encoded[i + 2];
            if (std::isxdigit(static_cast<unsigned char>(hi)) &&
                std::isxdigit(static_cast<unsigned char>(lo))) {
                char hex[3] = { hi, lo, '\0' };
                result += static_cast<char>(std::strtol(hex, nullptr, 16));
                i += 2;
            } else {
                result += encoded[i];
            }
        } else {
            result += encoded[i];
        }
    }
    return result;
}

std::string sanitize_path(const std::string& uri) {
    std::filesystem::path p = std::filesystem::path(uri).lexically_normal();
    std::string s = p.string();
    if (s.empty() || s[0] != '/') s = "/" + s;
    return s;
}

static bool wait_readable(int fd, int timeout_ms) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return ::select(fd + 1, &rset, nullptr, nullptr, &tv) > 0;
}

std::optional<HttpRequest> parse_request(int client_fd, int timeout_ms) {
    std::string raw;
    raw.reserve(READ_CHUNK);

    char chunk[READ_CHUNK];
    std::size_t header_end = std::string::npos;

    while (true) {
        if (!wait_readable(client_fd, timeout_ms)) return std::nullopt;
        const ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return std::nullopt;
        raw.append(chunk, static_cast<std::size_t>(n));
        header_end = raw.find("\r\n\r\n");
        if (header_end != std::string::npos) break;
        if (raw.size() > MAX_HEADER_SIZE) return std::nullopt;
    }

    std::istringstream stream(raw);
    std::string request_line;
    if (!std::getline(stream, request_line)) return std::nullopt;

    if (!request_line.empty() && request_line.back() == '\r')
        request_line.pop_back();

    HttpRequest req;
    std::string raw_uri;
    {
        std::istringstream rl(request_line);
        if (!(rl >> req.method >> raw_uri >> req.version))
            return std::nullopt;
    }

    const auto q_pos = raw_uri.find('?');
    if (q_pos != std::string::npos) {
        req.query_string = raw_uri.substr(q_pos + 1);
        raw_uri          = raw_uri.substr(0, q_pos);
    }

    req.uri = sanitize_path(url_decode(raw_uri));

    static const char* const known_methods[] =
        { "GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "PATCH" };
    bool valid = false;
    for (const char* m : known_methods)
        if (req.method == m) { valid = true; break; }
    if (!valid) return std::nullopt;

    std::string header_line;
    while (std::getline(stream, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r')
            header_line.pop_back();
        if (header_line.empty()) break;

        const auto colon = header_line.find(':');
        if (colon == std::string::npos) continue;

        std::string name  = header_line.substr(0, colon);
        std::string value = header_line.substr(colon + 1);

        // Trim leading OWS per RFC 7230 §3.2.6
        const auto trim_pos = value.find_first_not_of(" \t");
        value = (trim_pos != std::string::npos) ? value.substr(trim_pos) : std::string{};

        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        req.headers[std::move(name)] = std::move(value);
    }

    const auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        std::size_t body_len = 0;
        try {
            body_len = std::min(static_cast<std::size_t>(std::stoul(it->second)),
                                MAX_BODY_SIZE);
        } catch (...) {}

        if (body_len > 0) {
            const std::size_t body_start = header_end + 4;
            std::string already = (body_start < raw.size())
                                      ? raw.substr(body_start)
                                      : std::string{};

            while (already.size() < body_len) {
                if (!wait_readable(client_fd, timeout_ms)) break;
                const std::size_t to_read = std::min(sizeof(chunk),
                                                     body_len - already.size());
                const ssize_t n = ::recv(client_fd, chunk, to_read, 0);
                if (n <= 0) break;
                already.append(chunk, static_cast<std::size_t>(n));
            }
            req.body = already.substr(0, body_len);
        }
    }

    return req;
}

} // namespace http
