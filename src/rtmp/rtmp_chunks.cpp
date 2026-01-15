#include "rtmp/rtmp_chunks.hpp"
#include "core/bytes.hpp"
#include "core/log.hpp"
#include <algorithm>

namespace rtmp {

ChunkParser::ChunkParser(uint32_t chunk_size) : chunk_size_(chunk_size) {}

void ChunkParser::set_chunk_size(uint32_t size) {
    chunk_size_ = size;
}

Result<std::pair<ChunkHeader, size_t>> ChunkParser::parse_header(
    std::span<const uint8_t> data,
    uint32_t csid
) {
    if (data.empty()) {
        return Result<std::pair<ChunkHeader, size_t>>::Err("Empty data");
    }
    
    uint8_t first_byte = data[0];
    uint8_t fmt = (first_byte >> 6) & 0x03;
    uint32_t chunk_stream_id = first_byte & 0x3F;
    size_t offset = 1;
    
    if (chunk_stream_id == 0) {
        if (data.size() < 2) {
            return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for csid");
        }
        chunk_stream_id = 64 + data[1];
        offset = 2;
    } else if (chunk_stream_id == 1) {
        if (data.size() < 3) {
            return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for csid");
        }
        chunk_stream_id = 64 + data[1] + (data[2] * 256);
        offset = 3;
    }
    
    ChunkHeader header;
    header.fmt = fmt;
    header.chunk_stream_id = chunk_stream_id;
    
    auto& state = streams_[chunk_stream_id];
    
    if (fmt == 0) {
        if (data.size() < offset + 11) {
            return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for fmt 0");
        }
        
        header.timestamp = bytes::read_u24_be(data.data() + offset);
        offset += 3;
        header.message_length = bytes::read_u24_be(data.data() + offset);
        offset += 3;
        header.message_type_id = data[offset++];
        header.message_stream_id = bytes::read_u32_le(data.data() + offset);
        offset += 4;
        
        if (header.timestamp == 0xFFFFFF) {
            if (data.size() < offset + 4) {
                return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for extended timestamp");
            }
            header.timestamp = bytes::read_u32_be(data.data() + offset);
            header.has_extended_timestamp = true;
            offset += 4;
        }
        
        Logger::debug("Chunk header: fmt=", static_cast<int>(fmt),
                      ", csid=", chunk_stream_id,
                      ", msg_type=", static_cast<int>(header.message_type_id),
                      ", msg_len=", header.message_length);
        
        state.prev_header = header;
    }
    else if (fmt == 1) {
        if (data.size() < offset + 7) {
            return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for fmt 1");
        }
        
        uint32_t timestamp_delta = bytes::read_u24_be(data.data() + offset);
        offset += 3;
        header.message_length = bytes::read_u24_be(data.data() + offset);
        offset += 3;
        header.message_type_id = data[offset++];
        
        header.message_stream_id = state.prev_header.message_stream_id;
        
        if (timestamp_delta == 0xFFFFFF) {
            if (data.size() < offset + 4) {
                return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for extended timestamp");
            }
            timestamp_delta = bytes::read_u32_be(data.data() + offset);
            header.has_extended_timestamp = true;
            offset += 4;
        }
        
        header.timestamp = state.prev_header.timestamp + timestamp_delta;
        state.prev_header = header;
    }
    else if (fmt == 2) {
        if (data.size() < offset + 3) {
            return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for fmt 2");
        }
        
        uint32_t timestamp_delta = bytes::read_u24_be(data.data() + offset);
        offset += 3;
        
        header.message_length = state.prev_header.message_length;
        header.message_type_id = state.prev_header.message_type_id;
        header.message_stream_id = state.prev_header.message_stream_id;
        
        if (timestamp_delta == 0xFFFFFF) {
            if (data.size() < offset + 4) {
                return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for extended timestamp");
            }
            timestamp_delta = bytes::read_u32_be(data.data() + offset);
            header.has_extended_timestamp = true;
            offset += 4;
        }
        
        header.timestamp = state.prev_header.timestamp + timestamp_delta;
        state.prev_header.timestamp = header.timestamp;
    }
    else {
        header = state.prev_header;
        
        if (header.has_extended_timestamp) {
            if (data.size() < offset + 4) {
                return Result<std::pair<ChunkHeader, size_t>>::Err("Not enough data for extended timestamp");
            }
            header.timestamp = bytes::read_u32_be(data.data() + offset);
            offset += 4;
        }
    }
    
    return Result<std::pair<ChunkHeader, size_t>>(std::make_pair(header, offset));
}

Result<std::vector<Chunk>> ChunkParser::parse(std::span<const uint8_t> data, size_t& consumed) {
    std::vector<Chunk> chunks;
    size_t offset = 0;
    
    while (offset < data.size()) {
        auto header_result = parse_header(data.subspan(offset), 0);
        if (header_result.is_err()) {
            if (chunks.empty() && offset == 0) {
                consumed = 0;
                return Result<std::vector<Chunk>>::Err(header_result.error());
            }
            break;
        }
        
        auto [header, header_size] = header_result.value();
        offset += header_size;
        
        auto& state = streams_[header.chunk_stream_id];
        
        if (state.bytes_received == 0) {
            state.partial_message.clear();
            state.partial_message.reserve(header.message_length);
        }
        
        uint32_t bytes_to_read = std::min(
            chunk_size_,
            header.message_length - state.bytes_received
        );
        
        if (offset + bytes_to_read > data.size()) {
            break;
        }
        
        state.partial_message.insert(
            state.partial_message.end(),
            data.data() + offset,
            data.data() + offset + bytes_to_read
        );
        offset += bytes_to_read;
        state.bytes_received += bytes_to_read;
        
        if (state.bytes_received >= header.message_length) {
            Chunk chunk;
            chunk.header = header;
            chunk.payload = std::move(state.partial_message);
            
            // Check for SetChunkSize message and update chunk size immediately
            if (header.message_type_id == 1 && chunk.payload.size() == 4) {
                uint32_t new_size = bytes::read_u32_be(chunk.payload.data());
                if (new_size > 0 && new_size <= 0xFFFFFF) {
                    chunk_size_ = new_size;
                    Logger::debug("Chunk parser updated chunk size to ", new_size);
                }
            }
            
            chunks.push_back(std::move(chunk));
            
            state.bytes_received = 0;
            state.partial_message.clear();
        }
    }
    
    consumed = offset;
    return Result<std::vector<Chunk>>(std::move(chunks));
}

std::vector<uint8_t> encode_chunk(
    const ChunkHeader& header,
    std::span<const uint8_t> payload,
    uint32_t chunk_size
) {
    std::vector<uint8_t> output;
    
    size_t payload_offset = 0;
    bool first_chunk = true;
    
    while (payload_offset < payload.size()) {
        uint8_t fmt = first_chunk ? 0 : 3;
        uint8_t first_byte = (fmt << 6);
        
        uint32_t csid = header.chunk_stream_id;
        if (csid < 64) {
            first_byte |= csid;
            output.push_back(first_byte);
        } else if (csid < 320) {
            output.push_back(first_byte);
            output.push_back(static_cast<uint8_t>(csid - 64));
        } else {
            first_byte |= 1;
            output.push_back(first_byte);
            uint32_t csid_offset = csid - 64;
            output.push_back(static_cast<uint8_t>(csid_offset & 0xFF));
            output.push_back(static_cast<uint8_t>((csid_offset >> 8) & 0xFF));
        }
        
        if (first_chunk) {
            uint8_t timestamp_bytes[3];
            uint32_t timestamp = header.timestamp < 0xFFFFFF ? header.timestamp : 0xFFFFFF;
            bytes::write_u24_be(timestamp_bytes, timestamp);
            output.insert(output.end(), timestamp_bytes, timestamp_bytes + 3);
            
            uint8_t length_bytes[3];
            bytes::write_u24_be(length_bytes, header.message_length);
            output.insert(output.end(), length_bytes, length_bytes + 3);
            
            output.push_back(header.message_type_id);
            
            uint8_t stream_id_bytes[4];
            bytes::write_u32_le(stream_id_bytes, header.message_stream_id);
            output.insert(output.end(), stream_id_bytes, stream_id_bytes + 4);
            
            if (header.timestamp >= 0xFFFFFF) {
                uint8_t ext_timestamp_bytes[4];
                bytes::write_u32_be(ext_timestamp_bytes, header.timestamp);
                output.insert(output.end(), ext_timestamp_bytes, ext_timestamp_bytes + 4);
            }
        }
        
        size_t bytes_to_write = std::min(
            static_cast<size_t>(chunk_size),
            payload.size() - payload_offset
        );
        
        output.insert(
            output.end(),
            payload.data() + payload_offset,
            payload.data() + payload_offset + bytes_to_write
        );
        
        payload_offset += bytes_to_write;
        first_chunk = false;
    }
    
    return output;
}

}
