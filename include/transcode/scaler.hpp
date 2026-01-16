#pragma once
#include "decoder.hpp"

namespace transcode {

class Scaler {
public:
    Scaler();
    ~Scaler();
    
    bool initialize(int src_width, int src_height, int dst_width, int dst_height);
    bool scale(const VideoFrame& src, VideoFrame& dst);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
