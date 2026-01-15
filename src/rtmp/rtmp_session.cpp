#include "rtmp/rtmp_session.hpp"
#include "core/log.hpp"

namespace rtmp {

Session::Session() : parser_(128), out_chunk_size_(128) {}

Result<size_t> Session::process_input(std::span<const uint8_t> data) {
    if (!handshake_.is_done()) {
        return Result<size_t>::Err("Handshake not complete");
    }
    
    Logger::debug("Parsing chunks from ", data.size(), " bytes");
    
    size_t consumed = 0;
    auto chunks_result = parser_.parse(data, consumed);
    
    if (chunks_result.is_err()) {
        return Result<size_t>::Err(chunks_result.error());
    }
    
    Logger::debug("Parsed ", chunks_result.value().size(), " complete messages");
    
    for (const auto& chunk : chunks_result.value()) {
        Message msg;
        msg.type_id = chunk.header.message_type_id;
        msg.timestamp = chunk.header.timestamp;
        msg.stream_id = chunk.header.message_stream_id;
        msg.payload = chunk.payload;
        
        Logger::debug("Message type: ", static_cast<int>(msg.type_id),
                      ", timestamp: ", msg.timestamp,
                      ", stream_id: ", msg.stream_id,
                      ", payload: ", msg.payload.size(), " bytes");
        
        if (msg.type_id == static_cast<uint8_t>(MessageType::SetChunkSize)) {
            auto parsed = SetChunkSizeMessage::parse(msg.payload);
            if (parsed) {
                parser_.set_chunk_size(parsed->chunk_size);
                Logger::debug("Set chunk size to ", parsed->chunk_size);
            }
        }
        
        if (message_callback_) {
            message_callback_(msg);
        }
    }
    
    return Result<size_t>(consumed);
}

void Session::send_message(const Message& msg) {
    ChunkHeader header;
    header.fmt = 0;
    header.chunk_stream_id = 2;
    header.timestamp = msg.timestamp;
    header.message_length = static_cast<uint32_t>(msg.payload.size());
    header.message_type_id = msg.type_id;
    header.message_stream_id = msg.stream_id;
    header.has_extended_timestamp = false;
    
    if (msg.type_id == static_cast<uint8_t>(MessageType::Audio) ||
        msg.type_id == static_cast<uint8_t>(MessageType::Video) ||
        msg.type_id == static_cast<uint8_t>(MessageType::DataAMF0)) {
        header.chunk_stream_id = msg.type_id == static_cast<uint8_t>(MessageType::Audio) ? 4 : 
                                  msg.type_id == static_cast<uint8_t>(MessageType::Video) ? 6 : 5;
    }
    
    auto chunk_data = encode_chunk(header, msg.payload, out_chunk_size_);
    write_buffer_.append(chunk_data);
}

void Session::send_set_chunk_size(uint32_t size) {
    SetChunkSizeMessage msg;
    msg.chunk_size = size;
    
    Message m;
    m.type_id = static_cast<uint8_t>(MessageType::SetChunkSize);
    m.timestamp = 0;
    m.stream_id = 0;
    m.payload = msg.encode();
    
    send_message(m);
    out_chunk_size_ = size;
}

void Session::send_window_ack_size(uint32_t size) {
    WindowAckSizeMessage msg;
    msg.window_size = size;
    
    Message m;
    m.type_id = static_cast<uint8_t>(MessageType::WindowAckSize);
    m.timestamp = 0;
    m.stream_id = 0;
    m.payload = msg.encode();
    
    send_message(m);
}

void Session::send_set_peer_bandwidth(uint32_t size, uint8_t limit_type) {
    SetPeerBandwidthMessage msg;
    msg.window_size = size;
    msg.limit_type = limit_type;
    
    Message m;
    m.type_id = static_cast<uint8_t>(MessageType::SetPeerBandwidth);
    m.timestamp = 0;
    m.stream_id = 0;
    m.payload = msg.encode();
    
    send_message(m);
}

void Session::send_command(const CommandMessage& cmd, uint32_t chunk_stream_id) {
    Message m;
    m.type_id = static_cast<uint8_t>(MessageType::CommandAMF0);
    m.timestamp = 0;
    m.stream_id = 0;
    m.payload = cmd.encode();
    
    ChunkHeader header;
    header.fmt = 0;
    header.chunk_stream_id = chunk_stream_id;
    header.timestamp = 0;
    header.message_length = static_cast<uint32_t>(m.payload.size());
    header.message_type_id = m.type_id;
    header.message_stream_id = 0;
    header.has_extended_timestamp = false;
    
    auto chunk_data = encode_chunk(header, m.payload, out_chunk_size_);
    write_buffer_.append(chunk_data);
}

std::vector<uint8_t> Session::get_outgoing_data() {
    auto data = write_buffer_.readable();
    std::vector<uint8_t> result(data.begin(), data.end());
    write_buffer_.clear();
    return result;
}

std::vector<uint8_t> Session::generate_server_handshake_response(std::span<const uint8_t> c1) {
    return handshake_.generate_s0_s1_s2(c1);
}

std::vector<uint8_t> Session::generate_client_handshake() {
    return handshake_.generate_c0_c1();
}

}
