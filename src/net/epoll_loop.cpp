#include "net/epoll_loop.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

EpollLoop::EpollLoop() : running_(false) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("epoll_create1 failed");
    }
}

EpollLoop::~EpollLoop() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

Result<void> EpollLoop::add(int fd, uint32_t events, EpollCallback callback) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        return Result<void>::Err("epoll_ctl ADD failed: " + std::string(strerror(errno)));
    }
    
    callbacks_[fd] = std::move(callback);
    return Result<void>::Ok();
}

Result<void> EpollLoop::modify(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        return Result<void>::Err("epoll_ctl MOD failed: " + std::string(strerror(errno)));
    }
    
    return Result<void>::Ok();
}

Result<void> EpollLoop::remove(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        return Result<void>::Err("epoll_ctl DEL failed: " + std::string(strerror(errno)));
    }
    
    callbacks_.erase(fd);
    return Result<void>::Ok();
}

Result<void> EpollLoop::run_once(int timeout_ms) {
    epoll_event events[32];
    int nfds = epoll_wait(epoll_fd_, events, 32, timeout_ms);
    
    if (nfds == -1) {
        if (errno == EINTR) {
            return Result<void>::Ok();
        }
        return Result<void>::Err("epoll_wait failed: " + std::string(strerror(errno)));
    }
    
    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            it->second(events[i].events);
        }
    }
    
    return Result<void>::Ok();
}

void EpollLoop::start() {
    running_ = true;
}

void EpollLoop::stop() {
    running_ = false;
}
