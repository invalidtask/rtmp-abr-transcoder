#pragma once
#include <string>
#include <optional>
#include <vector>
#include <cstdint>

struct OutputSpec {
    std::string name;
    int width;
    int height;
    int video_bitrate_kbps;
    int audio_bitrate_kbps;
    std::string url;
};

struct Config {
    std::string listen_addr = "0.0.0.0:1935";
    std::string push_url;
    std::string push_template = "{app}/{stream}";
    size_t max_pending_bytes = 2 * 1024 * 1024;
    uint64_t pusher_down_disconnect_ms = 2000;
    std::string log_level = "info";
    std::string mode = "relay";
    std::vector<OutputSpec> outputs;
};

std::optional<Config> parse_args(int argc, char** argv);
void print_help(const char* program_name);
