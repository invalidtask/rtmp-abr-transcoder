#include "transcode/encoder.hpp"
#include "core/log.hpp"
#include <wels/codec_api.h>
#include <cstring>

namespace transcode {

struct H264Encoder::Impl {
    ISVCEncoder* encoder = nullptr;
    bool initialized = false;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    int frame_num = 0;
};

H264Encoder::H264Encoder() : impl_(std::make_unique<Impl>()) {}

H264Encoder::~H264Encoder() {
    if (impl_->encoder) {
        WelsDestroySVCEncoder(impl_->encoder);
    }
}

bool H264Encoder::initialize(const EncoderConfig& config) {
    if (impl_->initialized) {
        return true;
    }
    
    if (WelsCreateSVCEncoder(&impl_->encoder) != 0) {
        Logger::error("Failed to create OpenH264 encoder");
        return false;
    }
    
    SEncParamExt param;
    impl_->encoder->GetDefaultParams(&param);
    
    param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    param.fMaxFrameRate = config.fps;
    param.iPicWidth = config.width;
    param.iPicHeight = config.height;
    param.iTargetBitrate = config.bitrate_kbps * 1000;
    param.iRCMode = RC_BITRATE_MODE;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.bEnableDenoise = false;
    param.bEnableBackgroundDetection = false;
    param.bEnableAdaptiveQuant = true;
    param.bEnableFrameSkip = false;
    param.bEnableLongTermReference = false;
    param.iLtrMarkPeriod = 30;
    param.uiIntraPeriod = config.gop_size;
    param.eSpsPpsIdStrategy = CONSTANT_ID;
    param.bPrefixNalAddingCtrl = false;
    param.iLoopFilterDisableIdc = 0;
    param.iEntropyCodingModeFlag = 0;
    param.iMultipleThreadIdc = 1;
    
    param.sSpatialLayers[0].iVideoWidth = config.width;
    param.sSpatialLayers[0].iVideoHeight = config.height;
    param.sSpatialLayers[0].fFrameRate = config.fps;
    param.sSpatialLayers[0].iSpatialBitrate = config.bitrate_kbps * 1000;
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    
    if (impl_->encoder->InitializeExt(&param) != 0) {
        Logger::error("Failed to initialize OpenH264 encoder");
        WelsDestroySVCEncoder(impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    // Set encoding options
    int video_format = videoFormatI420;
    impl_->encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);
    
    impl_->initialized = true;
    Logger::info("H.264 encoder initialized: ", config.width, "x", config.height, 
                 " @ ", config.bitrate_kbps, " kbps");
    return true;
}

bool H264Encoder::encode(const VideoFrame& frame, std::vector<EncodedPacket>& out_packets) {
    if (!impl_->initialized || !impl_->encoder) {
        return false;
    }
    
    SSourcePicture pic;
    memset(&pic, 0, sizeof(SSourcePicture));
    
    pic.iPicWidth = frame.width;
    pic.iPicHeight = frame.height;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = frame.y_stride;
    pic.iStride[1] = frame.uv_stride;
    pic.iStride[2] = frame.uv_stride;
    pic.pData[0] = const_cast<uint8_t*>(frame.y_plane.data());
    pic.pData[1] = const_cast<uint8_t*>(frame.u_plane.data());
    pic.pData[2] = const_cast<uint8_t*>(frame.v_plane.data());
    pic.uiTimeStamp = frame.timestamp;
    
    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));
    
    int ret = impl_->encoder->EncodeFrame(&pic, &info);
    if (ret != 0) {
        Logger::error("Encode frame failed: ", ret);
        return false;
    }
    
    if (info.eFrameType == videoFrameTypeSkip) {
        return true;  // Frame skipped, not an error
    }
    
    // Extract SPS/PPS on first keyframe
    if (info.eFrameType == videoFrameTypeIDR && impl_->sps.empty()) {
        for (int layer = 0; layer < info.iLayerNum; layer++) {
            SLayerBSInfo* layer_info = &info.sLayerInfo[layer];
            for (int nal = 0; nal < layer_info->iNalCount; nal++) {
                int nal_size = layer_info->pNalLengthInByte[nal];
                uint8_t* nal_data = layer_info->pBsBuf;
                for (int i = 0; i < nal; i++) {
                    nal_data += layer_info->pNalLengthInByte[i];
                }
                
                // Check NAL type (first byte after start code)
                if (nal_size > 4) {
                    uint8_t nal_type = nal_data[4] & 0x1F;
                    if (nal_type == 7) {  // SPS
                        impl_->sps.assign(nal_data + 4, nal_data + nal_size);
                    } else if (nal_type == 8) {  // PPS
                        impl_->pps.assign(nal_data + 4, nal_data + nal_size);
                    }
                }
            }
        }
    }
    
    // Collect encoded data
    EncodedPacket packet;
    packet.timestamp = frame.timestamp;
    packet.dts = frame.timestamp;
    packet.keyframe = (info.eFrameType == videoFrameTypeIDR);
    
    for (int layer = 0; layer < info.iLayerNum; layer++) {
        SLayerBSInfo* layer_info = &info.sLayerInfo[layer];
        int layer_size = 0;
        for (int nal = 0; nal < layer_info->iNalCount; nal++) {
            layer_size += layer_info->pNalLengthInByte[nal];
        }
        
        size_t old_size = packet.data.size();
        packet.data.resize(old_size + layer_size);
        memcpy(packet.data.data() + old_size, layer_info->pBsBuf, layer_size);
    }
    
    out_packets.push_back(std::move(packet));
    
    impl_->frame_num++;
    Logger::debug("Encoded ", packet.keyframe ? "keyframe" : "frame", 
                  ": ", packet.data.size(), " bytes");
    
    return true;
}

bool H264Encoder::flush(std::vector<EncodedPacket>& out_packets) {
    // OpenH264 doesn't require explicit flushing
    return true;
}

std::vector<uint8_t> H264Encoder::get_sps() const {
    return impl_->sps;
}

std::vector<uint8_t> H264Encoder::get_pps() const {
    return impl_->pps;
}

}
