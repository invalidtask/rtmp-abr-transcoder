#include "net/socket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

Socket::Socket() : fd_(-1) {}

Socket::Socket(int fd) : fd_(fd) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

Result<void> Socket::set_nonblocking() {
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags == -1) {
        return Result<void>::Err("fcntl F_GETFL failed: " + std::string(strerror(errno)));
    }
    if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
        return Result<void>::Err("fcntl F_SETFL failed: " + std::string(strerror(errno)));
    }
    return Result<void>::Ok();
}

Result<void> Socket::set_reuseaddr() {
    int opt = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        return Result<void>::Err("setsockopt SO_REUSEADDR failed: " + std::string(strerror(errno)));
    }
    return Result<void>::Ok();
}

Result<void> Socket::set_nodelay() {
    int opt = 1;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        return Result<void>::Err("setsockopt TCP_NODELAY failed: " + std::string(strerror(errno)));
    }
    return Result<void>::Ok();
}

Result<void> Socket::bind(const std::string& addr, uint16_t port) {
    sockaddr_in addr_in{};
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(port);
    
    if (addr == "0.0.0.0" || addr.empty()) {
        addr_in.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, addr.c_str(), &addr_in.sin_addr) <= 0) {
            return Result<void>::Err("Invalid address: " + addr);
        }
    }
    
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in)) == -1) {
        return Result<void>::Err("bind failed: " + std::string(strerror(errno)));
    }
    
    return Result<void>::Ok();
}

Result<void> Socket::listen(int backlog) {
    if (::listen(fd_, backlog) == -1) {
        return Result<void>::Err("listen failed: " + std::string(strerror(errno)));
    }
    return Result<void>::Ok();
}

Result<Socket> Socket::accept() {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Result<Socket>::Err("EAGAIN");
        }
        return Result<Socket>::Err("accept failed: " + std::string(strerror(errno)));
    }
    
    return Result<Socket>(Socket(client_fd));
}

Result<void> Socket::connect(const std::string& addr, uint16_t port) {
    sockaddr_in addr_in{};
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(port);
    
    if (inet_pton(AF_INET, addr.c_str(), &addr_in.sin_addr) <= 0) {
        return Result<void>::Err("Invalid address: " + addr);
    }
    
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in)) == -1) {
        if (errno == EINPROGRESS) {
            return Result<void>::Ok();
        }
        return Result<void>::Err("connect failed: " + std::string(strerror(errno)));
    }
    
    return Result<void>::Ok();
}

Result<size_t> Socket::read(uint8_t* buf, size_t len) {
    ssize_t n = ::read(fd_, buf, len);
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Result<size_t>(0);
        }
        return Result<size_t>::Err("read failed: " + std::string(strerror(errno)));
    }
    return Result<size_t>(static_cast<size_t>(n));
}

Result<size_t> Socket::write(const uint8_t* buf, size_t len) {
    ssize_t n = ::write(fd_, buf, len);
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Result<size_t>(0);
        }
        return Result<size_t>::Err("write failed: " + std::string(strerror(errno)));
    }
    return Result<size_t>(static_cast<size_t>(n));
}

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Result<std::pair<std::string, uint16_t>> Socket::parse_address(const std::string& addr_port) {
    std::string hostname;
    uint16_t port = 1935;  // Default RTMP port
    
    size_t colon_pos = addr_port.rfind(':');
    if (colon_pos == std::string::npos) {
        // No port specified, use hostname as-is with default port
        hostname = addr_port;
    } else {
        // Port specified
        hostname = addr_port.substr(0, colon_pos);
        std::string port_str = addr_port.substr(colon_pos + 1);
        
        try {
            port = static_cast<uint16_t>(std::stoi(port_str));
        } catch (...) {
            return Result<std::pair<std::string, uint16_t>>::Err("Invalid port number");
        }
    }
    
    // Try to resolve hostname to IP address using getaddrinfo
    addrinfo hints{};
    hints.ai_family = AF_INET;  // IPv4
    hints.ai_socktype = SOCK_STREAM;
    
    addrinfo* result = nullptr;
    int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    
    if (ret != 0) {
        return Result<std::pair<std::string, uint16_t>>::Err(
            "Failed to resolve hostname: " + std::string(gai_strerror(ret))
        );
    }
    
    // Get the first IPv4 address
    std::string ip_addr;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
            char addr_buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &addr_in->sin_addr, addr_buf, sizeof(addr_buf))) {
                ip_addr = addr_buf;
                break;
            }
        }
    }
    
    freeaddrinfo(result);
    
    if (ip_addr.empty()) {
        return Result<std::pair<std::string, uint16_t>>::Err("No IPv4 address found for hostname");
    }
    
    return Result<std::pair<std::string, uint16_t>>(std::make_pair(ip_addr, port));
}
