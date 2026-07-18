// Minimal JSON value extractor – no external dependencies.
// Handles the flat key/value format used in config.json.

#include "config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace http {

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Finds "key" : <value> and returns the trimmed value without surrounding quotes.
std::string extract_value(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return {};

    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return {};
    ++pos;

    std::string value;
    bool in_string = false;
    for (; pos < json.size(); ++pos) {
        char c = json[pos];
        if (c == '"') {
            if (!in_string) { in_string = true; continue; }
            else            { break; }
        }
        if (!in_string && (c == ',' || c == '}' || c == '\n')) break;
        value += c;
    }
    return trim(value);
}

} // namespace

Config Config::from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string json = ss.str();

    Config cfg;

    const std::string port_val = extract_value(json, "port");
    if (!port_val.empty()) {
        const int p = std::stoi(port_val);
        if (p < 1 || p > 65535)
            throw std::runtime_error("Invalid port in config: " + port_val);
        cfg.port = p;
    }

    const std::string workers_val = extract_value(json, "workers");
    if (!workers_val.empty()) {
        const int w = std::stoi(workers_val);
        if (w < 1 || w > 1024)
            throw std::runtime_error("Invalid workers in config: " + workers_val);
        cfg.workers = w;
    }

    const std::string dr = extract_value(json, "document_root");
    if (!dr.empty()) cfg.document_root = dr;

    const std::string lf = extract_value(json, "log_file");
    if (!lf.empty()) cfg.log_file = lf;

    const std::string bl = extract_value(json, "backlog");
    if (!bl.empty()) cfg.backlog = std::stoi(bl);

    const std::string rt = extract_value(json, "recv_timeout_ms");
    if (!rt.empty()) cfg.recv_timeout_ms = std::stoi(rt);

    return cfg;
}

} // namespace http
