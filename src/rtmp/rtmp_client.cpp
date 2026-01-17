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
    Logger::debug("Client::connect to ", host, ":", port);
    
    socket_ = Socket(socket(AF_INET, SOCK_STREAM, 0));
    
    if (auto res = socket_.set_nonblocking(); res.is_err()) {
        Logger::error("Failed to set socket non-blocking: ", res.error());
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
        Logger::error("Socket connect failed: ", connect_result.error());
        return connect_result;
    }
    Logger::debug("Socket connect initiated (non-blocking)");
    
    auto handshake_data = session_->generate_client_handshake();
    session_->get_outgoing_data();
    
    Buffer temp_buf;
    temp_buf.append(handshake_data);
    
    Logger::debug("Adding pusher socket fd=", socket_.fd(), " to epoll");
    
    auto add_result = loop_.add(socket_.fd(), EPOLLOUT | EPOLLIN, [this, handshake_data](uint32_t events) mutable {
        Logger::debug("Pusher epoll callback fired, events=", events, 
                      " EPOLLIN=", (events & EPOLLIN) ? "yes" : "no",
                      " EPOLLOUT=", (events & EPOLLOUT) ? "yes" : "no");
        if (events & EPOLLOUT) {
            if (connecting_) {
                Logger::debug("EPOLLOUT received, sending handshake");
                auto write_result = socket_.write(handshake_data.data(), handshake_data.size());
                if (write_result.is_ok() && write_result.value() == handshake_data.size()) {
                    Logger::debug("Handshake C0C1 sent, ", write_result.value(), " bytes");
                    connecting_ = false;
                    loop_.modify(socket_.fd(), EPOLLIN);
                } else if (write_result.is_ok()) {
                    Logger::warn("Partial handshake write: ", write_result.value(), " of ", handshake_data.size(), " bytes");
                } else {
                    Logger::error("Handshake write failed: ", write_result.error());
                }
            } else {
                handle_writable();
            }
        }
        if (events & EPOLLIN) {
            Logger::debug("EPOLLIN received");
            handle_readable();
        }
        if (events & (EPOLLERR | EPOLLHUP)) {
            Logger::error("EPOLLERR or EPOLLHUP received");
            cleanup();
        }
    });
    
    if (add_result.is_err()) {
        Logger::error("Failed to add pusher socket to epoll: ", add_result.error());
        return add_result;
    }
    
    Logger::debug("Pusher socket added to epoll successfully");
    return Result<void>::Ok();
}

void Client::disconnect() {
    if (socket_.is_valid()) {
        cleanup();
    }
}

void Client::send_message(const Message& msg) {
    if (session_) {
        session_->send_message(msg);
        if (socket_.is_valid() && (session_->has_outgoing_data() || !write_buffer_.empty())) {
            loop_.modify(socket_.fd(), EPOLLIN | EPOLLOUT);
        }
    }
}

void Client::send_command(const CommandMessage& cmd, uint32_t chunk_stream_id) {
    if (session_) {
        session_->send_command(cmd, chunk_stream_id);
        if (socket_.is_valid() && (session_->has_outgoing_data() || !write_buffer_.empty())) {
            loop_.modify(socket_.fd(), EPOLLIN | EPOLLOUT);
        }
    }
}

void Client::flush() {
    if (!socket_.is_valid()) {
        return;
    }
    
    // Move any session outgoing data into our write buffer
    if (session_ && session_->has_outgoing_data()) {
        auto data = session_->get_outgoing_data();
        write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    }
    
    if (write_buffer_.empty()) {
        return;
    }
    
    Logger::debug("Flushing ", write_buffer_.size(), " bytes to pusher socket");
    auto result = socket_.write(write_buffer_.data(), write_buffer_.size());
    
    if (result.is_err()) {
        Logger::error("Flush failed: ", result.error());
        // Keep data in write_buffer_ for retry
        return;
    }
    
    size_t written = result.value();
    Logger::debug("Wrote ", written, " bytes");
    
    if (written < write_buffer_.size()) {
        Logger::warn("Partial flush occurred, ", written, " of ", write_buffer_.size(), " bytes written");
        // Erase only the written bytes, preserve the rest
        write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + written);
        // Register for EPOLLOUT to continue writing when socket is writable
        loop_.modify(socket_.fd(), EPOLLIN | EPOLLOUT);
    } else {
        // All data written successfully
        write_buffer_.clear();
    }
}

void Client::handle_writable() {
    Logger::debug("Client::handle_writable called");
    
    // Move any session outgoing data into our write buffer
    if (session_ && session_->has_outgoing_data()) {
        auto data = session_->get_outgoing_data();
        write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    }
    
    if (write_buffer_.empty()) {
        Logger::debug("No outgoing data, switching to EPOLLIN only");
        loop_.modify(socket_.fd(), EPOLLIN);
        return;
    }
    
    Logger::debug("Writing ", write_buffer_.size(), " bytes to socket");
    auto write_result = socket_.write(write_buffer_.data(), write_buffer_.size());
    if (write_result.is_err()) {
        Logger::error("Write error: ", write_result.error());
        cleanup();
        return;
    }
    
    size_t written = write_result.value();
    if (written < write_buffer_.size()) {
        Logger::warn("Partial write occurred, ", written, " of ", write_buffer_.size(), " bytes written");
        // Erase only the written bytes, preserve the rest
        write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + written);
        // Keep EPOLLOUT registered to continue writing
    } else {
        // All data written successfully
        write_buffer_.clear();
        // Switch back to EPOLLIN only
        loop_.modify(socket_.fd(), EPOLLIN);
    }
}

void Client::handle_readable() {
    Logger::debug("Client::handle_readable");
    uint8_t buffer[8192];
    
    auto read_result = socket_.read(buffer, sizeof(buffer));
    if (read_result.is_err()) {
        Logger::error("Read error: ", read_result.error());
        cleanup();
        return;
    }
    
    if (read_result.value() == 0) {
        Logger::debug("Connection closed by peer");
        cleanup();
        return;
    }
    
    size_t bytes_read = read_result.value();
    Logger::debug("Read ", bytes_read, " bytes from socket");
    
    // Append new data to read buffer
    read_buffer_.insert(read_buffer_.end(), buffer, buffer + bytes_read);
    Logger::debug("Read buffer now contains ", read_buffer_.size(), " bytes");
    
    if (!connected_) {
        // Per RTMP spec: S0+S1+S2 = 1 + 1536 + 1536 = 3073 bytes
        // Must wait until we have all 3073 bytes
        Logger::debug("Processing server handshake, buffer has ", read_buffer_.size(), " bytes");
        
        auto handshake_result = session_->handshake().process_server_handshake(
            std::span<const uint8_t>(read_buffer_.data(), read_buffer_.size())
        );
        
        if (handshake_result.is_ok()) {
            size_t consumed = handshake_result.value();
            Logger::debug("Client handshake complete! Consumed ", consumed, " bytes");
            
            // Remove consumed handshake bytes from buffer
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + consumed);
            
            connected_ = true;
            
            // Per RTMP spec: After handshake, client must send C2 (echo of S1)
            // generate_c2() creates 1536 bytes echoing S1
            auto c2_data = session_->handshake().generate_c2(
                std::span<const uint8_t>(session_->handshake().s1_data())
            );
            
            Logger::debug("Sending C2, ", c2_data.size(), " bytes");
            auto write_result = socket_.write(c2_data.data(), c2_data.size());
            if (write_result.is_err()) {
                Logger::error("Failed to send C2: ", write_result.error());
                cleanup();
                return;
            }
            Logger::debug("C2 sent successfully");
            
            if (connected_callback_) {
                Logger::debug("Calling connected_callback");
                connected_callback_();
            }
            
            // Process any remaining data after handshake as RTMP messages
            if (!read_buffer_.empty()) {
                Logger::debug("Processing ", read_buffer_.size(), " bytes after handshake");
                process_buffered_data();
            }
        } else {
            Logger::debug("Handshake not yet complete, need more data. Have ", 
                         read_buffer_.size(), " bytes, need 3073");
            // Data stays in read_buffer_ for next read
        }
    } else {
        // Handshake complete - process as RTMP chunks
        process_buffered_data();
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

void Client::process_buffered_data() {
    if (read_buffer_.empty()) {
        Logger::debug("process_buffered_data: buffer empty");
        return;
    }
    
    Logger::debug("process_buffered_data: processing ", read_buffer_.size(), " bytes");
    
    auto process_result = session_->process_input(
        std::span<const uint8_t>(read_buffer_.data(), read_buffer_.size())
    );
    
    if (process_result.is_ok()) {
        size_t consumed = process_result.value();
        Logger::debug("process_buffered_data: consumed ", consumed, " bytes");
        if (consumed > 0) {
            if (consumed <= read_buffer_.size()) {
                read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + consumed);
                Logger::debug("process_buffered_data: buffer now has ", read_buffer_.size(), " bytes");
            } else {
                Logger::error("Consumed more bytes than buffer contains! consumed=", consumed, 
                            ", buffer_size=", read_buffer_.size());
                read_buffer_.clear();
            }
        }
    } else {
        Logger::error("Process input error: ", process_result.error());
    }
}

}
