#include "rtmp/rtmp_handshake.hpp"
#include "core/time.hpp"
#include <cstring>
#include <random>

namespace rtmp {

Handshake::Handshake() : state_(State::Uninitialized) {}

std::vector<uint8_t> Handshake::generate_c0_c1() {
    std::vector<uint8_t> data(1 + 1536);
    
    data[0] = 3;
    
    std::memset(data.data() + 1, 0, 4);
    std::memset(data.data() + 5, 0, 4);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 9; i < 1537; ++i) {
        data[i] = static_cast<uint8_t>(dis(gen));
    }
    
    c1_data_.assign(data.begin() + 1, data.end());
    state_ = State::C0C1Sent;
    
    return data;
}

std::vector<uint8_t> Handshake::generate_s0_s1_s2(std::span<const uint8_t> c1) {
    std::vector<uint8_t> data(1 + 1536 + 1536);
    
    data[0] = 3;
    
    std::memset(data.data() + 1, 0, 4);
    std::memset(data.data() + 5, 0, 4);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 9; i < 1537; ++i) {
        data[i] = static_cast<uint8_t>(dis(gen));
    }
    
    s1_data_.assign(data.begin() + 1, data.begin() + 1537);
    
    std::memcpy(data.data() + 1537, c1.data(), 1536);
    
    state_ = State::S0S1S2Sent;
    
    return data;
}

std::vector<uint8_t> Handshake::generate_c2(std::span<const uint8_t> s1) {
    std::vector<uint8_t> data(1536);
    std::memcpy(data.data(), s1.data(), 1536);
    state_ = State::C2Sent;
    return data;
}

Result<size_t> Handshake::process_client_handshake(std::span<const uint8_t> data) {
    if (state_ == State::Uninitialized) {
        if (data.size() < 1537) {
            return Result<size_t>::Err("Not enough data for C0+C1");
        }
        
        if (data[0] != 3) {
            return Result<size_t>::Err("Invalid RTMP version");
        }
        
        c1_data_.assign(data.begin() + 1, data.begin() + 1537);
        state_ = State::S0S1S2Sent;
        
        if (data.size() >= 1537 + 1536) {
            state_ = State::Done;
            return Result<size_t>(1537 + 1536);
        }
        
        return Result<size_t>(1537);
    }
    else if (state_ == State::S0S1S2Sent) {
        if (data.size() < 1536) {
            return Result<size_t>::Err("Not enough data for C2");
        }
        
        state_ = State::Done;
        return Result<size_t>(1536);
    }
    
    return Result<size_t>::Err("Invalid state");
}

Result<size_t> Handshake::process_server_handshake(std::span<const uint8_t> data) {
    if (state_ == State::C0C1Sent) {
        if (data.size() < 1 + 1536 + 1536) {
            return Result<size_t>::Err("Not enough data for S0+S1+S2");
        }
        
        if (data[0] != 3) {
            return Result<size_t>::Err("Invalid RTMP version");
        }
        
        s1_data_.assign(data.begin() + 1, data.begin() + 1537);
        state_ = State::Done;
        
        return Result<size_t>(1 + 1536 + 1536);
    }
    
    return Result<size_t>::Err("Invalid state");
}

}
