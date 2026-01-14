#pragma once
#include "rtmp_handshake.hpp"
#include "rtmp_chunks.hpp"
#include "rtmp_messages.hpp"
#include "net/buffer.hpp"
#include "core/result.hpp"
#include <functional>
#include <memory>
#include <string>

namespace rtmp {

class Session {
public:
    using MessageCallback = std::function<void(const Message&)>;
    using CloseCallback = std::function<void()>;
    
    Session();
    
    void set_message_callback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void set_close_callback(CloseCallback cb) { close_callback_ = std::move(cb); }
    
    Result<size_t> process_input(std::span<const uint8_t> data);
    
    void send_message(const Message& msg);
    void send_set_chunk_size(uint32_t size);
    void send_window_ack_size(uint32_t size);
    void send_set_peer_bandwidth(uint32_t size, uint8_t limit_type);
    void send_command(const CommandMessage& cmd, uint32_t chunk_stream_id = 3);
    
    std::vector<uint8_t> get_outgoing_data();
    bool has_outgoing_data() const { return !write_buffer_.empty(); }
    
    bool handshake_done() const { return handshake_.is_done(); }
    
    std::vector<uint8_t> generate_server_handshake_response(std::span<const uint8_t> c1);
    std::vector<uint8_t> generate_client_handshake();
    
    const std::string& app_name() const { return app_name_; }
    const std::string& stream_name() const { return stream_name_; }
    void set_stream_info(const std::string& app, const std::string& stream) {
        app_name_ = app;
        stream_name_ = stream;
    }
    
    Handshake& handshake() { return handshake_; }
    const Handshake& handshake() const { return handshake_; }
    
private:
    Handshake handshake_;
    ChunkParser parser_;
    Buffer write_buffer_;
    MessageCallback message_callback_;
    CloseCallback close_callback_;
    
    uint32_t out_chunk_size_ = 128;
    std::string app_name_;
    std::string stream_name_;
};

}
