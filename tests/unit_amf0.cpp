#include "test_framework.hpp"
#include "rtmp/amf0.hpp"
#include <cmath>

TEST_CASE(amf0_encode_decode_number) {
    auto value = amf0::Value::Number(42.5);
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_number());
    REQUIRE(std::abs(decoded.as_number() - 42.5) < 0.001);
}

TEST_CASE(amf0_encode_decode_boolean_true) {
    auto value = amf0::Value::Boolean(true);
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_boolean());
    REQUIRE_EQUAL(decoded.as_boolean(), true);
}

TEST_CASE(amf0_encode_decode_boolean_false) {
    auto value = amf0::Value::Boolean(false);
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_boolean());
    REQUIRE_EQUAL(decoded.as_boolean(), false);
}

TEST_CASE(amf0_encode_decode_string) {
    auto value = amf0::Value::String("Hello, AMF0!");
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_string());
    REQUIRE_EQUAL(decoded.as_string(), "Hello, AMF0!");
}

TEST_CASE(amf0_encode_decode_null) {
    auto value = amf0::Value::Null();
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_null());
}

TEST_CASE(amf0_encode_decode_object) {
    amf0::Value::ObjectType obj;
    obj["name"] = amf0::Value::String("test");
    obj["value"] = amf0::Value::Number(123);
    obj["flag"] = amf0::Value::Boolean(true);
    
    auto value = amf0::Value::Object(obj);
    auto encoded = amf0::encode(value);
    REQUIRE(encoded.is_ok());
    
    auto decoded_pair = amf0::decode(encoded.value());
    REQUIRE(decoded_pair.is_ok());
    
    auto decoded = decoded_pair.value().first;
    REQUIRE(decoded.is_object());
    
    auto& decoded_obj = decoded.as_object();
    REQUIRE_EQUAL(decoded_obj.size(), 3);
    REQUIRE(decoded_obj.count("name") > 0);
    REQUIRE(decoded_obj.at("name").is_string());
    REQUIRE_EQUAL(decoded_obj.at("name").as_string(), "test");
    REQUIRE(decoded_obj.count("value") > 0);
    REQUIRE(decoded_obj.at("value").is_number());
    REQUIRE(std::abs(decoded_obj.at("value").as_number() - 123) < 0.001);
}

TEST_CASE(amf0_encode_decode_array) {
    std::vector<amf0::Value> values;
    values.push_back(amf0::Value::String("connect"));
    values.push_back(amf0::Value::Number(1.0));
    
    amf0::Value::ObjectType obj;
    obj["app"] = amf0::Value::String("live");
    values.push_back(amf0::Value::Object(obj));
    
    auto encoded = amf0::encode_array(values);
    REQUIRE(encoded.is_ok());
    
    auto decoded = amf0::decode_array(encoded.value());
    REQUIRE(decoded.is_ok());
    
    auto& decoded_array = decoded.value();
    REQUIRE_EQUAL(decoded_array.size(), 3);
    REQUIRE(decoded_array[0].is_string());
    REQUIRE_EQUAL(decoded_array[0].as_string(), "connect");
    REQUIRE(decoded_array[1].is_number());
    REQUIRE(std::abs(decoded_array[1].as_number() - 1.0) < 0.001);
    REQUIRE(decoded_array[2].is_object());
}
