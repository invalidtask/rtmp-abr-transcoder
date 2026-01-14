#pragma once
#include "core/result.hpp"
#include <string>
#include <cstdint>
#include <optional>

class Socket {
public:
    Socket();
    explicit Socket(int fd);
    ~Socket();
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    
    Result<void> set_nonblocking();
    Result<void> set_reuseaddr();
    Result<void> set_nodelay();
    
    Result<void> bind(const std::string& addr, uint16_t port);
    Result<void> listen(int backlog = 128);
    Result<Socket> accept();
    Result<void> connect(const std::string& addr, uint16_t port);
    
    Result<size_t> read(uint8_t* buf, size_t len);
    Result<size_t> write(const uint8_t* buf, size_t len);
    
    void close();
    int fd() const { return fd_; }
    bool is_valid() const { return fd_ >= 0; }
    
    static Result<std::pair<std::string, uint16_t>> parse_address(const std::string& addr_port);
    
private:
    int fd_;
};
