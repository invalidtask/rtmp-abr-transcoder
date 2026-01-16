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

TEST_CASE(chunk_parse_set_chunk_size_then_large_message) {
    // This test reproduces the bug where SetChunkSize is followed by a message
    // larger than the old chunk size but smaller than the new chunk size.
    // The parser should immediately apply the new chunk size.
    
    rtmp::ChunkParser parser(128);
    std::vector<uint8_t> data;
    
    // First chunk: SetChunkSize message (type 1)
    // CSID = 2, timestamp = 0, length = 4, type = 1, stream_id = 0
    data.push_back(0x02);  // fmt=0, csid=2
    
    uint8_t timestamp1[3] = {0, 0, 0};
    data.insert(data.end(), timestamp1, timestamp1 + 3);
    
    uint8_t length1[3] = {0, 0, 4};  // 4 bytes payload
    data.insert(data.end(), length1, length1 + 3);
    
    data.push_back(0x01);  // message type 1 (SetChunkSize)
    
    uint8_t stream_id1[4] = {0, 0, 0, 0};
    data.insert(data.end(), stream_id1, stream_id1 + 4);
    
    // SetChunkSize payload: new chunk size = 4096
    uint8_t chunk_size_payload[4];
    bytes::write_u32_be(chunk_size_payload, 4096);
    data.insert(data.end(), chunk_size_payload, chunk_size_payload + 4);
    
    // Second chunk: Connect command (type 20) with 161 bytes payload
    // This is larger than old chunk size (128) but fits in new chunk size (4096)
    data.push_back(0x03);  // fmt=0, csid=3
    
    uint8_t timestamp2[3] = {0, 0, 0};
    data.insert(data.end(), timestamp2, timestamp2 + 3);
    
    uint8_t length2[3] = {0, 0, 161};  // 161 bytes payload
    data.insert(data.end(), length2, length2 + 3);
    
    data.push_back(0x14);  // message type 20 (AMF0 command)
    
    uint8_t stream_id2[4] = {0, 0, 0, 0};
    data.insert(data.end(), stream_id2, stream_id2 + 4);
    
    // Add 161 bytes of payload for the connect message
    for (int i = 0; i < 161; ++i) {
        data.push_back(static_cast<uint8_t>(i % 256));
    }
    
    size_t consumed = 0;
    auto result = parser.parse(data, consumed);
    
    REQUIRE(result.is_ok());
    REQUIRE_EQUAL(consumed, data.size());
    
    auto& chunks = result.value();
    REQUIRE_EQUAL(chunks.size(), 2);
    
    // First chunk: SetChunkSize
    REQUIRE_EQUAL(chunks[0].header.chunk_stream_id, 2);
    REQUIRE_EQUAL(chunks[0].header.message_type_id, 0x01);
    REQUIRE_EQUAL(chunks[0].header.message_length, 4);
    REQUIRE_EQUAL(chunks[0].payload.size(), 4);
    
    // Second chunk: Connect command - should be fully parsed
    REQUIRE_EQUAL(chunks[1].header.chunk_stream_id, 3);
    REQUIRE_EQUAL(chunks[1].header.message_type_id, 0x14);
    REQUIRE_EQUAL(chunks[1].header.message_length, 161);
    REQUIRE_EQUAL(chunks[1].payload.size(), 161);
    
    // Verify parser chunk size was updated
    REQUIRE_EQUAL(parser.get_chunk_size(), 4096);
}
