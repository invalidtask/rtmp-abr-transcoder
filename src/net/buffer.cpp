#include "net/buffer.hpp"
#include <algorithm>

Buffer::Buffer(size_t capacity) {
    buffer_.reserve(capacity);
}

void Buffer::append(const uint8_t* data, size_t len) {
    if (write_pos_ + len > buffer_.size()) {
        if (read_pos_ > 0 && write_pos_ - read_pos_ + len <= buffer_.capacity()) {
            compact();
        } else {
            buffer_.resize(write_pos_ + len);
        }
    }
    std::memcpy(buffer_.data() + write_pos_, data, len);
    write_pos_ += len;
}

void Buffer::append(std::span<const uint8_t> data) {
    append(data.data(), data.size());
}

void Buffer::append(const std::vector<uint8_t>& data) {
    append(data.data(), data.size());
}

void Buffer::consume(size_t n) {
    read_pos_ += std::min(n, size());
    if (read_pos_ == write_pos_) {
        read_pos_ = 0;
        write_pos_ = 0;
    }
}

void Buffer::clear() {
    read_pos_ = 0;
    write_pos_ = 0;
}

void Buffer::reserve(size_t capacity) {
    if (capacity > buffer_.capacity()) {
        compact();
        buffer_.reserve(capacity);
    }
}

void Buffer::compact() {
    if (read_pos_ > 0) {
        size_t data_size = write_pos_ - read_pos_;
        std::memmove(buffer_.data(), buffer_.data() + read_pos_, data_size);
        read_pos_ = 0;
        write_pos_ = data_size;
    }
}
