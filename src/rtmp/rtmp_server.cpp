#include "rtmp/rtmp_server.hpp"
#include "core/log.hpp"
#include <sys/socket.h>
#include <sys/epoll.h>

namespace rtmp {

Server::Server(EpollLoop& loop) : loop_(loop) {
    listen_socket_ = Socket(socket(AF_INET, SOCK_STREAM, 0));
}

Result<void> Server::start(const std::string& addr, uint16_t port) {
    if (auto res = listen_socket_.set_reuseaddr(); res.is_err()) {
        return res;
    }
    
    if (auto res = listen_socket_.set_nonblocking(); res.is_err()) {
        return res;
    }
    
    if (auto res = listen_socket_.bind(addr, port); res.is_err()) {
        return res;
    }
    
    if (auto res = listen_socket_.listen(); res.is_err()) {
        return res;
    }
    
    Logger::info("RTMP server listening on ", addr, ":", port);
    
    return loop_.add(listen_socket_.fd(), EPOLLIN, [this](uint32_t events) {
        handle_accept();
    });
}

void Server::handle_accept() {
    while (true) {
        auto result = listen_socket_.accept();
        if (result.is_err()) {
            if (result.error() == "EAGAIN") {
                break;
            }
            Logger::error("Accept failed: ", result.error());
            break;
        }
        
        Socket client_socket = std::move(result.value());
        
        if (auto res = client_socket.set_nonblocking(); res.is_err()) {
            Logger::error("Failed to set nonblocking: ", res.error());
            continue;
        }
        
        if (auto res = client_socket.set_nodelay(); res.is_err()) {
            Logger::warn("Failed to set TCP_NODELAY: ", res.error());
        }
        
        auto session = std::make_shared<Session>();
        
        Logger::info("New RTMP connection accepted");
        
        if (connection_callback_) {
            connection_callback_(session, std::move(client_socket));
        }
    }
}

}
