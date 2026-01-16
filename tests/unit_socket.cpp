#include "test_framework.hpp"
#include "net/socket.hpp"

TEST_CASE(socket_parse_address_with_port) {
    auto result = Socket::parse_address("127.0.0.1:1935");
    REQUIRE(result.is_ok());
    
    auto [addr, port] = result.value();
    REQUIRE_EQUAL(addr, "127.0.0.1");
    REQUIRE_EQUAL(port, 1935);
}

TEST_CASE(socket_parse_address_without_port) {
    auto result = Socket::parse_address("localhost");
    REQUIRE(result.is_ok());
    
    auto [addr, port] = result.value();
    REQUIRE_EQUAL(addr, "127.0.0.1");
    REQUIRE_EQUAL(port, 1935);  // Default RTMP port
}

TEST_CASE(socket_parse_address_hostname_with_port) {
    auto result = Socket::parse_address("localhost:1936");
    REQUIRE(result.is_ok());
    
    auto [addr, port] = result.value();
    REQUIRE_EQUAL(addr, "127.0.0.1");
    REQUIRE_EQUAL(port, 1936);
}

TEST_CASE(socket_parse_address_ip_without_port) {
    auto result = Socket::parse_address("192.168.1.1");
    REQUIRE(result.is_ok());
    
    auto [addr, port] = result.value();
    REQUIRE_EQUAL(addr, "192.168.1.1");
    REQUIRE_EQUAL(port, 1935);  // Default RTMP port
}

TEST_CASE(socket_parse_address_invalid_hostname) {
    auto result = Socket::parse_address("this-hostname-does-not-exist-12345.invalid");
    REQUIRE(result.is_err());
}

TEST_CASE(socket_parse_address_invalid_port) {
    auto result = Socket::parse_address("127.0.0.1:invalid");
    REQUIRE(result.is_err());
}
