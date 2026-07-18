/**
 * @file router.h
 * @brief URL router and static-file server – public API.
 */

#pragma once

#include "parser.h"
#include <string>
#include <unordered_map>

namespace http {

/** @brief A fully-built HTTP response ready to serialise and send. */
struct HttpResponse {
    int         status_code   = 200;
    std::string status_text   = "OK";
    std::string content_type  = "text/html; charset=utf-8";
    std::string body;
    bool        is_head_only  = false;
    long long   content_length = -1; ///< -1 → derive from body.size()

    /** @return Serialised status line + response headers (no body). */
    std::string headers_to_string() const;
};

/**
 * @brief Maps request URIs to static files under document_root.
 *
 * All public methods are thread-safe: document_root_ is set once at
 * construction and never mutated.
 */
class Router {
public:
    explicit Router(std::string document_root);

    /**
     * @brief Route a parsed request and return an HttpResponse.
     * Returns 200, 403, 404, 405, or 500 as appropriate.
     */
    HttpResponse route(const HttpRequest& request) const;

    /**
     * @brief Write an HttpResponse to @p client_fd with partial-send retry.
     * @return true on success, false if the write failed (e.g. EPIPE).
     */
    static bool send_response(int client_fd, const HttpResponse& response);

    /** @brief Build a styled HTML error page for the given status code. */
    static std::string error_page(int code, const std::string& text);

    /** @brief Return the MIME type for a file extension (e.g. ".html"). */
    static std::string mime_type(const std::string& extension);

private:
    std::string document_root_;

    HttpResponse serve_file(const std::string& fs_path, bool head_only) const;
    static HttpResponse make_error(int code, const std::string& text);
};

} // namespace http
