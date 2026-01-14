#pragma once
#include "rtmp_session.hpp"
#include "net/socket.hpp"
#include "net/epoll_loop.hpp"
#include "core/result.hpp"
#include <memory>
#include <functional>

namespace rtmp {

class Client {
public:
    using ConnectedCallback = std::function<void()>;
    using MessageCallback = std::function<void(const Message&)>;
    using DisconnectedCallback = std::function<void()>;
    
    explicit Client(EpollLoop& loop);
    ~Client();
    
    Result<void> connect(const std::string& host, uint16_t port);
    void disconnect();
    
    void send_message(const Message& msg);
    void send_command(const CommandMessage& cmd, uint32_t chunk_stream_id = 3);
    
    void set_connected_callback(ConnectedCallback cb) { connected_callback_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void set_disconnected_callback(DisconnectedCallback cb) { disconnected_callback_ = std::move(cb); }
    
    bool is_connected() const { return connected_; }
    std::shared_ptr<Session> session() { return session_; }
    
private:
    void handle_writable();
    void handle_readable();
    void cleanup();
    
    EpollLoop& loop_;
    Socket socket_;
    std::shared_ptr<Session> session_;
    bool connected_ = false;
    bool connecting_ = false;
    
    ConnectedCallback connected_callback_;
    MessageCallback message_callback_;
    DisconnectedCallback disconnected_callback_;
};

}
