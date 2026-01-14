#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <span>

namespace bytes {

inline uint16_t read_u16_be(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

inline uint32_t read_u24_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) |
           data[2];
}

inline uint32_t read_u32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           data[3];
}

inline uint32_t read_u32_le(const uint8_t* data) {
    return data[0] |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

inline double read_f64_be(const uint8_t* data) {
    uint64_t val = (static_cast<uint64_t>(data[0]) << 56) |
                   (static_cast<uint64_t>(data[1]) << 48) |
                   (static_cast<uint64_t>(data[2]) << 40) |
                   (static_cast<uint64_t>(data[3]) << 32) |
                   (static_cast<uint64_t>(data[4]) << 24) |
                   (static_cast<uint64_t>(data[5]) << 16) |
                   (static_cast<uint64_t>(data[6]) << 8) |
                   data[7];
    double result;
    std::memcpy(&result, &val, sizeof(result));
    return result;
}

inline void write_u16_be(uint8_t* data, uint16_t val) {
    data[0] = (val >> 8) & 0xFF;
    data[1] = val & 0xFF;
}

inline void write_u24_be(uint8_t* data, uint32_t val) {
    data[0] = (val >> 16) & 0xFF;
    data[1] = (val >> 8) & 0xFF;
    data[2] = val & 0xFF;
}

inline void write_u32_be(uint8_t* data, uint32_t val) {
    data[0] = (val >> 24) & 0xFF;
    data[1] = (val >> 16) & 0xFF;
    data[2] = (val >> 8) & 0xFF;
    data[3] = val & 0xFF;
}

inline void write_u32_le(uint8_t* data, uint32_t val) {
    data[0] = val & 0xFF;
    data[1] = (val >> 8) & 0xFF;
    data[2] = (val >> 16) & 0xFF;
    data[3] = (val >> 24) & 0xFF;
}

inline void write_f64_be(uint8_t* data, double val) {
    uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    data[0] = (bits >> 56) & 0xFF;
    data[1] = (bits >> 48) & 0xFF;
    data[2] = (bits >> 40) & 0xFF;
    data[3] = (bits >> 32) & 0xFF;
    data[4] = (bits >> 24) & 0xFF;
    data[5] = (bits >> 16) & 0xFF;
    data[6] = (bits >> 8) & 0xFF;
    data[7] = bits & 0xFF;
}

}
