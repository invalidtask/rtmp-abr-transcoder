#include "relay/relay_manager.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include <sys/epoll.h>
#include <algorithm>
#include <sstream>

namespace relay {

RelayManager::RelayManager(EpollLoop& loop, const RelayPolicy& policy)
    : loop_(loop), policy_(policy), push_template_("{app}/{stream}") {}

void RelayManager::set_push_url(const std::string& url) {
    push_url_ = url;
}

void RelayManager::set_push_template(const std::string& tmpl) {
    push_template_ = tmpl;
}

Result<void> RelayManager::start_server(const std::string& addr, uint16_t port) {
    server_ = std::make_unique<rtmp::Server>(loop_);
    
    server_->set_connection_callback([this](std::shared_ptr<rtmp::Session> session, Socket socket) {
        handle_new_publisher(session, std::move(socket));
    });
    
    return server_->start(addr, port);
}

void RelayManager::handle_new_publisher(std::shared_ptr<rtmp::Session> session, Socket socket) {
    auto publisher = std::make_unique<Publisher>();
    publisher->session = session;
    publisher->socket = std::move(socket);
    publisher->ready = false;
    
    Publisher* pub_ptr = publisher.get();
    publishers_[pub_ptr] = std::move(publisher);
    
    session->set_message_callback([this, pub_ptr](const rtmp::Message& msg) {
        handle_publisher_message(pub_ptr, msg);
    });
    
    session->set_close_callback([this, pub_ptr]() {
        remove_publisher(pub_ptr);
    });
    
    setup_publisher_read(pub_ptr);
}

void RelayManager::setup_publisher_read(Publisher* pub) {
    loop_.add(pub->socket.fd(), EPOLLIN, [this, pub](uint32_t events) {
        if (events & (EPOLLERR | EPOLLHUP)) {
            remove_publisher(pub);
            return;
        }
        
        if (events & EPOLLIN) {
            uint8_t buffer[8192];
            auto read_result = pub->socket.read(buffer, sizeof(buffer));
            
            if (read_result.is_err()) {
                Logger::error("Publisher read error: ", read_result.error());
                remove_publisher(pub);
                return;
            }
            
            if (read_result.value() == 0) {
                Logger::info("Publisher disconnected");
                remove_publisher(pub);
                return;
            }
            
            Logger::debug("Read ", read_result.value(), " bytes from publisher");
            
            if (!pub->session->handshake_done()) {
                auto& hs = pub->session->handshake();
                Logger::debug("Handshake state before: ", static_cast<int>(hs.state()));
                
                auto handshake_result = hs.process_client_handshake(
                    std::span<const uint8_t>(buffer, read_result.value())
                );
                
                if (handshake_result.is_err()) {
                    Logger::error("Handshake failed: ", handshake_result.error());
                    remove_publisher(pub);
                    return;
                }
                
                size_t consumed = handshake_result.value();
                Logger::debug("Handshake state after: ", static_cast<int>(hs.state()), 
                             ", consumed: ", consumed, " bytes, total read: ", read_result.value());
                
                // After receiving C0+C1, send S0+S1+S2
                // When state is S0S1S2Sent, process_client_handshake() has validated that
                // the input span had at least 1537 bytes (C0+C1). Double-check for safety.
                if (hs.state() == rtmp::Handshake::State::S0S1S2Sent && read_result.value() >= 1537) {
                    auto response = pub->session->generate_server_handshake_response(
                        std::span<const uint8_t>(buffer + 1, 1536)
                    );
                    auto write_result = pub->socket.write(response.data(), response.size());
                    if (write_result.is_err()) {
                        Logger::error("Failed to write handshake response: ", write_result.error());
                        remove_publisher(pub);
                        return;
                    }
                }
                
                // Only send protocol messages AFTER handshake is fully complete
                if (hs.is_done()) {
                    Logger::debug("Handshake complete, sending protocol messages");
                    pub->session->send_window_ack_size(2500000);
                    pub->session->send_set_peer_bandwidth(2500000, 2);
                    pub->session->send_set_chunk_size(4096);
                    
                    // Flush the protocol messages immediately
                    auto data = pub->session->get_outgoing_data();
                    if (!data.empty()) {
                        Logger::debug("Flushing ", data.size(), " bytes of protocol messages to socket");
                        auto write_result = pub->socket.write(data.data(), data.size());
                        if (write_result.is_err()) {
                            Logger::error("Failed to write protocol messages: ", write_result.error());
                            remove_publisher(pub);
                            return;
                        }
                        Logger::debug("Wrote ", write_result.value(), " bytes of protocol messages");
                    }
                }
                
                // Process any remaining data after handshake
                if (hs.is_done() && consumed < read_result.value()) {
                    auto process_result = pub->session->process_input(
                        std::span<const uint8_t>(buffer + consumed, read_result.value() - consumed)
                    );
                    if (process_result.is_err()) {
                        Logger::error("Failed to process input after handshake: ", process_result.error());
                        remove_publisher(pub);
                        return;
                    }
                }
                
                return;  // Don't process RTMP messages until handshake is done
            }
            
            // Normal message processing after handshake complete
            auto process_result = pub->session->process_input(
                std::span<const uint8_t>(buffer, read_result.value())
            );
            if (process_result.is_err()) {
                Logger::error("Failed to process input: ", process_result.error());
                remove_publisher(pub);
                return;
            }
        }
    });
}

void RelayManager::handle_publisher_message(Publisher* pub, const rtmp::Message& msg) {
    if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::CommandAMF0)) {
        auto cmd = rtmp::CommandMessage::parse(msg.payload);
        if (cmd) {
            handle_publisher_command(pub, *cmd);
        }
    }
    else if (pub->ready) {
        // Track statistics and log media messages
        if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::Audio)) {
            pub->stats.audio_messages++;
            pub->stats.audio_bytes += msg.payload.size();
            Logger::debug("Received audio message, timestamp: ", msg.timestamp, 
                          ", size: ", msg.payload.size());
        }
        else if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::Video)) {
            pub->stats.video_messages++;
            pub->stats.video_bytes += msg.payload.size();
            bool is_keyframe = !msg.payload.empty() && (msg.payload[0] & 0xF0) == 0x10;
            if (is_keyframe) {
                pub->stats.keyframes++;
            }
            Logger::debug("Received video message, timestamp: ", msg.timestamp,
                          ", size: ", msg.payload.size(),
                          ", keyframe: ", is_keyframe);
        }
        else if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::DataAMF0)) {
            pub->stats.data_messages++;
            Logger::debug("Received data message, size: ", msg.payload.size());
        }
        
        // Log stats periodically (every 5 seconds)
        uint64_t now = time_util::now_ms();
        if (now - pub->last_stats_log >= 5000) {
            log_stats(pub);
            pub->last_stats_log = now;
        }
        
        auto it = pushers_.find(pub->stream_id);
        if (it != pushers_.end()) {
            relay_message(it->second.get(), msg);
        }
    }
}

void RelayManager::handle_publisher_command(Publisher* pub, const rtmp::CommandMessage& cmd) {
    Logger::debug("Publisher command: ", cmd.name, 
                  ", txn_id: ", cmd.transaction_id,
                  ", args: ", cmd.arguments.size());
    
    if (cmd.name == "connect") {
        std::string app_name;
        if (!cmd.arguments.empty() && cmd.arguments[0].is_object()) {
            auto& obj = cmd.arguments[0].as_object();
            if (obj.count("app") && obj.at("app").is_string()) {
                app_name = obj.at("app").as_string();
            }
        }
        
        pub->stream_id.app = app_name;
        
        rtmp::CommandMessage response;
        response.name = "_result";
        response.transaction_id = cmd.transaction_id;
        
        amf0::Value::ObjectType props;
        props["fmsVer"] = amf0::Value::String("FMS/3,0,1,123");
        props["capabilities"] = amf0::Value::Number(31);
        response.arguments.push_back(amf0::Value::Object(props));
        
        amf0::Value::ObjectType info;
        info["level"] = amf0::Value::String("status");
        info["code"] = amf0::Value::String("NetConnection.Connect.Success");
        info["description"] = amf0::Value::String("Connection succeeded");
        info["objectEncoding"] = amf0::Value::Number(0);
        response.arguments.push_back(amf0::Value::Object(info));
        
        pub->session->send_command(response);
        Logger::debug("Sending _result for connect, txn_id: ", cmd.transaction_id);
        flush_publisher_responses(pub);
    }
    else if (cmd.name == "createStream") {
        rtmp::CommandMessage response;
        response.name = "_result";
        response.transaction_id = cmd.transaction_id;
        response.arguments.push_back(amf0::Value::Null());
        response.arguments.push_back(amf0::Value::Number(1));
        
        pub->session->send_command(response);
        Logger::debug("Sending _result for createStream, txn_id: ", cmd.transaction_id);
        flush_publisher_responses(pub);
    }
    else if (cmd.name == "publish") {
        if (!cmd.arguments.empty() && cmd.arguments[0].is_null() && 
            cmd.arguments.size() > 1 && cmd.arguments[1].is_string()) {
            
            pub->stream_id.stream = cmd.arguments[1].as_string();
            pub->ready = true;
            pub->stats.start_time = time_util::now_ms();
            pub->last_stats_log = pub->stats.start_time;
            
            Logger::info("Publisher started: ", pub->stream_id.to_string());
            
            rtmp::CommandMessage response;
            response.name = "onStatus";
            response.transaction_id = 0;
            response.arguments.push_back(amf0::Value::Null());
            
            amf0::Value::ObjectType info;
            info["level"] = amf0::Value::String("status");
            info["code"] = amf0::Value::String("NetStream.Publish.Start");
            info["description"] = amf0::Value::String("Stream is now published");
            response.arguments.push_back(amf0::Value::Object(info));
            
            pub->session->send_command(response);
            Logger::debug("Sending onStatus for publish");
            flush_publisher_responses(pub);
            
            create_pusher(pub->stream_id);
        }
    }
}

void RelayManager::remove_publisher(Publisher* pub) {
    if (publishers_.count(pub)) {
        loop_.remove(pub->socket.fd());
        publishers_.erase(pub);
    }
}

void RelayManager::log_stats(Publisher* pub) {
    uint64_t elapsed = time_util::now_ms() - pub->stats.start_time;
    double elapsed_sec = elapsed / 1000.0;
    if (elapsed_sec > 0) {
        double bitrate = (pub->stats.audio_bytes + pub->stats.video_bytes) * 8 / elapsed_sec / 1000.0;
        Logger::info("Stats: audio=", pub->stats.audio_messages, 
                     " video=", pub->stats.video_messages,
                     " (", pub->stats.keyframes, " keyframes)",
                     " data=", pub->stats.data_messages,
                     " total_bytes=", pub->stats.audio_bytes + pub->stats.video_bytes,
                     " duration=", elapsed_sec, "s",
                     " bitrate=", bitrate, " kbps");
    }
}

void RelayManager::flush_publisher_responses(Publisher* pub) {
    auto data = pub->session->get_outgoing_data();
    if (!data.empty()) {
        Logger::debug("Flushing ", data.size(), " bytes to publisher socket");
        auto write_result = pub->socket.write(data.data(), data.size());
        if (write_result.is_err()) {
            Logger::error("Write failed: ", write_result.error());
        } else {
            Logger::debug("Wrote ", write_result.value(), " bytes to publisher");
        }
    }
}

void RelayManager::create_pusher(const StreamId& stream_id) {
    Logger::info("Creating pusher for stream: ", stream_id.to_string());
    
    if (push_url_.empty()) {
        Logger::warn("No push URL configured, running in sink mode");
        return;
    }
    
    auto pusher = std::make_unique<Pusher>();
    pusher->stream_id = stream_id;
    pusher->client = std::make_unique<rtmp::Client>(loop_);
    
    Pusher* pusher_ptr = pusher.get();
    
    pusher->client->set_connected_callback([this, pusher_ptr]() {
        Logger::info("Pusher connected_callback fired for ", pusher_ptr->stream_id.to_string());
        handle_pusher_connected(pusher_ptr);
    });
    
    pusher->client->set_message_callback([this, pusher_ptr](const rtmp::Message& msg) {
        handle_pusher_message(pusher_ptr, msg);
    });
    
    pusher->client->set_disconnected_callback([this, pusher_ptr]() {
        handle_pusher_disconnected(pusher_ptr);
    });
    
    std::string url = build_push_url(stream_id);
    
    size_t proto_end = url.find("://");
    if (proto_end == std::string::npos) {
        Logger::error("Invalid push URL: ", url);
        return;
    }
    
    size_t host_start = proto_end + 3;
    size_t path_start = url.find('/', host_start);
    
    std::string host_port = url.substr(host_start, path_start - host_start);
    auto addr_result = Socket::parse_address(host_port);
    
    if (addr_result.is_err()) {
        Logger::error("Failed to parse push URL: ", addr_result.error());
        return;
    }
    
    auto [host, port] = addr_result.value();
    
    Logger::info("Connecting pusher to ", host, ":", port);
    
    auto connect_result = pusher->client->connect(host, port);
    if (connect_result.is_err()) {
        Logger::error("Pusher initial connect() failed: ", connect_result.error());
        // Initial connection failure - pusher will remain disconnected
        // Reconnection will be handled via disconnected_callback if connection drops later
    } else {
        Logger::debug("Pusher connect() initiated successfully");
    }
    
    pushers_[stream_id] = std::move(pusher);
    Logger::debug("Pusher added to map, total pushers: ", pushers_.size());
}

void RelayManager::handle_pusher_connected(Pusher* pusher) {
    Logger::info("Pusher connected for ", pusher->stream_id.to_string());
    
    pusher->connected = true;
    pusher->reconnect_delay_ms = 100;
    
    std::string url = build_push_url(pusher->stream_id);
    size_t path_start = url.find('/', url.find("://") + 3);
    std::string path = path_start != std::string::npos ? url.substr(path_start) : "/";
    
    size_t app_end = path.find('/', 1);
    std::string app = app_end != std::string::npos ? path.substr(1, app_end - 1) : path.substr(1);
    
    Logger::debug("Sending RTMP connect command to app: ", app);
    
    rtmp::CommandMessage connect_cmd;
    connect_cmd.name = "connect";
    connect_cmd.transaction_id = 1;
    
    amf0::Value::ObjectType connect_obj;
    connect_obj["app"] = amf0::Value::String(app);
    connect_obj["type"] = amf0::Value::String("nonprivate");
    connect_obj["tcUrl"] = amf0::Value::String(url.substr(0, path_start + app.size() + 1));
    connect_cmd.arguments.push_back(amf0::Value::Object(connect_obj));
    
    pusher->client->send_command(connect_cmd);
    Logger::debug("Connect command sent, txn_id: 1");
    
    // Force flush
    pusher->client->flush();
}

void RelayManager::handle_pusher_message(Pusher* pusher, const rtmp::Message& msg) {
    Logger::debug("Pusher received message type: ", static_cast<int>(msg.type_id), 
                  ", size: ", msg.payload.size());
    
    if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::CommandAMF0)) {
        auto cmd = rtmp::CommandMessage::parse(msg.payload);
        if (cmd) {
            Logger::debug("Pusher received command: ", cmd->name, ", txn_id: ", cmd->transaction_id);
            
            if (cmd->name == "_result" && !pusher->publishing) {
                if (cmd->transaction_id == 1) {
                    Logger::debug("Sending createStream command");
                    rtmp::CommandMessage create_stream;
                    create_stream.name = "createStream";
                    create_stream.transaction_id = 2;
                    create_stream.arguments.push_back(amf0::Value::Null());
                    
                    pusher->client->send_command(create_stream);
                    pusher->client->flush();
                }
                else if (cmd->transaction_id == 2) {
                    Logger::debug("Sending publish command for stream: ", pusher->stream_id.stream);
                    rtmp::CommandMessage publish_cmd;
                    publish_cmd.name = "publish";
                    publish_cmd.transaction_id = 0;
                    publish_cmd.arguments.push_back(amf0::Value::Null());
                    publish_cmd.arguments.push_back(amf0::Value::String(pusher->stream_id.stream));
                    publish_cmd.arguments.push_back(amf0::Value::String("live"));
                    
                    pusher->client->send_command(publish_cmd);
                    pusher->client->flush();
                    pusher->publishing = true;
                    
                    Logger::info("Pusher publishing to ", pusher->stream_id.to_string());
                    
                    if (!pusher->buffer.empty()) {
                        Logger::debug("Flushing ", pusher->buffer.size(), " buffered messages");
                    }
                    for (const auto& buffered_msg : pusher->buffer) {
                        pusher->client->send_message(buffered_msg);
                    }
                    pusher->buffer.clear();
                    pusher->pending_bytes = 0;
                }
            } else if (cmd->name == "_error") {
                Logger::error("Pusher received error from server");
            } else if (cmd->name == "onStatus") {
                Logger::debug("Pusher received onStatus");
            }
        }
    }
}

void RelayManager::handle_pusher_disconnected(Pusher* pusher) {
    Logger::warn("Pusher disconnected for ", pusher->stream_id.to_string());
    
    // Mark for reconnect, don't access pusher after this
    StreamId stream_id = pusher->stream_id;
    uint32_t delay_ms = pusher->reconnect_delay_ms;
    
    pusher->connected = false;
    pusher->publishing = false;
    
    // Create new client within same Pusher struct
    pusher->client.reset();  // Destroy old client (removes from epoll)
    pusher->client = std::make_unique<rtmp::Client>(loop_);
    
    // Re-setup callbacks with same pusher pointer
    pusher->client->set_connected_callback([this, pusher]() {
        Logger::info("Pusher connected_callback fired for ", pusher->stream_id.to_string());
        handle_pusher_connected(pusher);
    });
    
    pusher->client->set_message_callback([this, pusher](const rtmp::Message& msg) {
        handle_pusher_message(pusher, msg);
    });
    
    pusher->client->set_disconnected_callback([this, pusher]() {
        handle_pusher_disconnected(pusher);
    });
    
    // Update reconnect delay with exponential backoff
    pusher->reconnect_delay_ms = std::min(delay_ms * 2, 5000u);
    
    // Reconnect
    std::string url = build_push_url(stream_id);
    size_t proto_end = url.find("://");
    if (proto_end == std::string::npos) {
        Logger::error("Invalid push URL: ", url);
        return;
    }
    
    size_t host_start = proto_end + 3;
    size_t path_start = url.find('/', host_start);
    
    std::string host_port = url.substr(host_start, path_start - host_start);
    auto addr_result = Socket::parse_address(host_port);
    
    if (addr_result.is_err()) {
        Logger::error("Failed to parse push URL: ", addr_result.error());
        return;
    }
    
    auto [host, port] = addr_result.value();
    
    Logger::info("Reconnecting pusher to ", host, ":", port);
    
    // Note: Immediate reconnection (no delay) as event loop doesn't have timer support
    // The exponential backoff delay is calculated but not enforced here
    auto connect_result = pusher->client->connect(host, port);
    if (connect_result.is_err()) {
        Logger::error("Pusher reconnect failed: ", connect_result.error());
    } else {
        Logger::debug("Pusher reconnect initiated");
    }
}

void RelayManager::relay_message(Pusher* pusher, const rtmp::Message& msg) {
    if (msg.type_id != static_cast<uint8_t>(rtmp::MessageType::Audio) &&
        msg.type_id != static_cast<uint8_t>(rtmp::MessageType::Video) &&
        msg.type_id != static_cast<uint8_t>(rtmp::MessageType::DataAMF0)) {
        return;
    }
    
    if (pusher->publishing) {
        const char* type_name = "unknown";
        if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::Audio)) {
            type_name = "audio";
        } else if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::Video)) {
            type_name = "video";
        } else if (msg.type_id == static_cast<uint8_t>(rtmp::MessageType::DataAMF0)) {
            type_name = "data";
        }
        Logger::debug("Relaying ", type_name, " message to pusher, timestamp: ", msg.timestamp, ", size: ", msg.payload.size());
        pusher->client->send_message(msg);
    } else {
        pusher->pending_bytes += msg.payload.size();
        
        if (pusher->pending_bytes > policy_.max_pending_bytes) {
            if (policy_.backpressure == BackpressurePolicy::DropOldest && !pusher->buffer.empty()) {
                size_t dropped_size = pusher->buffer.front().payload.size();
                pusher->buffer.erase(pusher->buffer.begin());
                pusher->pending_bytes -= dropped_size;
                Logger::debug("Dropped oldest message, pending: ", pusher->pending_bytes, " bytes");
            }
            else if (policy_.backpressure == BackpressurePolicy::DropNewest) {
                pusher->pending_bytes -= msg.payload.size();
                Logger::debug("Dropped newest message, pending: ", pusher->pending_bytes, " bytes");
                return;
            }
        }
        
        pusher->buffer.push_back(msg);
        Logger::debug("Buffering message (not yet publishing), pending: ", pusher->pending_bytes, " bytes, buffer size: ", pusher->buffer.size());
    }
}

std::string RelayManager::build_push_url(const StreamId& stream_id) {
    std::string result = push_template_;
    
    size_t pos = result.find("{app}");
    if (pos != std::string::npos) {
        result.replace(pos, 5, stream_id.app);
    }
    
    pos = result.find("{stream}");
    if (pos != std::string::npos) {
        result.replace(pos, 8, stream_id.stream);
    }
    
    if (push_url_.empty()) {
        return result;
    }
    
    if (push_url_.back() == '/') {
        return push_url_ + result;
    }
    return push_url_ + "/" + result;
}

}
