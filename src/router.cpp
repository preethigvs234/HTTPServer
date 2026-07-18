// Static-file router and HTTP response serialisation.
//
// Traversal guard uses two layers:
//   1. sanitize_path() in the parser lexically removes ".." segments.
//   2. route() resolves the canonical path and checks it is rooted at
//      document_root_ with a path-separator boundary check, blocking both
//      raw and URL-encoded traversal attempts.
//
// weakly_canonical() is called with the error_code overload throughout so
// that filesystem errors produce 404/500 responses rather than exceptions
// propagating into worker threads.

#include "router.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace http {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// MIME table
// ---------------------------------------------------------------------------

std::string Router::mime_type(const std::string& ext) {
    // Static map is initialised once on first call; subsequent calls are
    // read-only and require no locking under C++11 and later.
    static const std::unordered_map<std::string, std::string> table = {
        { ".html",  "text/html; charset=utf-8"  },
        { ".htm",   "text/html; charset=utf-8"  },
        { ".css",   "text/css; charset=utf-8"   },
        { ".js",    "application/javascript"    },
        { ".json",  "application/json"          },
        { ".png",   "image/png"                 },
        { ".jpg",   "image/jpeg"                },
        { ".jpeg",  "image/jpeg"                },
        { ".gif",   "image/gif"                 },
        { ".svg",   "image/svg+xml"             },
        { ".ico",   "image/x-icon"              },
        { ".webp",  "image/webp"                },
        { ".pdf",   "application/pdf"           },
        { ".txt",   "text/plain; charset=utf-8" },
        { ".xml",   "application/xml"           },
        { ".woff",  "font/woff"                 },
        { ".woff2", "font/woff2"                },
        { ".ttf",   "font/ttf"                  },
        { ".mp4",   "video/mp4"                 },
        { ".webm",  "video/webm"                },
    };

    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const auto it = table.find(lower);
    return (it != table.end()) ? it->second : "application/octet-stream";
}

// ---------------------------------------------------------------------------
// Error page
// ---------------------------------------------------------------------------

std::string Router::error_page(int code, const std::string& text) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n"
        << "<html lang=\"en\"><head>\n"
        << "  <meta charset=\"UTF-8\">\n"
        << "  <title>" << code << " " << text << "</title>\n"
        << "  <style>\n"
        << "    body{font-family:'Segoe UI',sans-serif;background:#0f0f1a;"
           "color:#e0e0e0;display:flex;flex-direction:column;align-items:center;"
           "justify-content:center;height:100vh;margin:0}\n"
        << "    h1{font-size:6rem;margin:0;color:#7c6aff}\n"
        << "    p{font-size:1.4rem}\n"
        << "    a{color:#a78bfa;text-decoration:none}\n"
        << "  </style>\n"
        << "</head><body>\n"
        << "  <h1>" << code << "</h1>\n"
        << "  <p>" << text << "</p>\n"
        << "  <a href=\"/\">\u2190 Back to Home</a>\n"
        << "</body></html>\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// HttpResponse
// ---------------------------------------------------------------------------

std::string HttpResponse::headers_to_string() const {
    const long long len = (content_length >= 0)
                              ? content_length
                              : static_cast<long long>(body.size());
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
        << "Content-Type: "            << content_type << "\r\n"
        << "Content-Length: "          << len          << "\r\n"
        << "Connection: close\r\n"
        << "Server: HPHTTPServer/1.0\r\n"
        << "X-Content-Type-Options: nosniff\r\n"
        << "\r\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

Router::Router(std::string document_root)
    : document_root_(std::move(document_root)) {}

HttpResponse Router::make_error(int code, const std::string& text) {
    HttpResponse resp;
    resp.status_code  = code;
    resp.status_text  = text;
    resp.content_type = "text/html; charset=utf-8";
    resp.body         = error_page(code, text);
    return resp;
}

HttpResponse Router::serve_file(const std::string& fs_path,
                                bool               head_only) const
{
    std::ifstream file(fs_path, std::ios::binary);
    if (!file.is_open())
        return make_error(500, "Internal Server Error");

    std::ostringstream oss;
    oss << file.rdbuf();

    HttpResponse resp;
    resp.status_code  = 200;
    resp.status_text  = "OK";
    resp.content_type = mime_type(fs::path(fs_path).extension().string());
    resp.body         = oss.str();
    resp.is_head_only = head_only;
    return resp;
}

HttpResponse Router::route(const HttpRequest& request) const {
    if (request.method != "GET" && request.method != "HEAD")
        return make_error(405, "Method Not Allowed");

    const bool  head_only = (request.method == "HEAD");
    std::string uri       = request.uri;
    if (uri == "/") uri = "/index.html";

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::current_path() / document_root_, ec);
    if (ec) return make_error(500, "Internal Server Error");

    // uri is guaranteed to start with '/' by sanitize_path(), so substr(1) is safe.
    const fs::path full = fs::weakly_canonical(root / uri.substr(1), ec);
    if (ec) return make_error(404, "Not Found");

    // Path-boundary check: /srv/pub must not match /srv/public.
    const std::string root_str = root.string();
    const std::string full_str = full.string();
    const bool under_root =
        full_str.size() >= root_str.size() &&
        full_str.compare(0, root_str.size(), root_str) == 0 &&
        (full_str.size() == root_str.size() || full_str[root_str.size()] == '/');

    if (!under_root) return make_error(403, "Forbidden");

    if (!fs::is_regular_file(full, ec) || ec)
        return make_error(404, "Not Found");

    struct stat st{};
    if (::stat(full_str.c_str(), &st) != 0)
        return make_error(500, "Internal Server Error");
    if (!(st.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)))
        return make_error(403, "Forbidden");

    return serve_file(full_str, head_only);
}

// ---------------------------------------------------------------------------
// send_response
// ---------------------------------------------------------------------------

bool Router::send_response(int client_fd, const HttpResponse& response) {
    const std::string headers = response.headers_to_string();

    auto send_all = [client_fd](const char* buf, std::size_t len) -> bool {
        while (len > 0) {
            const ssize_t n = ::send(client_fd, buf, len, MSG_NOSIGNAL);
            if (n <= 0) return false;
            buf += n;
            len -= static_cast<std::size_t>(n);
        }
        return true;
    };

    if (!send_all(headers.data(), headers.size())) return false;

    if (!response.is_head_only && !response.body.empty())
        return send_all(response.body.data(), response.body.size());

    return true;
}

} // namespace http
