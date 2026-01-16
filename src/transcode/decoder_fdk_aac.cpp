#include "transcode/decoder.hpp"
#include "core/log.hpp"
#include <fdk-aac/aacdecoder_lib.h>
#include <cstring>

namespace transcode {

struct AACDecoder::Impl {
    HANDLE_AACDECODER decoder = nullptr;
    bool initialized = false;
    int sample_rate = 0;
    int channels = 0;
};

AACDecoder::AACDecoder() : impl_(std::make_unique<Impl>()) {}

AACDecoder::~AACDecoder() {
    if (impl_->decoder) {
        aacDecoder_Close(impl_->decoder);
    }
}

bool AACDecoder::initialize(const uint8_t* asc_data, size_t asc_size) {
    if (impl_->initialized) {
        return true;
    }
    
    impl_->decoder = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!impl_->decoder) {
        Logger::error("Failed to create FDK-AAC decoder");
        return false;
    }
    
    // Configure with Audio Specific Config
    if (asc_data && asc_size > 0) {
        UCHAR* conf[] = {const_cast<UCHAR*>(asc_data)};
        UINT conf_size[] = {static_cast<UINT>(asc_size)};
        
        AAC_DECODER_ERROR err = aacDecoder_ConfigRaw(impl_->decoder, conf, conf_size);
        if (err != AAC_DEC_OK) {
            Logger::error("Failed to configure AAC decoder: ", err);
            aacDecoder_Close(impl_->decoder);
            impl_->decoder = nullptr;
            return false;
        }
    }
    
    impl_->initialized = true;
    Logger::info("AAC decoder initialized");
    return true;
}

bool AACDecoder::decode(const uint8_t* aac_data, size_t size, uint32_t timestamp, std::vector<AudioFrame>& out_frames) {
    if (!impl_->initialized || !impl_->decoder) {
        return false;
    }
    
    // Fill decoder buffer
    UINT bytes_valid = size;
    UINT buffer_size = size;
    UCHAR* input_buffer = const_cast<UCHAR*>(aac_data);
    
    AAC_DECODER_ERROR err = aacDecoder_Fill(impl_->decoder, &input_buffer, &buffer_size, &bytes_valid);
    if (err != AAC_DEC_OK) {
        Logger::debug("AAC decoder fill failed: ", err);
        return false;
    }
    
    // Decode frame
    const int max_samples = 2048 * 2;  // Max frame size * channels
    std::vector<INT_PCM> pcm_buffer(max_samples);
    
    err = aacDecoder_DecodeFrame(impl_->decoder, pcm_buffer.data(), max_samples, 0);
    if (err != AAC_DEC_OK) {
        Logger::debug("AAC decode frame failed: ", err);
        return false;
    }
    
    // Get stream info
    CStreamInfo* info = aacDecoder_GetStreamInfo(impl_->decoder);
    if (!info || info->sampleRate <= 0) {
        Logger::debug("Invalid stream info");
        return false;
    }
    
    AudioFrame frame;
    frame.sample_rate = info->sampleRate;
    frame.channels = info->numChannels;
    frame.timestamp = timestamp;
    
    // Copy samples (convert from INT_PCM to int16_t)
    int num_samples = info->frameSize * info->numChannels;
    frame.samples.resize(num_samples);
    for (int i = 0; i < num_samples; i++) {
        frame.samples[i] = static_cast<int16_t>(pcm_buffer[i]);
    }
    
    out_frames.push_back(std::move(frame));
    
    Logger::debug("Decoded audio frame: ", frame.sample_rate, "Hz, ", 
                  frame.channels, " channels, ", frame.samples.size() / frame.channels, " samples");
    
    return true;
}

}
