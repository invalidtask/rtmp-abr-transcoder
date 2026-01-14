#include "rtmp/rtmp_messages.hpp"
#include "core/bytes.hpp"

namespace rtmp {

std::optional<SetChunkSizeMessage> SetChunkSizeMessage::parse(const std::vector<uint8_t>& payload) {
    if (payload.size() < 4) {
        return std::nullopt;
    }
    
    SetChunkSizeMessage msg;
    msg.chunk_size = bytes::read_u32_be(payload.data()) & 0x7FFFFFFF;
    return msg;
}

std::vector<uint8_t> SetChunkSizeMessage::encode() const {
    std::vector<uint8_t> payload(4);
    bytes::write_u32_be(payload.data(), chunk_size & 0x7FFFFFFF);
    return payload;
}

std::optional<WindowAckSizeMessage> WindowAckSizeMessage::parse(const std::vector<uint8_t>& payload) {
    if (payload.size() < 4) {
        return std::nullopt;
    }
    
    WindowAckSizeMessage msg;
    msg.window_size = bytes::read_u32_be(payload.data());
    return msg;
}

std::vector<uint8_t> WindowAckSizeMessage::encode() const {
    std::vector<uint8_t> payload(4);
    bytes::write_u32_be(payload.data(), window_size);
    return payload;
}

std::optional<SetPeerBandwidthMessage> SetPeerBandwidthMessage::parse(const std::vector<uint8_t>& payload) {
    if (payload.size() < 5) {
        return std::nullopt;
    }
    
    SetPeerBandwidthMessage msg;
    msg.window_size = bytes::read_u32_be(payload.data());
    msg.limit_type = payload[4];
    return msg;
}

std::vector<uint8_t> SetPeerBandwidthMessage::encode() const {
    std::vector<uint8_t> payload(5);
    bytes::write_u32_be(payload.data(), window_size);
    payload[4] = limit_type;
    return payload;
}

std::optional<CommandMessage> CommandMessage::parse(const std::vector<uint8_t>& payload) {
    auto values_result = amf0::decode_array(payload);
    if (values_result.is_err() || values_result.value().empty()) {
        return std::nullopt;
    }
    
    auto& values = values_result.value();
    
    if (!values[0].is_string()) {
        return std::nullopt;
    }
    
    CommandMessage cmd;
    cmd.name = values[0].as_string();
    
    if (values.size() > 1 && values[1].is_number()) {
        cmd.transaction_id = values[1].as_number();
    }
    
    for (size_t i = 2; i < values.size(); ++i) {
        cmd.arguments.push_back(values[i]);
    }
    
    return cmd;
}

std::vector<uint8_t> CommandMessage::encode() const {
    std::vector<amf0::Value> values;
    values.push_back(amf0::Value::String(name));
    values.push_back(amf0::Value::Number(transaction_id));
    
    for (const auto& arg : arguments) {
        values.push_back(arg);
    }
    
    auto result = amf0::encode_array(values);
    return result.is_ok() ? result.value() : std::vector<uint8_t>();
}

}
