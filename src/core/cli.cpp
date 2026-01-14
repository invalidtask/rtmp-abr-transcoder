#include "core/cli.hpp"
#include <iostream>
#include <cstring>

void print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --listen <addr:port>              Listen address (default: 0.0.0.0:1935)\n"
              << "  --push-url <url>                  Destination RTMP URL (required for relay mode)\n"
              << "  --push-template <template>        Stream path template (default: {app}/{stream})\n"
              << "  --max-pending-bytes <bytes>       Max pending bytes buffer (default: 2097152)\n"
              << "  --pusher-down-disconnect-ms <ms>  Pusher disconnect timeout (default: 2000)\n"
              << "  --log-level <level>               Log level: debug|info (default: info)\n"
              << "  --mode <mode>                     Mode: relay|sink (default: relay)\n"
              << "  --help                            Show this help\n";
}

std::optional<Config> parse_args(int argc, char** argv) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return std::nullopt;
        }
        else if (arg == "--listen" && i + 1 < argc) {
            config.listen_addr = argv[++i];
        }
        else if (arg == "--push-url" && i + 1 < argc) {
            config.push_url = argv[++i];
        }
        else if (arg == "--push-template" && i + 1 < argc) {
            config.push_template = argv[++i];
        }
        else if (arg == "--max-pending-bytes" && i + 1 < argc) {
            config.max_pending_bytes = std::stoull(argv[++i]);
        }
        else if (arg == "--pusher-down-disconnect-ms" && i + 1 < argc) {
            config.pusher_down_disconnect_ms = std::stoull(argv[++i]);
        }
        else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        }
        else if (arg == "--mode" && i + 1 < argc) {
            config.mode = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_help(argv[0]);
            return std::nullopt;
        }
    }
    
    return config;
}
