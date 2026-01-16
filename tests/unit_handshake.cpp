#include "test_framework.hpp"
#include "rtmp/rtmp_handshake.hpp"
#include <vector>
#include <cstring>

// Test that handshake requires full S0+S1+S2 data (3073 bytes)
TEST_CASE(handshake_server_requires_full_data) {
    rtmp::Handshake handshake;
    
    // Generate C0+C1 to set state to C0C1Sent
    auto c0c1 = handshake.generate_c0_c1();
    REQUIRE_EQUAL(c0c1.size(), 1537);
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::C0C1Sent);
    
    // Create partial S0+S1+S2 data (only first 1448 bytes as in the problem)
    std::vector<uint8_t> partial_data(1448);
    partial_data[0] = 3; // S0
    // Fill rest with dummy data
    for (size_t i = 1; i < 1448; ++i) {
        partial_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Attempt to process partial data - should fail with NEED_MORE_DATA
    auto result = handshake.process_server_handshake(
        std::span<const uint8_t>(partial_data.data(), partial_data.size())
    );
    
    REQUIRE(result.is_err());
    REQUIRE_EQUAL(std::string(result.error()), std::string("NEED_MORE_DATA"));
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::C0C1Sent);
}

// Test that handshake succeeds with full S0+S1+S2 data
TEST_CASE(handshake_server_succeeds_with_full_data) {
    rtmp::Handshake handshake;
    
    // Generate C0+C1 to set state to C0C1Sent
    auto c0c1 = handshake.generate_c0_c1();
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::C0C1Sent);
    
    // Create full S0+S1+S2 data (3073 bytes)
    constexpr size_t FULL_SIZE = 1 + 1536 + 1536;
    std::vector<uint8_t> full_data(FULL_SIZE);
    full_data[0] = 3; // S0
    
    // Fill S1 (bytes 1-1536) with dummy data
    for (size_t i = 1; i < 1537; ++i) {
        full_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Fill S2 (bytes 1537-3072) with dummy data
    for (size_t i = 1537; i < FULL_SIZE; ++i) {
        full_data[i] = static_cast<uint8_t>((i * 2) % 256);
    }
    
    // Process full data - should succeed
    auto result = handshake.process_server_handshake(
        std::span<const uint8_t>(full_data.data(), full_data.size())
    );
    
    REQUIRE(result.is_ok());
    REQUIRE_EQUAL(result.value(), FULL_SIZE);
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::Done);
    
    // Verify S1 data was stored correctly
    const auto& s1 = handshake.s1_data();
    REQUIRE_EQUAL(s1.size(), 1536);
    REQUIRE_EQUAL(s1[0], full_data[1]);
    REQUIRE_EQUAL(s1[100], full_data[101]);
}

// Test incremental buffering scenario (mimics the TCP fragmentation)
TEST_CASE(handshake_buffering_across_reads) {
    rtmp::Handshake handshake;
    
    // Generate C0+C1
    auto c0c1 = handshake.generate_c0_c1();
    
    // Simulate S0+S1+S2 arriving in 3 TCP segments: 1448, 1448, 177
    std::vector<uint8_t> full_data(3073);
    full_data[0] = 3;
    for (size_t i = 1; i < 3073; ++i) {
        full_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Simulate a buffer that accumulates data
    std::vector<uint8_t> buffer;
    
    // First read: 1448 bytes
    buffer.insert(buffer.end(), full_data.begin(), full_data.begin() + 1448);
    auto result1 = handshake.process_server_handshake(
        std::span<const uint8_t>(buffer.data(), buffer.size())
    );
    REQUIRE(result1.is_err());
    REQUIRE_EQUAL(std::string(result1.error()), std::string("NEED_MORE_DATA"));
    
    // Second read: another 1448 bytes (total 2896)
    buffer.insert(buffer.end(), full_data.begin() + 1448, full_data.begin() + 2896);
    auto result2 = handshake.process_server_handshake(
        std::span<const uint8_t>(buffer.data(), buffer.size())
    );
    REQUIRE(result2.is_err());
    REQUIRE_EQUAL(std::string(result2.error()), std::string("NEED_MORE_DATA"));
    
    // Third read: final 177 bytes (total 3073)
    buffer.insert(buffer.end(), full_data.begin() + 2896, full_data.end());
    auto result3 = handshake.process_server_handshake(
        std::span<const uint8_t>(buffer.data(), buffer.size())
    );
    REQUIRE(result3.is_ok());
    REQUIRE_EQUAL(result3.value(), 3073);
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::Done);
}

// Test that generate_c2 correctly echoes S1 data
TEST_CASE(handshake_generate_c2_echoes_s1) {
    rtmp::Handshake handshake;
    
    // Create S1 data
    std::vector<uint8_t> s1_data(1536);
    for (size_t i = 0; i < 1536; ++i) {
        s1_data[i] = static_cast<uint8_t>((i * 7) % 256);
    }
    
    // Generate C2
    auto c2 = handshake.generate_c2(std::span<const uint8_t>(s1_data.data(), s1_data.size()));
    
    REQUIRE_EQUAL(c2.size(), 1536);
    REQUIRE_EQUAL(handshake.state(), rtmp::Handshake::State::C2Sent);
    
    // Verify C2 is exact echo of S1
    for (size_t i = 0; i < 1536; ++i) {
        REQUIRE_EQUAL(c2[i], s1_data[i]);
    }
}

// Test accessors for c1_data and s1_data
TEST_CASE(handshake_data_accessors) {
    rtmp::Handshake handshake;
    
    // Generate C0+C1 and verify c1_data accessor
    auto c0c1 = handshake.generate_c0_c1();
    const auto& c1 = handshake.c1_data();
    REQUIRE_EQUAL(c1.size(), 1536);
    
    // Verify c1 matches bytes 1-1536 of c0c1
    for (size_t i = 0; i < 1536; ++i) {
        REQUIRE_EQUAL(c1[i], c0c1[i + 1]);
    }
    
    // Process server handshake and verify s1_data accessor
    std::vector<uint8_t> s0s1s2(3073);
    s0s1s2[0] = 3;
    for (size_t i = 1; i < 3073; ++i) {
        s0s1s2[i] = static_cast<uint8_t>((i * 3) % 256);
    }
    
    auto result = handshake.process_server_handshake(
        std::span<const uint8_t>(s0s1s2.data(), s0s1s2.size())
    );
    REQUIRE(result.is_ok());
    
    const auto& s1 = handshake.s1_data();
    REQUIRE_EQUAL(s1.size(), 1536);
    
    // Verify s1 matches bytes 1-1536 of s0s1s2
    for (size_t i = 0; i < 1536; ++i) {
        REQUIRE_EQUAL(s1[i], s0s1s2[i + 1]);
    }
}
