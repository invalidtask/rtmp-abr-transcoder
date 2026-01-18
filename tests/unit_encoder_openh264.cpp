#include "test_framework.hpp"
#include "transcode/encoder.hpp"
#include "transcode/decoder.hpp"
#include <vector>

// H.264 NAL unit constants
constexpr uint8_t NAL_FORBIDDEN_ZERO_BIT_MASK = 0x80;
constexpr uint8_t NAL_TYPE_MASK = 0x1F;
constexpr uint8_t MAX_VALID_NAL_TYPE = 31;

// Test that encoder outputs AVCC format (4-byte length prefix, not Annex-B start codes)
TEST_CASE(encoder_outputs_avcc_format) {
    transcode::H264Encoder encoder;
    
    // Initialize encoder with simple config
    transcode::EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.bitrate_kbps = 500;
    config.fps = 30;
    config.gop_size = 60;
    config.preset = "fast";
    
    REQUIRE(encoder.initialize(config));
    
    // Create a simple test frame (all zeros for simplicity)
    transcode::VideoFrame frame;
    frame.width = 320;
    frame.height = 240;
    frame.y_stride = 320;
    frame.uv_stride = 160;
    frame.timestamp = 0;
    
    // Allocate frame data
    frame.y_plane.resize(320 * 240);
    frame.u_plane.resize(160 * 120);
    frame.v_plane.resize(160 * 120);
    
    std::vector<transcode::EncodedPacket> packets;
    REQUIRE(encoder.encode(frame, packets));
    REQUIRE(packets.size() > 0);
    
    // Check that the packet data is in AVCC format
    const auto& packet = packets[0];
    REQUIRE(packet.data.size() > 4);
    
    // Parse NAL units from AVCC format
    size_t offset = 0;
    bool found_valid_nalu = false;
    
    while (offset + 4 <= packet.data.size()) {
        // Read 4-byte big-endian length prefix (AVCC format)
        uint32_t nal_size = (static_cast<uint32_t>(packet.data[offset]) << 24) |
                           (static_cast<uint32_t>(packet.data[offset + 1]) << 16) |
                           (static_cast<uint32_t>(packet.data[offset + 2]) << 8) |
                           static_cast<uint32_t>(packet.data[offset + 3]);
        
        offset += 4;
        
        // Validate NAL size is reasonable
        REQUIRE(nal_size > 0);
        REQUIRE(offset + nal_size <= packet.data.size());
        
        // Verify this is NOT an Annex-B start code
        // Annex-B would have 00 00 00 01 or 00 00 01 at the start
        // But in AVCC, the first byte should be the NAL unit header
        if (nal_size >= 1) {
            uint8_t nal_header = packet.data[offset];
            // NAL unit header should have forbidden_zero_bit = 0 (bit 7)
            REQUIRE((nal_header & NAL_FORBIDDEN_ZERO_BIT_MASK) == 0);
            found_valid_nalu = true;
        }
        
        offset += nal_size;
    }
    
    // Should have consumed all data
    REQUIRE_EQUAL(offset, packet.data.size());
    REQUIRE(found_valid_nalu);
}

// Test that encoder does NOT output Annex-B format
TEST_CASE(encoder_does_not_output_annexb) {
    transcode::H264Encoder encoder;
    
    transcode::EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.bitrate_kbps = 500;
    config.fps = 30;
    config.gop_size = 60;
    config.preset = "fast";
    
    REQUIRE(encoder.initialize(config));
    
    transcode::VideoFrame frame;
    frame.width = 320;
    frame.height = 240;
    frame.y_stride = 320;
    frame.uv_stride = 160;
    frame.timestamp = 0;
    
    frame.y_plane.resize(320 * 240);
    frame.u_plane.resize(160 * 120);
    frame.v_plane.resize(160 * 120);
    
    std::vector<transcode::EncodedPacket> packets;
    REQUIRE(encoder.encode(frame, packets));
    REQUIRE(packets.size() > 0);
    
    const auto& packet = packets[0];
    
    // Check that we don't have Annex-B start codes (00 00 00 01 or 00 00 01)
    // anywhere in the packet data
    for (size_t i = 0; i + 4 <= packet.data.size(); i++) {
        // Check for 4-byte Annex-B start code
        if (packet.data[i] == 0x00 && packet.data[i+1] == 0x00 && 
            packet.data[i+2] == 0x00 && packet.data[i+3] == 0x01) {
            // We found an Annex-B start code - this should NOT happen in AVCC format
            REQUIRE(false); // Fail the test
        }
    }
    
    for (size_t i = 0; i + 3 <= packet.data.size(); i++) {
        // Check for 3-byte Annex-B start code
        if (packet.data[i] == 0x00 && packet.data[i+1] == 0x00 && 
            packet.data[i+2] == 0x01) {
            // Check if this could be a start code (not just random data)
            // Start codes should be followed by a valid NAL header
            if (i + 3 < packet.data.size()) {
                uint8_t potential_nal_header = packet.data[i+3];
                // Valid NAL header has forbidden_zero_bit = 0 (bit 7)
                // NAL type should be in range 1-31 for valid NAL units (type 0 is reserved)
                uint8_t nal_type = potential_nal_header & NAL_TYPE_MASK;
                if ((potential_nal_header & NAL_FORBIDDEN_ZERO_BIT_MASK) == 0 && 
                    nal_type >= 1 && nal_type <= MAX_VALID_NAL_TYPE) {
                    REQUIRE(false); // Fail - found Annex-B start code
                }
            }
        }
    }
}
