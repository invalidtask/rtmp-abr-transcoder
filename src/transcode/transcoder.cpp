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
    
    // Only log if we have actual dimensions (not placeholder 0x0)
    if (width > 0 && height > 0) {
        Logger::info("Source stream: ", width, "x", height, " @ ", fps, "fps, H.264/AAC");
    }
    
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
        if (size >= 5) {
            parse_avc_decoder_config(data + 5, size - 5);
        }
        return;
    } else if (avc_packet_type == 1) {
        // NALU data in AVCC format (length-prefixed)
        if (size < 5) {
            return;
        }
        
        // Convert AVCC to Annex-B format
        std::vector<uint8_t> annexb_data;
        const uint8_t* avcc_data = data + 5;
        size_t avcc_size = size - 5;
        size_t offset = 0;
        
        while (offset + nalu_length_size_ <= avcc_size) {
            uint32_t nalu_len = 0;
            for (int i = 0; i < nalu_length_size_; i++) {
                nalu_len = (nalu_len << 8) | avcc_data[offset + i];
            }
            offset += nalu_length_size_;
            
            if (offset + nalu_len > avcc_size) break;
            
            // Add Annex-B startcode
            annexb_data.push_back(0x00);
            annexb_data.push_back(0x00);
            annexb_data.push_back(0x00);
            annexb_data.push_back(0x01);
            
            // Add NALU data
            annexb_data.insert(annexb_data.end(), 
                              avcc_data + offset, 
                              avcc_data + offset + nalu_len);
            offset += nalu_len;
        }
        
        // Decode Annex-B data
        std::vector<VideoFrame> frames;
        if (video_decoder_->decode(annexb_data.data(), annexb_data.size(), timestamp, frames)) {
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
            Logger::info("AAC decoder initialized with ASC");
            audio_initialized_ = true;
        }
        return;
    } else if (aac_packet_type == 1) {
        // Raw AAC data - only decode if decoder is initialized
        if (!audio_initialized_) {
            Logger::debug("Skipping AAC frame - decoder not initialized");
            return;
        }
        
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
    // Update source dimensions from actual decoded frame
    if (source_width_ != frame.width || source_height_ != frame.height) {
        source_width_ = frame.width;
        source_height_ = frame.height;
        Logger::info("Detected source resolution: ", source_width_, "x", source_height_);
    }
    
    Logger::debug("Processing video frame: ", frame.width, "x", frame.height);
    
    for (auto& output : outputs_) {
        // Initialize encoders on first frame
        if (!output->video_initialized) {
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
            
            Logger::info("Scaler initialized: ", frame.width, "x", frame.height, 
                        " -> ", output->config.width, "x", output->config.height);
            Logger::info("H264 encoder initialized: ", output->config.width, "x", output->config.height,
                        " @ ", output->config.video_bitrate_kbps, "kbps");
            
            output->video_initialized = true;
        }
        
        // Scale the frame
        VideoFrame scaled_frame;
        if (!output->scaler->scale(frame, scaled_frame)) {
            Logger::error("Scaling failed for ", output->config.name);
            continue;
        }
        
        // Encode the scaled frame
        std::vector<EncodedPacket> packets;
        if (!output->video_encoder->encode(scaled_frame, packets)) {
            Logger::error("Encoding failed for ", output->config.name);
            continue;
        }
        
        // Push each encoded packet
        for (auto& packet : packets) {
            Logger::debug("Encoded ", output->config.name, ": ", packet.data.size(), 
                         " bytes, keyframe=", packet.keyframe);
            push_video_packet(*output, packet);
        }
    }
}

void Transcoder::process_audio_frame(const AudioFrame& frame) {
    Logger::debug("Processing audio frame: ", frame.sample_rate, "Hz, ", frame.channels, " ch");
    
    for (auto& output : outputs_) {
        // Initialize audio encoder on first frame
        if (!output->audio_initialized) {
            if (!output->audio_encoder->initialize(frame.sample_rate, frame.channels, 
                                                   output->config.audio_bitrate_kbps)) {
                Logger::error("Failed to initialize audio encoder for ", output->config.name);
                continue;
            }
            Logger::info("AAC encoder initialized for ", output->config.name);
            output->audio_initialized = true;
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
        // Buffer while waiting for connection
        output.pending_video.push_back(packet);
        if (output.pending_video.size() > 300) {  // ~10 seconds at 30fps
            output.pending_video.erase(output.pending_video.begin());
        }
        Logger::debug("Buffering video packet for ", output.config.name, 
                     " (not yet publishing), buffer size: ", output.pending_video.size());
        return;
    }
    
    // Build and send FLV video tag
    rtmp::Message msg;
    msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Video);
    msg.timestamp = packet.timestamp;
    msg.stream_id = output.stream_id;
    msg.payload = build_flv_video_packet(packet);
    
    output.pusher->send_message(msg);
    
    Logger::debug("Pushed video to ", output.config.name);
}

std::vector<uint8_t> Transcoder::build_flv_video_packet(const EncodedPacket& packet) {
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
    
    return flv_data;
}

void Transcoder::push_audio_packet(Output& output, const EncodedPacket& packet) {
    if (!output.publishing) {
        // Buffer while waiting for connection
        output.pending_audio.push_back(packet);
        if (output.pending_audio.size() > 1500) {  // ~10 seconds at 150 frames/sec
            output.pending_audio.erase(output.pending_audio.begin());
        }
        Logger::debug("Buffering audio packet for ", output.config.name, 
                     " (not yet publishing), buffer size: ", output.pending_audio.size());
        return;
    }
    
    // Build and send FLV audio tag
    rtmp::Message msg;
    msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Audio);
    msg.timestamp = packet.timestamp;
    msg.stream_id = output.stream_id;
    msg.payload = build_flv_audio_packet(packet);
    
    output.pusher->send_message(msg);
    
    Logger::debug("Pushed audio to ", output.config.name);
}

std::vector<uint8_t> Transcoder::build_flv_audio_packet(const EncodedPacket& packet) {
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
    
    return flv_data;
}

bool Transcoder::start() {
    // Create pusher connections immediately, not on first frame
    for (auto& output : outputs_) {
        output->pusher = std::make_unique<rtmp::Client>(loop_);
        setup_pusher_callbacks(output.get());
        
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
                Logger::info("Connecting output ", output->config.name, " to ", url);
                output->pusher->connect(host, port);
            } else {
                Logger::error("Failed to parse URL for ", output->config.name, ": ", addr_result.error());
            }
        } else {
            Logger::error("Invalid URL format for ", output->config.name, ": ", url);
        }
    }
    
    Logger::info("Transcoder started");
    return true;
}

void Transcoder::stop() {
    Logger::info("Transcoder stopped");
    outputs_.clear();
}

void Transcoder::setup_pusher_callbacks(Output* output) {
    output->pusher->set_connected_callback([this, out = output]() {
        Logger::info("Output connected: ", out->config.name);
        out->connected = true;
        
        // Parse app and tcUrl from rtmp_url
        std::string url = out->config.rtmp_url;
        size_t proto_end = url.find("://");
        if (proto_end == std::string::npos) {
            Logger::error("Invalid URL format (missing ://): ", url);
            return;
        }
        
        size_t host_start = proto_end + 3;
        size_t path_start = url.find('/', host_start);
        if (path_start == std::string::npos) {
            Logger::error("Invalid URL format (missing path): ", url);
            return;
        }
        
        std::string path = url.substr(path_start);
        
        size_t app_end = path.find('/', 1);
        std::string app = app_end != std::string::npos ? path.substr(1, app_end - 1) : path.substr(1);
        
        if (app.empty()) {
            Logger::error("Invalid URL format (missing app): ", url);
            return;
        }
        
        // Build tcUrl (rtmp://host:port/app)
        std::string tcUrl = url.substr(0, path_start + app.size() + 1);
        
        // Send connect command
        rtmp::CommandMessage connect_cmd;
        connect_cmd.name = "connect";
        connect_cmd.transaction_id = 1;
        
        amf0::Value::ObjectType connect_obj;
        connect_obj["app"] = amf0::Value::String(app);
        connect_obj["type"] = amf0::Value::String("nonprivate");
        connect_obj["tcUrl"] = amf0::Value::String(tcUrl);
        connect_cmd.arguments.push_back(amf0::Value::Object(connect_obj));
        
        out->pusher->send_command(connect_cmd);
        out->pusher->flush();
    });
    
    output->pusher->set_message_callback([this, out = output](const rtmp::Message& msg) {
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
                    // CreateStream succeeded, extract stream_id and send publish
                    // The stream_id is returned as the second argument (after null)
                    if (cmd->arguments.size() >= 2 && cmd->arguments[1].is_number()) {
                        out->stream_id = static_cast<uint32_t>(cmd->arguments[1].as_number());
                        Logger::debug("Received stream_id: ", out->stream_id);
                    } else {
                        Logger::error("Invalid createStream response - no stream_id");
                        return;
                    }
                    
                    // Extract stream name from URL (last path component)
                    std::string url = out->config.rtmp_url;
                    size_t last_slash = url.rfind('/');
                    std::string stream_name = (last_slash != std::string::npos) ? 
                                               url.substr(last_slash + 1) : "stream";
                    
                    // Build publish command
                    rtmp::CommandMessage publish_cmd;
                    publish_cmd.name = "publish";
                    publish_cmd.transaction_id = 0;
                    publish_cmd.arguments.push_back(amf0::Value::Null());
                    publish_cmd.arguments.push_back(amf0::Value::String(stream_name));
                    publish_cmd.arguments.push_back(amf0::Value::String("live"));
                    
                    // Send publish command on the correct stream_id
                    rtmp::Message publish_msg;
                    publish_msg.type_id = static_cast<uint8_t>(rtmp::MessageType::CommandAMF0);
                    publish_msg.timestamp = 0;
                    publish_msg.stream_id = out->stream_id;
                    publish_msg.payload = publish_cmd.encode();
                    
                    out->pusher->send_message(publish_msg);
                    out->pusher->flush();
                    
                    Logger::debug("Sent publish command on stream_id: ", out->stream_id);
                }
            } else if (cmd && cmd->name == "onStatus") {
                // Handle onStatus response to confirm publish success
                if (cmd->arguments.size() >= 2 && cmd->arguments[1].is_object()) {
                    auto info = cmd->arguments[1].as_object();
                    auto code_it = info.find("code");
                    if (code_it != info.end() && code_it->second.is_string()) {
                        std::string code = code_it->second.as_string();
                        Logger::debug("Received onStatus: ", code);
                        
                        if (code == "NetStream.Publish.Start") {
                            Logger::info("Publish confirmed for ", out->config.name);
                            // Mark as ready and flush pending packets
                            on_publish_ready(out);
                        }
                    }
                }
            }
        }
    });
    
    output->pusher->set_disconnected_callback([this, out = output]() {
        Logger::warn("Output disconnected: ", out->config.name);
        out->connected = false;
        out->publishing = false;
    });
}

void Transcoder::on_publish_ready(Output* output) {
    output->publishing = true;
    
    Logger::info("Output ", output->config.name, " ready, flushing ", 
                 output->pending_video.size(), " video + ", 
                 output->pending_audio.size(), " audio buffered packets");
    
    // Flush pending video packets
    for (auto& packet : output->pending_video) {
        rtmp::Message msg;
        msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Video);
        msg.timestamp = packet.timestamp;
        msg.stream_id = output->stream_id;
        msg.payload = build_flv_video_packet(packet);
        
        output->pusher->send_message(msg);
    }
    output->pending_video.clear();
    
    // Flush pending audio packets
    for (auto& packet : output->pending_audio) {
        rtmp::Message msg;
        msg.type_id = static_cast<uint8_t>(rtmp::MessageType::Audio);
        msg.timestamp = packet.timestamp;
        msg.stream_id = output->stream_id;
        msg.payload = build_flv_audio_packet(packet);
        
        output->pusher->send_message(msg);
    }
    output->pending_audio.clear();
}

void Transcoder::parse_avc_decoder_config(const uint8_t* data, size_t size) {
    if (size < 7) {
        return;
    }
    
    // AVCDecoderConfigurationRecord format:
    // byte 0: configurationVersion
    // byte 1: AVCProfileIndication
    // byte 2: profile_compatibility
    // byte 3: AVCLevelIndication
    // byte 4: (lengthSizeMinusOne & 0x03) | 0xFC
    // byte 5: (numOfSPS & 0x1F) | 0xE0
    // bytes 6-7: SPS length, followed by SPS
    
    nalu_length_size_ = (data[4] & 0x03) + 1;
    
    uint8_t num_sps = data[5] & 0x1F;
    size_t offset = 6;
    
    // Extract and feed SPS to decoder
    for (size_t i = 0; i < num_sps && offset + 2 <= size; i++) {
        uint16_t sps_len = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        if (offset + sps_len <= size) {
            // Feed SPS as Annex-B format
            std::vector<uint8_t> sps_annexb = {0x00, 0x00, 0x00, 0x01};
            sps_annexb.insert(sps_annexb.end(), data + offset, data + offset + sps_len);
            
            std::vector<VideoFrame> dummy;
            video_decoder_->decode(sps_annexb.data(), sps_annexb.size(), 0, dummy);
            
            offset += sps_len;
        }
    }
    
    // Extract and feed PPS similarly
    if (offset < size) {
        uint8_t num_pps = data[offset++];
        for (size_t i = 0; i < num_pps && offset + 2 <= size; i++) {
            uint16_t pps_len = (data[offset] << 8) | data[offset + 1];
            offset += 2;
            
            if (offset + pps_len <= size) {
                std::vector<uint8_t> pps_annexb = {0x00, 0x00, 0x00, 0x01};
                pps_annexb.insert(pps_annexb.end(), data + offset, data + offset + pps_len);
                
                std::vector<VideoFrame> dummy;
                video_decoder_->decode(pps_annexb.data(), pps_annexb.size(), 0, dummy);
                
                offset += pps_len;
            }
        }
    }
    
    Logger::info("Parsed AVC decoder config, NALU length size: ", nalu_length_size_);
}

}
