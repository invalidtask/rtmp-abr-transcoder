#include "core/cli.hpp"
#include "core/log.hpp"
#include "net/epoll_loop.hpp"
#include "relay/relay_manager.hpp"
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
    }
    
    EpollLoop loop;
    g_loop = &loop;
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    RelayPolicy policy;
    policy.max_pending_bytes = config.max_pending_bytes;
    policy.pusher_down_disconnect_ms = config.pusher_down_disconnect_ms;
    
    relay::RelayManager manager(loop, policy);
    
    if (config.mode == "relay") {
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
