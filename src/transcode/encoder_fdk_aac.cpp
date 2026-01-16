#include "transcode/encoder.hpp"
#include "core/log.hpp"
#include <fdk-aac/aacenc_lib.h>
#include <cstring>

namespace transcode {

struct AACEncoder::Impl {
    HANDLE_AACENCODER encoder = nullptr;
    bool initialized = false;
    std::vector<uint8_t> asc;
    int sample_rate = 0;
    int channels = 0;
    int frame_size = 0;
};

AACEncoder::AACEncoder() : impl_(std::make_unique<Impl>()) {}

AACEncoder::~AACEncoder() {
    if (impl_->encoder) {
        aacEncClose(&impl_->encoder);
    }
}

bool AACEncoder::initialize(int sample_rate, int channels, int bitrate_kbps) {
    if (impl_->initialized) {
        return true;
    }
    
    if (aacEncOpen(&impl_->encoder, 0, channels) != AACENC_OK) {
        Logger::error("Failed to create FDK-AAC encoder");
        return false;
    }
    
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    
    // Set encoder parameters
    if (aacEncoder_SetParam(impl_->encoder, AACENC_AOT, AOT_AAC_LC) != AACENC_OK) {
        Logger::error("Failed to set AAC profile");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    if (aacEncoder_SetParam(impl_->encoder, AACENC_SAMPLERATE, sample_rate) != AACENC_OK) {
        Logger::error("Failed to set sample rate");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    if (aacEncoder_SetParam(impl_->encoder, AACENC_CHANNELMODE, channels == 2 ? MODE_2 : MODE_1) != AACENC_OK) {
        Logger::error("Failed to set channel mode");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    if (aacEncoder_SetParam(impl_->encoder, AACENC_BITRATE, bitrate_kbps * 1000) != AACENC_OK) {
        Logger::error("Failed to set bitrate");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    if (aacEncoder_SetParam(impl_->encoder, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK) {
        Logger::error("Failed to set transport format");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    // Initialize encoder
    if (aacEncEncode(impl_->encoder, nullptr, nullptr, nullptr, nullptr) != AACENC_OK) {
        Logger::error("Failed to initialize AAC encoder");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    // Get encoder info
    AACENC_InfoStruct info;
    if (aacEncInfo(impl_->encoder, &info) != AACENC_OK) {
        Logger::error("Failed to get encoder info");
        aacEncClose(&impl_->encoder);
        impl_->encoder = nullptr;
        return false;
    }
    
    impl_->frame_size = info.frameLength;
    
    // Extract Audio Specific Config
    impl_->asc.assign(info.confBuf, info.confBuf + info.confSize);
    
    impl_->initialized = true;
    Logger::info("AAC encoder initialized: ", sample_rate, "Hz, ", 
                 channels, " channels @ ", bitrate_kbps, " kbps");
    return true;
}

bool AACEncoder::encode(const AudioFrame& frame, std::vector<EncodedPacket>& out_packets) {
    if (!impl_->initialized || !impl_->encoder) {
        return false;
    }
    
    // Prepare input buffer
    AACENC_BufDesc in_buf = {0};
    AACENC_InArgs in_args = {0};
    
    void* in_ptr = const_cast<int16_t*>(frame.samples.data());
    int in_size = frame.samples.size() * sizeof(int16_t);
    int in_elem_size = sizeof(int16_t);
    int in_identifier = IN_AUDIO_DATA;
    
    in_buf.numBufs = 1;
    in_buf.bufs = &in_ptr;
    in_buf.bufferIdentifiers = &in_identifier;
    in_buf.bufSizes = &in_size;
    in_buf.bufElSizes = &in_elem_size;
    
    in_args.numInSamples = frame.samples.size();
    
    // Prepare output buffer
    std::vector<uint8_t> out_buffer(8192);
    AACENC_BufDesc out_buf = {0};
    AACENC_OutArgs out_args = {0};
    
    void* out_ptr = out_buffer.data();
    int out_size = out_buffer.size();
    int out_elem_size = 1;
    int out_identifier = OUT_BITSTREAM_DATA;
    
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_identifier;
    out_buf.bufSizes = &out_size;
    out_buf.bufElSizes = &out_elem_size;
    
    // Encode
    AACENC_ERROR err = aacEncEncode(impl_->encoder, &in_buf, &out_buf, &in_args, &out_args);
    if (err != AACENC_OK) {
        Logger::debug("AAC encode failed: ", err);
        return false;
    }
    
    if (out_args.numOutBytes > 0) {
        EncodedPacket packet;
        packet.timestamp = frame.timestamp;
        packet.dts = frame.timestamp;
        packet.keyframe = false;
        packet.data.assign(out_buffer.begin(), out_buffer.begin() + out_args.numOutBytes);
        
        out_packets.push_back(std::move(packet));
        
        Logger::debug("Encoded audio: ", packet.data.size(), " bytes");
        return true;
    }
    
    return true;  // No output yet, not an error
}

std::vector<uint8_t> AACEncoder::get_audio_specific_config() const {
    return impl_->asc;
}

}
