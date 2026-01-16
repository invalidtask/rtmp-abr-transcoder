#include "transcode/scaler.hpp"
#include "core/log.hpp"
#include <libyuv.h>

namespace transcode {

struct Scaler::Impl {
    int src_width = 0;
    int src_height = 0;
    int dst_width = 0;
    int dst_height = 0;
    bool initialized = false;
};

Scaler::Scaler() : impl_(std::make_unique<Impl>()) {}

Scaler::~Scaler() {}

bool Scaler::initialize(int src_width, int src_height, int dst_width, int dst_height) {
    impl_->src_width = src_width;
    impl_->src_height = src_height;
    impl_->dst_width = dst_width;
    impl_->dst_height = dst_height;
    impl_->initialized = true;
    
    Logger::info("Scaler initialized: ", src_width, "x", src_height, 
                 " -> ", dst_width, "x", dst_height);
    return true;
}

bool Scaler::scale(const VideoFrame& src, VideoFrame& dst) {
    if (!impl_->initialized) {
        return false;
    }
    
    // Prepare destination frame
    dst.width = impl_->dst_width;
    dst.height = impl_->dst_height;
    dst.y_stride = impl_->dst_width;
    dst.uv_stride = impl_->dst_width / 2;
    dst.timestamp = src.timestamp;
    dst.keyframe = src.keyframe;
    
    // Allocate destination buffers
    dst.y_plane.resize(dst.y_stride * dst.height);
    dst.u_plane.resize(dst.uv_stride * dst.height / 2);
    dst.v_plane.resize(dst.uv_stride * dst.height / 2);
    
    // Scale using libyuv
    int ret = libyuv::I420Scale(
        src.y_plane.data(), src.y_stride,
        src.u_plane.data(), src.uv_stride,
        src.v_plane.data(), src.uv_stride,
        src.width, src.height,
        dst.y_plane.data(), dst.y_stride,
        dst.u_plane.data(), dst.uv_stride,
        dst.v_plane.data(), dst.uv_stride,
        dst.width, dst.height,
        libyuv::kFilterBilinear
    );
    
    if (ret != 0) {
        Logger::error("Scaling failed: ", ret);
        return false;
    }
    
    Logger::debug("Scaled to ", dst.width, "x", dst.height);
    return true;
}

}
