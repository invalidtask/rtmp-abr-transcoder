#pragma once
#include "core/result.hpp"
#include <cstdint>
#include <functional>
#include <map>

enum class EpollEventType {
    Read = 1,
    Write = 2,
    Error = 4,
    HangUp = 8
};

using EpollCallback = std::function<void(uint32_t events)>;

class EpollLoop {
public:
    EpollLoop();
    ~EpollLoop();
    
    EpollLoop(const EpollLoop&) = delete;
    EpollLoop& operator=(const EpollLoop&) = delete;
    
    Result<void> add(int fd, uint32_t events, EpollCallback callback);
    Result<void> modify(int fd, uint32_t events);
    Result<void> remove(int fd);
    
    Result<void> run_once(int timeout_ms = -1);
    void start();
    void stop();
    
    bool is_running() const { return running_; }
    
private:
    int epoll_fd_;
    bool running_;
    std::map<int, EpollCallback> callbacks_;
};
