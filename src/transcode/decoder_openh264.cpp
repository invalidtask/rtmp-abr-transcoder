#include "transcode/decoder.hpp"
#include "core/log.hpp"
#include <wels/codec_api.h>
#include <cstring>

namespace transcode {

struct H264Decoder::Impl {
    ISVCDecoder* decoder = nullptr;
    bool initialized = false;
};

H264Decoder::H264Decoder() : impl_(std::make_unique<Impl>()) {}

H264Decoder::~H264Decoder() {
    if (impl_->decoder) {
        WelsDestroyDecoder(impl_->decoder);
    }
}

bool H264Decoder::initialize() {
    if (impl_->initialized) {
        return true;
    }
    
    if (WelsCreateDecoder(&impl_->decoder) != 0) {
        Logger::error("Failed to create OpenH264 decoder");
        return false;
    }
    
    SDecodingParam param = {0};
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    
    if (impl_->decoder->Initialize(&param) != 0) {
        Logger::error("Failed to initialize OpenH264 decoder");
        WelsDestroyDecoder(impl_->decoder);
        impl_->decoder = nullptr;
        return false;
    }
    
    impl_->initialized = true;
    Logger::info("H.264 decoder initialized");
    return true;
}

bool H264Decoder::decode(const uint8_t* nalu_data, size_t size, uint32_t timestamp, std::vector<VideoFrame>& out_frames) {
    if (!impl_->initialized || !impl_->decoder) {
        return false;
    }
    
    uint8_t* decoded_data[3] = {nullptr, nullptr, nullptr};
    SBufferInfo buf_info = {0};
    
    // Decode the frame
    int ret = impl_->decoder->DecodeFrameNoDelay(nalu_data, size, decoded_data, &buf_info);
    
    if (ret != 0) {
        Logger::debug("Decode failed with return code: ", ret);
        return false;
    }
    
    // Check if we got a frame
    if (buf_info.iBufferStatus == 1 && decoded_data[0] != nullptr) {
        VideoFrame frame;
        frame.width = buf_info.UsrData.sSystemBuffer.iWidth;
        frame.height = buf_info.UsrData.sSystemBuffer.iHeight;
        frame.y_stride = buf_info.UsrData.sSystemBuffer.iStride[0];
        frame.uv_stride = buf_info.UsrData.sSystemBuffer.iStride[1];
        frame.timestamp = timestamp;
        frame.keyframe = (buf_info.UsrData.sSystemBuffer.iFormat == videoFormatI420);
        
        // Copy Y plane
        int y_size = frame.y_stride * frame.height;
        frame.y_plane.resize(y_size);
        std::memcpy(frame.y_plane.data(), decoded_data[0], y_size);
        
        // Copy U plane
        int uv_height = frame.height / 2;
        int u_size = frame.uv_stride * uv_height;
        frame.u_plane.resize(u_size);
        std::memcpy(frame.u_plane.data(), decoded_data[1], u_size);
        
        // Copy V plane
        int v_size = frame.uv_stride * uv_height;
        frame.v_plane.resize(v_size);
        std::memcpy(frame.v_plane.data(), decoded_data[2], v_size);
        
        out_frames.push_back(std::move(frame));
        
        Logger::debug("Decoded frame ", out_frames.size() - 1, ": ", frame.width, "x", frame.height, 
                      ", keyframe: ", frame.keyframe);
        
        return true;
    }
    
    return true;  // No error, just no frame yet
}

}
