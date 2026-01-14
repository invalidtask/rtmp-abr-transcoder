#pragma once
#include <vector>
#include <span>
#include <cstdint>
#include <cstring>

class Buffer {
public:
    Buffer() = default;
    explicit Buffer(size_t capacity);
    
    void append(const uint8_t* data, size_t len);
    void append(std::span<const uint8_t> data);
    void append(const std::vector<uint8_t>& data);
    
    void consume(size_t n);
    void clear();
    
    const uint8_t* data() const { return buffer_.data() + read_pos_; }
    size_t size() const { return write_pos_ - read_pos_; }
    bool empty() const { return read_pos_ == write_pos_; }
    
    std::span<const uint8_t> readable() const {
        return std::span<const uint8_t>(buffer_.data() + read_pos_, write_pos_ - read_pos_);
    }
    
    void reserve(size_t capacity);
    
private:
    void compact();
    
    std::vector<uint8_t> buffer_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
};
