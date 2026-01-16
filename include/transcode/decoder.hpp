#pragma once
#include <cstdint>
#include <vector>
#include <memory>

namespace transcode {

struct VideoFrame {
    std::vector<uint8_t> y_plane;
    std::vector<uint8_t> u_plane;
    std::vector<uint8_t> v_plane;
    int width;
    int height;
    int y_stride;
    int uv_stride;
    uint32_t timestamp;
    bool keyframe;
};

struct AudioFrame {
    std::vector<int16_t> samples;  // Interleaved PCM
    int sample_rate;
    int channels;
    uint32_t timestamp;
};

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();
    
    bool initialize();
    bool decode(const uint8_t* nalu_data, size_t size, uint32_t timestamp, std::vector<VideoFrame>& out_frames);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AACDecoder {
public:
    AACDecoder();
    ~AACDecoder();
    
    bool initialize(const uint8_t* asc_data, size_t asc_size);  // Audio Specific Config
    bool decode(const uint8_t* aac_data, size_t size, uint32_t timestamp, std::vector<AudioFrame>& out_frames);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
