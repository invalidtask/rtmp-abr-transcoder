#include "rtmp/rtmp_client.hpp"
#include "core/log.hpp"
#include <sys/socket.h>
#include <sys/epoll.h>

namespace rtmp {

Client::Client(EpollLoop& loop) : loop_(loop) {}

Client::~Client() {
    disconnect();
}

Result<void> Client::connect(const std::string& host, uint16_t port) {
    socket_ = Socket(socket(AF_INET, SOCK_STREAM, 0));
    
    if (auto res = socket_.set_nonblocking(); res.is_err()) {
        return res;
    }
    
    if (auto res = socket_.set_nodelay(); res.is_err()) {
        Logger::warn("Failed to set TCP_NODELAY: ", res.error());
    }
    
    session_ = std::make_shared<Session>();
    
    session_->set_message_callback([this](const Message& msg) {
        if (message_callback_) {
            message_callback_(msg);
        }
    });
    
    connecting_ = true;
    
    auto connect_result = socket_.connect(host, port);
    if (connect_result.is_err()) {
        return connect_result;
    }
    
    auto handshake_data = session_->generate_client_handshake();
    session_->get_outgoing_data();
    
    Buffer temp_buf;
    temp_buf.append(handshake_data);
    
    return loop_.add(socket_.fd(), EPOLLOUT | EPOLLIN, [this, handshake_data](uint32_t events) mutable {
        if (events & EPOLLOUT) {
            if (connecting_) {
                auto write_result = socket_.write(handshake_data.data(), handshake_data.size());
                if (write_result.is_ok() && write_result.value() == handshake_data.size()) {
                    connecting_ = false;
                    loop_.modify(socket_.fd(), EPOLLIN);
                }
            } else {
                handle_writable();
            }
        }
        if (events & EPOLLIN) {
            handle_readable();
        }
        if (events & (EPOLLERR | EPOLLHUP)) {
            cleanup();
        }
    });
}

void Client::disconnect() {
    if (socket_.is_valid()) {
        cleanup();
    }
}

void Client::send_message(const Message& msg) {
    if (session_) {
        session_->send_message(msg);
        if (socket_.is_valid() && session_->has_outgoing_data()) {
            loop_.modify(socket_.fd(), EPOLLIN | EPOLLOUT);
        }
    }
}

void Client::send_command(const CommandMessage& cmd, uint32_t chunk_stream_id) {
    if (session_) {
        session_->send_command(cmd, chunk_stream_id);
        if (socket_.is_valid() && session_->has_outgoing_data()) {
            loop_.modify(socket_.fd(), EPOLLIN | EPOLLOUT);
        }
    }
}

void Client::handle_writable() {
    if (!session_ || !session_->has_outgoing_data()) {
        loop_.modify(socket_.fd(), EPOLLIN);
        return;
    }
    
    auto data = session_->get_outgoing_data();
    if (data.empty()) {
        loop_.modify(socket_.fd(), EPOLLIN);
        return;
    }
    
    auto write_result = socket_.write(data.data(), data.size());
    if (write_result.is_err()) {
        Logger::error("Write error: ", write_result.error());
        cleanup();
        return;
    }
    
    if (write_result.value() < data.size()) {
        Buffer temp;
        temp.append(data.data() + write_result.value(), data.size() - write_result.value());
    }
}

void Client::handle_readable() {
    uint8_t buffer[8192];
    
    auto read_result = socket_.read(buffer, sizeof(buffer));
    if (read_result.is_err()) {
        Logger::error("Read error: ", read_result.error());
        cleanup();
        return;
    }
    
    if (read_result.value() == 0) {
        cleanup();
        return;
    }
    
    size_t bytes_read = read_result.value();
    
    if (!session_->handshake_done()) {
        auto handshake_result = session_->handshake().process_server_handshake(
            std::span<const uint8_t>(buffer, bytes_read)
        );
        
        if (handshake_result.is_ok()) {
            connected_ = true;
            if (connected_callback_) {
                connected_callback_();
            }
            
            size_t consumed = handshake_result.value();
            if (consumed < bytes_read) {
                auto process_result = session_->process_input(
                    std::span<const uint8_t>(buffer + consumed, bytes_read - consumed)
                );
                if (process_result.is_err()) {
                    Logger::error("Process input error: ", process_result.error());
                }
            }
        }
    } else {
        auto process_result = session_->process_input(
            std::span<const uint8_t>(buffer, bytes_read)
        );
        
        if (process_result.is_err()) {
            Logger::error("Process input error: ", process_result.error());
        }
    }
}

void Client::cleanup() {
    if (socket_.is_valid()) {
        loop_.remove(socket_.fd());
        socket_.close();
    }
    
    connected_ = false;
    connecting_ = false;
    
    if (disconnected_callback_) {
        disconnected_callback_();
    }
}

}
