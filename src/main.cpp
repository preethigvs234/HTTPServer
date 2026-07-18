#include "server.h"
#include "config.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    const std::string config_path = (argc > 1) ? argv[1] : "config.json";

    http::Config cfg;
    try {
        cfg = http::Config::from_file(config_path);
        std::cout << "[main] Loaded config: " << config_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[main] Warning: " << e.what() << " – using defaults.\n";
        cfg = http::Config::defaults();
    }

    try {
        http::Server server(cfg);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[main] Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
