#include "transcode/encoder.hpp"
#include "core/log.hpp"
#include <wels/codec_api.h>
#include <cstring>

namespace transcode {

// H.264 NAL unit types
constexpr uint8_t NAL_TYPE_SPS = 7;
constexpr uint8_t NAL_TYPE_PPS = 8;

// Helper function to detect and skip Annex-B start code
// Returns the start code length (3 or 4 bytes)
static int get_startcode_length(const uint8_t* data, int size) {
    if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
        return 4;  // 00 00 00 01
    } else if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
        return 3;  // 00 00 01
    }
    return 0;  // No start code found
}

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
            uint8_t* nal_ptr = layer_info->pBsBuf;
            
            for (int nal = 0; nal < layer_info->iNalCount; nal++) {
                int nal_size_with_startcode = layer_info->pNalLengthInByte[nal];
                
                // Skip the start code
                int startcode_len = get_startcode_length(nal_ptr, nal_size_with_startcode);
                if (startcode_len == 0) {
                    Logger::error("Expected Annex-B start code in NAL unit from encoder");
                    nal_ptr += nal_size_with_startcode;
                    continue;
                }
                
                int nal_size = nal_size_with_startcode - startcode_len;
                uint8_t* nal_data = nal_ptr + startcode_len;
                
                // Check NAL type (first byte of NAL unit)
                if (nal_size > 0) {
                    uint8_t nal_type = nal_data[0] & 0x1F;
                    if (nal_type == NAL_TYPE_SPS) {
                        impl_->sps.assign(nal_data, nal_data + nal_size);
                    } else if (nal_type == NAL_TYPE_PPS) {
                        impl_->pps.assign(nal_data, nal_data + nal_size);
                    }
                }
                
                nal_ptr += nal_size_with_startcode;
            }
        }
    }
    
    // Collect encoded data
    EncodedPacket packet;
    packet.timestamp = frame.timestamp;
    packet.dts = frame.timestamp;
    packet.keyframe = (info.eFrameType == videoFrameTypeIDR);
    
    // Convert from Annex-B format (with start codes) to AVCC format (with length prefixes)
    for (int layer = 0; layer < info.iLayerNum; layer++) {
        SLayerBSInfo* layer_info = &info.sLayerInfo[layer];
        uint8_t* nal_ptr = layer_info->pBsBuf;
        
        for (int nal = 0; nal < layer_info->iNalCount; nal++) {
            int nal_size_with_startcode = layer_info->pNalLengthInByte[nal];
            
            // Skip the start code
            int startcode_len = get_startcode_length(nal_ptr, nal_size_with_startcode);
            if (startcode_len == 0) {
                Logger::error("Expected Annex-B start code in NAL unit from encoder");
                nal_ptr += nal_size_with_startcode;
                continue;
            }
            
            int nal_size = nal_size_with_startcode - startcode_len;
            if (nal_size <= 0) {
                Logger::error("Invalid NAL size after removing start code");
                nal_ptr += nal_size_with_startcode;
                continue;
            }
            
            uint8_t* nal_data = nal_ptr + startcode_len;
            
            // Write 4-byte big-endian length prefix (AVCC format)
            size_t old_size = packet.data.size();
            packet.data.resize(old_size + 4 + nal_size);
            packet.data[old_size] = (nal_size >> 24) & 0xFF;
            packet.data[old_size + 1] = (nal_size >> 16) & 0xFF;
            packet.data[old_size + 2] = (nal_size >> 8) & 0xFF;
            packet.data[old_size + 3] = nal_size & 0xFF;
            memcpy(packet.data.data() + old_size + 4, nal_data, nal_size);
            
            nal_ptr += nal_size_with_startcode;
        }
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
