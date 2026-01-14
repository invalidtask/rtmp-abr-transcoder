#include "test_framework.hpp"
#include "rtmp/rtmp_chunks.hpp"
#include "core/bytes.hpp"

TEST_CASE(chunk_parse_fmt0_small) {
    rtmp::ChunkParser parser(128);
    
    std::vector<uint8_t> data;
    data.push_back(0x03);
    
    uint8_t timestamp[3] = {0, 0, 100};
    data.insert(data.end(), timestamp, timestamp + 3);
    
    uint8_t length[3] = {0, 0, 10};
    data.insert(data.end(), length, length + 3);
    
    data.push_back(0x14);
    
    uint8_t stream_id[4] = {0, 0, 0, 0};
    data.insert(data.end(), stream_id, stream_id + 4);
    
    for (int i = 0; i < 10; ++i) {
        data.push_back(static_cast<uint8_t>(i));
    }
    
    size_t consumed = 0;
    auto result = parser.parse(data, consumed);
    
    REQUIRE(result.is_ok());
    REQUIRE_EQUAL(consumed, data.size());
    
    auto& chunks = result.value();
    REQUIRE_EQUAL(chunks.size(), 1);
    REQUIRE_EQUAL(chunks[0].header.chunk_stream_id, 3);
    REQUIRE_EQUAL(chunks[0].header.timestamp, 100);
    REQUIRE_EQUAL(chunks[0].header.message_length, 10);
    REQUIRE_EQUAL(chunks[0].header.message_type_id, 0x14);
    REQUIRE_EQUAL(chunks[0].payload.size(), 10);
}

TEST_CASE(chunk_parse_multi_chunk) {
    rtmp::ChunkParser parser(64);
    
    std::vector<uint8_t> data;
    data.push_back(0x02);
    
    uint8_t timestamp[3] = {0, 0, 50};
    data.insert(data.end(), timestamp, timestamp + 3);
    
    uint8_t length[3] = {0, 0, 150};
    data.insert(data.end(), length, length + 3);
    
    data.push_back(0x08);
    
    uint8_t stream_id[4] = {1, 0, 0, 0};
    data.insert(data.end(), stream_id, stream_id + 4);
    
    for (int i = 0; i < 64; ++i) {
        data.push_back(static_cast<uint8_t>(i));
    }
    
    data.push_back(0xC2);
    for (int i = 64; i < 128; ++i) {
        data.push_back(static_cast<uint8_t>(i));
    }
    
    data.push_back(0xC2);
    for (int i = 128; i < 150; ++i) {
        data.push_back(static_cast<uint8_t>(i));
    }
    
    size_t consumed = 0;
    auto result = parser.parse(data, consumed);
    
    REQUIRE(result.is_ok());
    REQUIRE_EQUAL(consumed, data.size());
    
    auto& chunks = result.value();
    REQUIRE_EQUAL(chunks.size(), 1);
    REQUIRE_EQUAL(chunks[0].header.chunk_stream_id, 2);
    REQUIRE_EQUAL(chunks[0].header.timestamp, 50);
    REQUIRE_EQUAL(chunks[0].payload.size(), 150);
}

TEST_CASE(chunk_encode_small) {
    rtmp::ChunkHeader header;
    header.chunk_stream_id = 3;
    header.timestamp = 100;
    header.message_length = 10;
    header.message_type_id = 0x14;
    header.message_stream_id = 0;
    header.has_extended_timestamp = false;
    
    std::vector<uint8_t> payload;
    for (int i = 0; i < 10; ++i) {
        payload.push_back(static_cast<uint8_t>(i));
    }
    
    auto encoded = rtmp::encode_chunk(header, payload, 128);
    
    REQUIRE_EQUAL(encoded[0], 0x03);
    REQUIRE_EQUAL(bytes::read_u24_be(encoded.data() + 1), 100);
    REQUIRE_EQUAL(bytes::read_u24_be(encoded.data() + 4), 10);
    REQUIRE_EQUAL(encoded[7], 0x14);
    REQUIRE_EQUAL(encoded.size(), 12 + 10);
}

TEST_CASE(chunk_encode_decode_roundtrip) {
    rtmp::ChunkHeader header;
    header.chunk_stream_id = 5;
    header.timestamp = 12345;
    header.message_length = 200;
    header.message_type_id = 0x09;
    header.message_stream_id = 1;
    header.has_extended_timestamp = false;
    
    std::vector<uint8_t> payload(200);
    for (size_t i = 0; i < 200; ++i) {
        payload[i] = static_cast<uint8_t>(i % 256);
    }
    
    auto encoded = rtmp::encode_chunk(header, payload, 128);
    
    rtmp::ChunkParser parser(128);
    size_t consumed = 0;
    auto result = parser.parse(encoded, consumed);
    
    REQUIRE(result.is_ok());
    REQUIRE_EQUAL(consumed, encoded.size());
    
    auto& chunks = result.value();
    REQUIRE_EQUAL(chunks.size(), 1);
    REQUIRE_EQUAL(chunks[0].header.chunk_stream_id, header.chunk_stream_id);
    REQUIRE_EQUAL(chunks[0].header.timestamp, header.timestamp);
    REQUIRE_EQUAL(chunks[0].header.message_type_id, header.message_type_id);
    REQUIRE_EQUAL(chunks[0].payload.size(), payload.size());
}
