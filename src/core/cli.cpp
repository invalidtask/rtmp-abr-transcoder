#include "core/cli.hpp"
#include <iostream>
#include <cstring>
#include <sstream>

static bool parse_output_spec(const std::string& spec, OutputSpec& out) {
    // Format: name:widthxheight@video_bitrate,url
    // Example: 720p:1280x720@2500,rtmp://server/app/stream_720p
    
    size_t colon_pos = spec.find(':');
    if (colon_pos == std::string::npos) {
        return false;
    }
    
    out.name = spec.substr(0, colon_pos);
    
    size_t x_pos = spec.find('x', colon_pos);
    if (x_pos == std::string::npos) {
        return false;
    }
    
    size_t at_pos = spec.find('@', x_pos);
    if (at_pos == std::string::npos) {
        return false;
    }
    
    size_t comma_pos = spec.find(',', at_pos);
    if (comma_pos == std::string::npos) {
        return false;
    }
    
    try {
        out.width = std::stoi(spec.substr(colon_pos + 1, x_pos - colon_pos - 1));
        out.height = std::stoi(spec.substr(x_pos + 1, at_pos - x_pos - 1));
        out.video_bitrate_kbps = std::stoi(spec.substr(at_pos + 1, comma_pos - at_pos - 1));
        out.url = spec.substr(comma_pos + 1);
        
        // Set default audio bitrate based on video bitrate
        if (out.video_bitrate_kbps >= 2000) {
            out.audio_bitrate_kbps = 128;
        } else {
            out.audio_bitrate_kbps = 64;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --listen <addr:port>              Listen address (default: 0.0.0.0:1935)\n"
              << "  --push-url <url>                  Destination RTMP URL (required for relay mode)\n"
              << "  --push-template <template>        Stream path template (default: {app}/{stream})\n"
              << "  --max-pending-bytes <bytes>       Max pending bytes buffer (default: 2097152)\n"
              << "  --pusher-down-disconnect-ms <ms>  Pusher disconnect timeout (default: 2000)\n"
              << "  --log-level <level>               Log level: debug|info (default: info)\n"
              << "  --mode <mode>                     Mode: relay|sink|transcode (default: relay)\n"
              << "  --output <spec>                   Output for transcode mode (format: name:WxH@kbps,url)\n"
              << "  --help                            Show this help\n"
              << "\nTranscode mode example:\n"
              << "  " << program_name << " --listen 0.0.0.0:1935 --mode transcode \\\n"
              << "    --output \"720p:1280x720@2500,rtmp://server/live/stream_720p\" \\\n"
              << "    --output \"360p:640x360@1000,rtmp://server/live/stream_360p\"\n";
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
        else if (arg == "--output" && i + 1 < argc) {
            OutputSpec spec;
            if (parse_output_spec(argv[++i], spec)) {
                config.outputs.push_back(spec);
            } else {
                std::cerr << "Invalid output specification: " << argv[i] << std::endl;
                std::cerr << "Expected format: name:WxH@kbps,url" << std::endl;
                return std::nullopt;
            }
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_help(argv[0]);
            return std::nullopt;
        }
    }
    
    return config;
}
