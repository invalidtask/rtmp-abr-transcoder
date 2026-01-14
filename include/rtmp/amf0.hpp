#pragma once
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <span>
#include "core/result.hpp"

namespace amf0 {

enum class Type : uint8_t {
    Number = 0,
    Boolean = 1,
    String = 2,
    Object = 3,
    Null = 5,
    EcmaArray = 8
};

class Value {
public:
    using ObjectType = std::map<std::string, Value>;
    using ArrayType = std::map<std::string, Value>;
    
    Value() : data_(nullptr) {}
    Value(double num) : data_(num) {}
    Value(bool b) : data_(b) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(ObjectType obj) : data_(std::move(obj)) {}
    
    static Value Null() { return Value(); }
    static Value Number(double n) { return Value(n); }
    static Value Boolean(bool b) { return Value(b); }
    static Value String(std::string s) { return Value(std::move(s)); }
    static Value Object(ObjectType obj) { return Value(std::move(obj)); }
    static Value EcmaArray(ArrayType arr);
    
    bool is_number() const { return std::holds_alternative<double>(data_); }
    bool is_boolean() const { return std::holds_alternative<bool>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_object() const { return std::holds_alternative<ObjectType>(data_); }
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_ecma_array() const { return is_object(); }
    
    double as_number() const { return std::get<double>(data_); }
    bool as_boolean() const { return std::get<bool>(data_); }
    const std::string& as_string() const { return std::get<std::string>(data_); }
    const ObjectType& as_object() const { return std::get<ObjectType>(data_); }
    ObjectType& as_object() { return std::get<ObjectType>(data_); }
    
    Type type() const;
    
private:
    std::variant<std::nullptr_t, double, bool, std::string, ObjectType> data_;
};

Result<std::vector<uint8_t>> encode(const Value& value);
Result<std::vector<uint8_t>> encode_array(const std::vector<Value>& values);

Result<std::pair<Value, size_t>> decode(std::span<const uint8_t> data);
Result<std::vector<Value>> decode_array(std::span<const uint8_t> data);

}
