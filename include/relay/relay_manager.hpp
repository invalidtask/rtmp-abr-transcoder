#pragma once
#include "stream_id.hpp"
#include "policy.hpp"
#include "rtmp/rtmp_server.hpp"
#include "rtmp/rtmp_client.hpp"
#include "net/epoll_loop.hpp"
#include <memory>
#include <map>
#include <string>

namespace transcode {
    class Transcoder;
}

namespace relay {

struct SinkStats {
    uint64_t audio_messages = 0;
    uint64_t video_messages = 0;
    uint64_t data_messages = 0;
    uint64_t audio_bytes = 0;
    uint64_t video_bytes = 0;
    uint64_t keyframes = 0;
    uint64_t start_time = 0;
};

class RelayManager {
public:
    explicit RelayManager(EpollLoop& loop, const RelayPolicy& policy);
    
    void set_push_url(const std::string& url);
    void set_push_template(const std::string& tmpl);
    void set_transcoder(std::shared_ptr<transcode::Transcoder> transcoder);
    
    Result<void> start_server(const std::string& addr, uint16_t port);
    
    void handle_new_publisher(std::shared_ptr<rtmp::Session> session, Socket socket);
    
private:
    struct Publisher {
        std::shared_ptr<rtmp::Session> session;
        Socket socket;
        StreamId stream_id;
        bool ready = false;
        std::vector<rtmp::Message> pending_messages;
        SinkStats stats;
        uint64_t last_stats_log = 0;
    };
    
    struct Pusher {
        std::unique_ptr<rtmp::Client> client;
        StreamId stream_id;
        bool connected = false;
        bool publishing = false;
        uint64_t last_disconnect_time = 0;
        uint32_t reconnect_delay_ms = 100;
        size_t pending_bytes = 0;
        std::vector<rtmp::Message> buffer;
    };
    
    void handle_publisher_message(Publisher* pub, const rtmp::Message& msg);
    void handle_publisher_command(Publisher* pub, const rtmp::CommandMessage& cmd);
    void setup_publisher_read(Publisher* pub);
    void remove_publisher(Publisher* pub);
    void log_stats(Publisher* pub);
    void flush_publisher_responses(Publisher* pub);
    
    void create_pusher(const StreamId& stream_id);
    void handle_pusher_connected(Pusher* pusher);
    void handle_pusher_message(Pusher* pusher, const rtmp::Message& msg);
    void handle_pusher_disconnected(Pusher* pusher);
    void relay_message(Pusher* pusher, const rtmp::Message& msg);
    
    std::string build_push_url(const StreamId& stream_id);
    
    EpollLoop& loop_;
    RelayPolicy policy_;
    std::string push_url_;
    std::string push_template_;
    std::shared_ptr<transcode::Transcoder> transcoder_;
    
    std::unique_ptr<rtmp::Server> server_;
    std::map<Publisher*, std::unique_ptr<Publisher>> publishers_;
    std::map<StreamId, std::unique_ptr<Pusher>> pushers_;
};

}
