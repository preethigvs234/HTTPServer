/**
 * @file parser.h
 * @brief HTTP/1.1 request parser – public API.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <optional>

namespace http {

/** @brief Structured representation of a parsed HTTP/1.1 request. */
struct HttpRequest {
    std::string method;
    std::string uri;
    std::string version;
    std::string query_string;
    std::string body;
    std::unordered_map<std::string, std::string> headers;

    bool is_valid() const noexcept {
        return !method.empty() && !uri.empty() && !version.empty();
    }

    /** @return Header value by name (case-insensitive), or empty string. */
    std::string header(const std::string& name) const;
};

/**
 * @brief Read and parse one HTTP/1.1 request from a connected socket.
 *
 * Returns std::nullopt on timeout, EOF, header-flood, or parse error.
 * Body is read up to min(Content-Length, 1 MiB).
 */
std::optional<HttpRequest> parse_request(int client_fd, int timeout_ms = 5000);

/** @brief RFC 3986 percent-decode. In path context '+' is a literal plus. */
std::string url_decode(const std::string& encoded);

/** @brief Lexically normalise a URI path, collapsing ".." segments. */
std::string sanitize_path(const std::string& uri);

} // namespace http
