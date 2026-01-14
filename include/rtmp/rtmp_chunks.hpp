#pragma once
#include "core/result.hpp"
#include <cstdint>
#include <vector>
#include <span>
#include <map>

namespace rtmp {

struct ChunkHeader {
    uint8_t fmt;
    uint32_t chunk_stream_id;
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type_id;
    uint32_t message_stream_id;
    bool has_extended_timestamp;
    
    ChunkHeader() 
        : fmt(0), chunk_stream_id(0), timestamp(0), message_length(0),
          message_type_id(0), message_stream_id(0), has_extended_timestamp(false) {}
};

struct Chunk {
    ChunkHeader header;
    std::vector<uint8_t> payload;
};

class ChunkParser {
public:
    explicit ChunkParser(uint32_t chunk_size = 128);
    
    void set_chunk_size(uint32_t size);
    uint32_t get_chunk_size() const { return chunk_size_; }
    
    Result<std::vector<Chunk>> parse(std::span<const uint8_t> data, size_t& consumed);
    
private:
    struct StreamState {
        ChunkHeader prev_header;
        std::vector<uint8_t> partial_message;
        uint32_t bytes_received = 0;
    };
    
    Result<std::pair<ChunkHeader, size_t>> parse_header(
        std::span<const uint8_t> data,
        uint32_t csid
    );
    
    uint32_t chunk_size_;
    std::map<uint32_t, StreamState> streams_;
};

std::vector<uint8_t> encode_chunk(
    const ChunkHeader& header,
    std::span<const uint8_t> payload,
    uint32_t chunk_size = 128
);

}
