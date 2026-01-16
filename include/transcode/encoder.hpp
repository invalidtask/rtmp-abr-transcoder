#pragma once
#include "decoder.hpp"
#include <string>

namespace transcode {

struct EncoderConfig {
    int width;
    int height;
    int bitrate_kbps;
    int fps;
    int gop_size;  // Keyframe interval
    std::string preset;  // "fast", "medium", "slow"
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    uint32_t timestamp;
    uint32_t dts;
    bool keyframe;
};

class H264Encoder {
public:
    H264Encoder();
    ~H264Encoder();
    
    bool initialize(const EncoderConfig& config);
    bool encode(const VideoFrame& frame, std::vector<EncodedPacket>& out_packets);
    bool flush(std::vector<EncodedPacket>& out_packets);
    
    std::vector<uint8_t> get_sps() const;
    std::vector<uint8_t> get_pps() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AACEncoder {
public:
    AACEncoder();
    ~AACEncoder();
    
    bool initialize(int sample_rate, int channels, int bitrate_kbps);
    bool encode(const AudioFrame& frame, std::vector<EncodedPacket>& out_packets);
    
    std::vector<uint8_t> get_audio_specific_config() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
