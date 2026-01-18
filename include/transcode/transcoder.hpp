#pragma once
#include "decoder.hpp"
#include "encoder.hpp"
#include "scaler.hpp"
#include "rtmp/rtmp_client.hpp"
#include "net/epoll_loop.hpp"
#include <functional>

namespace transcode {

struct OutputConfig {
    std::string name;  // "720p", "360p"
    int width;
    int height;
    int video_bitrate_kbps;
    int audio_bitrate_kbps;
    std::string rtmp_url;
};

class Transcoder {
public:
    Transcoder(EpollLoop& loop);
    ~Transcoder();
    
    bool add_output(const OutputConfig& config);
    
    // Called when source stream metadata is available
    void on_source_metadata(int width, int height, int fps, int sample_rate, int channels);
    
    // Called for each incoming media message
    void on_video_data(const uint8_t* data, size_t size, uint32_t timestamp, bool keyframe);
    void on_audio_data(const uint8_t* data, size_t size, uint32_t timestamp);
    
    // Start/stop transcoding
    bool start();
    void stop();
    
private:
    struct Output {
        OutputConfig config;
        std::unique_ptr<Scaler> scaler;
        std::unique_ptr<H264Encoder> video_encoder;
        std::unique_ptr<AACEncoder> audio_encoder;
        std::unique_ptr<rtmp::Client> pusher;
        bool connected = false;
        bool publishing = false;
        bool video_initialized = false;
        bool audio_initialized = false;
        uint32_t stream_id = 0;
        std::vector<EncodedPacket> pending_video;
        std::vector<EncodedPacket> pending_audio;
    };
    
    void process_video_frame(const VideoFrame& frame);
    void process_audio_frame(const AudioFrame& frame);
    void push_video_packet(Output& output, const EncodedPacket& packet);
    void push_audio_packet(Output& output, const EncodedPacket& packet);
    void setup_pusher_callbacks(Output* output);
    void on_publish_ready(Output* output);
    void parse_avc_decoder_config(const uint8_t* data, size_t size);
    
    // Helper methods for building FLV packets
    std::vector<uint8_t> build_flv_video_packet(const EncodedPacket& packet);
    std::vector<uint8_t> build_flv_audio_packet(const EncodedPacket& packet);
    std::vector<uint8_t> build_avc_decoder_config(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);
    std::vector<uint8_t> build_aac_sequence_header(const std::vector<uint8_t>& asc);
    uint8_t map_sample_rate_to_flv_sound_rate(uint32_t sample_rate);
    
    EpollLoop& loop_;
    std::unique_ptr<H264Decoder> video_decoder_;
    std::unique_ptr<AACDecoder> audio_decoder_;
    std::vector<std::unique_ptr<Output>> outputs_;
    
    int source_width_ = 0;
    int source_height_ = 0;
    int source_fps_ = 30;
    int source_sample_rate_ = 44100;
    int source_channels_ = 2;
    
    bool audio_initialized_ = false;
    int nalu_length_size_ = 4;
    
    // Timestamp passthrough - single shared base timestamp for A/V sync
    uint32_t base_timestamp_ = 0;  // Single shared base for A/V sync
    bool base_timestamp_set_ = false;
    bool publishing_started_ = false;
    
    // Frame count for statistics only (not used for timestamp calculation)
    uint64_t video_frame_count_ = 0;
    uint32_t actual_audio_sample_rate_ = 44100;
};

}
