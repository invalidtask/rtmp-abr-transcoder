#include "transcode/transcoder.hpp"
#include "core/log.hpp"
#include "rtmp/rtmp_messages.hpp"
#include "net/socket.hpp"
#include <algorithm>

namespace transcode {

Transcoder::Transcoder(EpollLoop& loop)
    : loop_(loop),
      video_decoder_(std::make_unique<H264Decoder>()),
      audio_decoder_(std::make_unique<AACDecoder>()) {
}

Transcoder::~Transcoder() {
    stop();
}

bool Transcoder::add_output(const OutputConfig& config) {
    auto output = std::make_unique<Output>();
    output->config = config;
    output->scaler = std::make_unique<Scaler>();
    output->video_encoder = std::make_unique<H264Encoder>();
    output->audio_encoder = std::make_unique<AACEncoder>();
    
    outputs_.push_back(std::move(output));
    
    Logger::info("Added output: ", config.name, " (", config.width, "x", config.height, 
                 " @ ", config.video_bitrate_kbps, " kbps)");
    return true;
}

void Transcoder::on_source_metadata(int width, int height, int fps, int sample_rate, int channels) {
    source_width_ = width;
    source_height_ = height;
    source_fps_ = fps;
    source_sample_rate_ = sample_rate;
    source_channels_ = channels;
    
    Logger::info("Source stream: ", width, "x", height, " @ ", fps, "fps, H.264/AAC");
    
    // Initialize decoders
    if (!video_decoder_->initialize()) {
        Logger::error("Failed to initialize video decoder");
        return;
    }
    
    // Audio decoder will be initialized when we get audio config
}

void Transcoder::on_video_data(const uint8_t* data, size_t size, uint32_t timestamp, bool keyframe) {
    if (size < 5) {
        return;  // Too small to be valid
    }
    
    // FLV video data format:
    // byte 0: frame type (4 bits) + codec id (4 bits)
    // byte 1: AVC packet type (0=sequence header, 1=NALU, 2=end of sequence)
    // bytes 2-4: composition time (24 bits)
    
    uint8_t frame_type = (data[0] >> 4) & 0x0F;
    uint8_t codec_id = data[0] & 0x0F;
    
    if (codec_id != 7) {  // AVC/H.264
        Logger::warn("Unsupported video codec: ", static_cast<int>(codec_id));
        return;
    }
    
    if (size < 2) {
        return;
    }
    
    uint8_t avc_packet_type = data[1];
    
    if (avc_packet_type == 0) {
        // Sequence header (contains SPS/PPS)
        Logger::debug("Received AVC sequence header");
        // We'll use the decoder to extract config
        return;
    } else if (avc_packet_type == 1) {
        // NALU data
        if (size < 5) {
            return;
        }
        
        const uint8_t* nalu_data = data + 5;
        size_t nalu_size = size - 5;
        
        // Decode the frame
        std::vector<VideoFrame> frames;
        if (video_decoder_->decode(nalu_data, nalu_size, timestamp, frames)) {
            for (auto& frame : frames) {
                process_video_frame(frame);
            }
        }
    }
}

void Transcoder::on_audio_data(const uint8_t* data, size_t size, uint32_t timestamp) {
    if (size < 2) {
        return;
    }
    
    // FLV audio data format:
    // byte 0: sound format (4 bits) + sound rate (2 bits) + sound size (1 bit) + sound type (1 bit)
    // byte 1: AAC packet type (0=sequence header, 1=raw AAC data)
    
    uint8_t sound_format = (data[0] >> 4) & 0x0F;
    
    if (sound_format != 10) {  // AAC
        Logger::warn("Unsupported audio codec: ", static_cast<int>(sound_format));
        return;
    }
    
    uint8_t aac_packet_type = data[1];
    
    if (aac_packet_type == 0) {
        // Audio Specific Config
        if (size < 4) {
            return;
        }
        const uint8_t* asc_data = data + 2;
        size_t asc_size = size - 2;
        
        if (!audio_decoder_->initialize(asc_data, asc_size)) {
            Logger::error("Failed to initialize audio decoder with ASC");
        } else {
            Logger::debug("Audio decoder configured with ASC");
        }
        return;
    } else if (aac_packet_type == 1) {
        // Raw AAC data
        if (size < 3) {
            return;
        }
        
        const uint8_t* aac_data = data + 2;
        size_t aac_size = size - 2;
        
        std::vector<AudioFrame> frames;
        if (audio_decoder_->decode(aac_data, aac_size, timestamp, frames)) {
            for (auto& frame : frames) {
                process_audio_frame(frame);
            }
        }
    }
}

void Transcoder::process_video_frame(const VideoFrame& frame) {
    Logger::debug("Processing video frame: ", frame.width, "x", frame.height);
    
    for (auto& output : outputs_) {
        // Initialize encoders on first frame
        if (!output->video_encoder->get_sps().empty()) {
            // Already initialized
        } else {
            EncoderConfig enc_config;
            enc_config.width = output->config.width;
            enc_config.height = output->config.height;
            enc_config.bitrate_kbps = output->config.video_bitrate_kbps;
            enc_config.fps = source_fps_;
            enc_config.gop_size = source_fps_ * 2;  // 2 second GOP
            enc_config.preset = "fast";
            
            if (!output->video_encoder->initialize(enc_config)) {
                Logger::error("Failed to initialize video encoder for ", output->config.name);
                continue;
            }
            
            // Initialize scaler
            if (!output->scaler->initialize(frame.width, frame.height, 
                                           output->config.width, output->config.height)) {
                Logger::error("Failed to initialize scaler for ", output->config.name);
                continue;
            }
            
            // Start RTMP pusher
            output->pusher = std::make_unique<rtmp::Client>(loop_);
            
            // Setup callbacks
            output->pusher->set_connected_callback([this, out = output.get()]() {
                Logger::info("Output connected: ", out->config.name);
                out->connected = true;
                
                // Send connect command
                rtmp::CommandMessage connect_cmd;
                connect_cmd.name = "connect";
                connect_cmd.transaction_id = 1;
                
                amf0::Value::ObjectType connect_obj;
                connect_obj["app"] = amf0::Value::String("live");
                connect_obj["type"] = amf0::Value::String("nonprivate");
                connect_cmd.arguments.push_back(amf0::Value::Object(connect_obj));
                
                out->pusher->send_command(connect_cmd);
                out->pusher->flush();
            });
            
            output->pusher->set_message_callback([this, out = output.get()](const rtmp::Message& msg) {
                if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::CommandAMF0)) {
                    auto cmd = rtmp::CommandMessage::parse(msg.payload);
                    if (cmd && cmd->name == "_result") {
                        if (cmd->transaction_id == 1) {
                            // Connect succeeded, send createStream
                            rtmp::CommandMessage create_stream;
                            create_stream.name = "createStream";
                            create_stream.transaction_id = 2;
                            create_stream.arguments.push_back(amf0::Value::Null());
                            
                            out->pusher->send_command(create_stream);
                            out->pusher->flush();
                        } else if (cmd->transaction_id == 2) {
                            // CreateStream succeeded, send publish
                            rtmp::CommandMessage publish_cmd;
                            publish_cmd.name = "publish";
                            publish_cmd.transaction_id = 0;
                            publish_cmd.arguments.push_back(amf0::Value::Null());
                            publish_cmd.arguments.push_back(amf0::Value::String("stream"));
                            publish_cmd.arguments.push_back(amf0::Value::String("live"));
                            
                            out->pusher->send_command(publish_cmd);
                            out->pusher->flush();
                            out->publishing = true;
                            
                            Logger::info("Output publishing: ", out->config.name);
                        }
                    }
                }
            });
            
            output->pusher->set_disconnected_callback([this, out = output.get()]() {
                Logger::warn("Output disconnected: ", out->config.name);
                out->connected = false;
                out->publishing = false;
            });
            
            // Parse URL and connect
            std::string url = output->config.rtmp_url;
            size_t proto_end = url.find("://");
            if (proto_end != std::string::npos) {
                size_t host_start = proto_end + 3;
                size_t path_start = url.find('/', host_start);
                
                std::string host_port = url.substr(host_start, path_start - host_start);
                auto addr_result = Socket::parse_address(host_port);
                
                if (addr_result.is_ok()) {
                    auto [host, port] = addr_result.value();
                    Logger::info("Transcoding to ", output->config.name, 
                                 "@", output->config.video_bitrate_kbps, "kbps -> ", url);
                    output->pusher->connect(host, port);
                } else {
                    Logger::error("Failed to parse URL for ", output->config.name);
                }
            }
        }
        
        // Scale and encode
        VideoFrame scaled_frame;
        if (output->scaler->scale(frame, scaled_frame)) {
            std::vector<EncodedPacket> packets;
            if (output->video_encoder->encode(scaled_frame, packets)) {
                for (auto& packet : packets) {
                    push_video_packet(*output, packet);
                }
            }
        }
    }
}

void Transcoder::process_audio_frame(const AudioFrame& frame) {
    Logger::debug("Processing audio frame: ", frame.sample_rate, "Hz, ", frame.channels, " ch");
    
    for (auto& output : outputs_) {
        // Initialize audio encoder on first frame
        if (output->audio_encoder->get_audio_specific_config().empty()) {
            if (!output->audio_encoder->initialize(frame.sample_rate, frame.channels, 
                                                   output->config.audio_bitrate_kbps)) {
                Logger::error("Failed to initialize audio encoder for ", output->config.name);
                continue;
            }
        }
        
        // Encode
        std::vector<EncodedPacket> packets;
        if (output->audio_encoder->encode(frame, packets)) {
            for (auto& packet : packets) {
                push_audio_packet(*output, packet);
            }
        }
    }
}

void Transcoder::push_video_packet(Output& output, const EncodedPacket& packet) {
    if (!output.publishing) {
        return;
    }
    
    // Build FLV video tag
    std::vector<uint8_t> flv_data;
    flv_data.reserve(5 + packet.data.size());
    
    // Byte 0: frame type + codec id
    uint8_t frame_type = packet.keyframe ? 0x10 : 0x20;  // 1=keyframe, 2=inter
    uint8_t codec_id = 0x07;  // AVC
    flv_data.push_back(frame_type | codec_id);
    
    // Byte 1: AVC packet type (1 = NALU)
    flv_data.push_back(0x01);
    
    // Bytes 2-4: composition time (0 for now)
    flv_data.push_back(0x00);
    flv_data.push_back(0x00);
    flv_data.push_back(0x00);
    
    // Append NALU data
    flv_data.insert(flv_data.end(), packet.data.begin(), packet.data.end());
    
    // Send as RTMP message
    rtmp::Message msg;
    msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Video);
    msg.timestamp = packet.timestamp;
    msg.stream_id = 1;
    msg.payload = std::move(flv_data);
    
    output.pusher->send_message(msg);
    
    Logger::debug("Pushed to ", output.config.name, " output");
}

void Transcoder::push_audio_packet(Output& output, const EncodedPacket& packet) {
    if (!output.publishing) {
        return;
    }
    
    // Build FLV audio tag
    std::vector<uint8_t> flv_data;
    flv_data.reserve(2 + packet.data.size());
    
    // Byte 0: sound format + rate + size + type
    uint8_t sound_format = 0xA0;  // AAC
    uint8_t sound_rate = 0x03;    // 44kHz
    uint8_t sound_size = 0x01;    // 16-bit
    uint8_t sound_type = 0x01;    // Stereo
    flv_data.push_back(sound_format | (sound_rate << 2) | (sound_size << 1) | sound_type);
    
    // Byte 1: AAC packet type (1 = raw AAC)
    flv_data.push_back(0x01);
    
    // Append AAC data
    flv_data.insert(flv_data.end(), packet.data.begin(), packet.data.end());
    
    // Send as RTMP message
    rtmp::Message msg;
    msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Audio);
    msg.timestamp = packet.timestamp;
    msg.stream_id = 1;
    msg.payload = std::move(flv_data);
    
    output.pusher->send_message(msg);
}

bool Transcoder::start() {
    Logger::info("Transcoder started");
    return true;
}

void Transcoder::stop() {
    Logger::info("Transcoder stopped");
    outputs_.clear();
}

}
