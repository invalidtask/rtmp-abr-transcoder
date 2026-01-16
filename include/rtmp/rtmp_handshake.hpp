#pragma once
#include "core/result.hpp"
#include <cstdint>
#include <vector>
#include <span>

namespace rtmp {

class Handshake {
public:
    enum class State {
        Uninitialized,
        C0C1Sent,
        S0S1S2Sent,
        C2Sent,
        Done
    };
    
    Handshake();
    
    std::vector<uint8_t> generate_c0_c1();
    std::vector<uint8_t> generate_s0_s1_s2(std::span<const uint8_t> c1);
    std::vector<uint8_t> generate_c2(std::span<const uint8_t> s1);
    
    Result<size_t> process_client_handshake(std::span<const uint8_t> data);
    Result<size_t> process_server_handshake(std::span<const uint8_t> data);
    
    bool is_done() const { return state_ == State::Done; }
    State state() const { return state_; }
    
    const std::vector<uint8_t>& s1_data() const { return s1_data_; }
    const std::vector<uint8_t>& c1_data() const { return c1_data_; }
    
private:
    State state_;
    std::vector<uint8_t> c1_data_;
    std::vector<uint8_t> s1_data_;
};

}
