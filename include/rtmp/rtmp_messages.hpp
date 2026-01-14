#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "amf0.hpp"

namespace rtmp {

enum class MessageType : uint8_t {
    SetChunkSize = 1,
    Acknowledgement = 3,
    WindowAckSize = 5,
    SetPeerBandwidth = 6,
    Audio = 8,
    Video = 9,
    DataAMF0 = 18,
    CommandAMF0 = 20
};

struct Message {
    uint8_t type_id;
    uint32_t timestamp;
    uint32_t stream_id;
    std::vector<uint8_t> payload;
};

struct SetChunkSizeMessage {
    uint32_t chunk_size;
    
    static std::optional<SetChunkSizeMessage> parse(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> encode() const;
};

struct WindowAckSizeMessage {
    uint32_t window_size;
    
    static std::optional<WindowAckSizeMessage> parse(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> encode() const;
};

struct SetPeerBandwidthMessage {
    uint32_t window_size;
    uint8_t limit_type;
    
    static std::optional<SetPeerBandwidthMessage> parse(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> encode() const;
};

struct CommandMessage {
    std::string name;
    double transaction_id;
    std::vector<amf0::Value> arguments;
    
    static std::optional<CommandMessage> parse(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> encode() const;
};

}
