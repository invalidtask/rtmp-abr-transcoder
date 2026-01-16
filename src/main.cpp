#include "core/cli.hpp"
#include "core/log.hpp"
#include "net/epoll_loop.hpp"
#include "relay/relay_manager.hpp"
#include "transcode/transcoder.hpp"
#include <csignal>
#include <iostream>

static EpollLoop* g_loop = nullptr;

void signal_handler(int signum) {
    if (g_loop) {
        Logger::info("Received signal ", signum, ", shutting down...");
        g_loop->stop();
    }
}

int main(int argc, char** argv) {
    auto config_opt = parse_args(argc, argv);
    if (!config_opt) {
        return 1;
    }
    
    auto config = *config_opt;
    
    if (config.log_level == "debug") {
        Logger::set_level(LogLevel::Debug);
    } else {
        Logger::set_level(LogLevel::Info);
    }
    
    Logger::info("Starting RTMP ABR Transcoder");
    Logger::info("Mode: ", config.mode);
    Logger::info("Listen: ", config.listen_addr);
    
    if (config.mode == "relay") {
        if (config.push_url.empty()) {
            Logger::error("Relay mode requires --push-url");
            return 1;
        }
        Logger::info("Push URL: ", config.push_url);
        Logger::info("Push template: ", config.push_template);
    } else if (config.mode == "transcode") {
        if (config.outputs.empty()) {
            Logger::error("Transcode mode requires at least one --output");
            return 1;
        }
        for (const auto& out : config.outputs) {
            Logger::info("Output: ", out.name, " (", out.width, "x", out.height, 
                         " @ ", out.video_bitrate_kbps, " kbps) -> ", out.url);
        }
    }
    
    EpollLoop loop;
    g_loop = &loop;
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    RelayPolicy policy;
    policy.max_pending_bytes = config.max_pending_bytes;
    policy.pusher_down_disconnect_ms = config.pusher_down_disconnect_ms;
    
    relay::RelayManager manager(loop, policy);
    
    // Setup transcoder if in transcode mode
    std::shared_ptr<transcode::Transcoder> transcoder;
    if (config.mode == "transcode") {
        transcoder = std::make_shared<transcode::Transcoder>(loop);
        
        for (const auto& out : config.outputs) {
            transcode::OutputConfig out_config;
            out_config.name = out.name;
            out_config.width = out.width;
            out_config.height = out.height;
            out_config.video_bitrate_kbps = out.video_bitrate_kbps;
            out_config.audio_bitrate_kbps = out.audio_bitrate_kbps;
            out_config.rtmp_url = out.url;
            
            if (!transcoder->add_output(out_config)) {
                Logger::error("Failed to add output: ", out.name);
                return 1;
            }
        }
        
        manager.set_transcoder(transcoder);
    } else if (config.mode == "relay") {
        manager.set_push_url(config.push_url);
        manager.set_push_template(config.push_template);
    }
    
    auto addr_result = Socket::parse_address(config.listen_addr);
    if (addr_result.is_err()) {
        Logger::error("Invalid listen address: ", addr_result.error());
        return 1;
    }
    
    auto [addr, port] = addr_result.value();
    
    auto start_result = manager.start_server(addr, port);
    if (start_result.is_err()) {
        Logger::error("Failed to start server: ", start_result.error());
        return 1;
    }
    
    Logger::info("Server started successfully");
    
    loop.start();
    while (loop.is_running()) {
        auto result = loop.run_once(100);
        if (result.is_err()) {
            Logger::error("Event loop error: ", result.error());
            break;
        }
    }
    
    Logger::info("Shutdown complete");
    
    return 0;
}
