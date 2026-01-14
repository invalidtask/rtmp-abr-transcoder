#pragma once
#include "rtmp_session.hpp"
#include "net/socket.hpp"
#include "net/epoll_loop.hpp"
#include <memory>
#include <functional>

namespace rtmp {

class Server {
public:
    using ConnectionCallback = std::function<void(std::shared_ptr<Session>, Socket)>;
    
    explicit Server(EpollLoop& loop);
    
    Result<void> start(const std::string& addr, uint16_t port);
    void set_connection_callback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    
private:
    void handle_accept();
    
    EpollLoop& loop_;
    Socket listen_socket_;
    ConnectionCallback connection_callback_;
};

}
